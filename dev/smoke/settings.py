#!/usr/bin/env python3
"""Checks that every setting does something: changes each one in a running client, from a view where it should show,
and compares the picture with the one before.

    python dev/smoke/settings.py [--sweep <file.json>] [--out <folder>] [--loader fabric|neoforge|forge]
                                 [--only <view>[,<view>...]] [--report <folder>] [--list]

It walks a development client through the views of a sweep file (default settings.json). At each view it takes a
picture, then for each setting listed there: changes it, waits for the picture to settle, takes a picture, puts the
setting back, waits again and takes a picture of the unchanged view. How much the unchanged pictures before and after
a setting differ is the noise around it (the denoiser, moving clouds, rain, an exposure still on its way back). A
setting whose picture differs from both by no more than that does nothing in that view.

A setting is named in the sweep file by what it is:

    "sun_glow"                                  a shader pack attribute, changed to the far end of its range (a
                                                switch is flipped, a choice moves to its next value)
    {"attr": "num_ray_bounces", "to": "1"}      the same, to a value given
    {"opt": "dayBrightness", "to": "100"}       an option of options.properties
    {"mattr": "tone_mapping.saturation"}        an attribute of another module
    {"module": "tone_mapping"}                  every attribute of that module
    {"name": "...", "set": [...], "undo": [...]}  DevAutomation actions as they are

and may carry "expect": "none" with a "why", for a setting that is known to need something the view lacks. The value
a setting is put back to is the one stored in the development settings, which the run backs up and restores as
smoke.py does.

It writes report.md (a table per view, with the pictures) and summary.json to the output folder, and lists the
shader pack attributes no view covers. It does not fail the build: what it finds is for a person to read.
"""

import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import smoke

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
RT = "render_pipeline.module.ray_tracing.attribute."
PACK = REPO / "native/src/shader/world/ray_tracing/internal/radiante-pt/configs.json"
MODULES = REPO / "common/src/main/resources/modules"
TRUE, FALSE = "render_pipeline.true", "render_pipeline.false"


def read_triplets(text):
    """The attributes of a pipeline or module YAML: name -> (type, value, runtime)."""
    found = {}
    for match in re.finditer(r"-\s*name:\s*\"?([\w.]+\.attribute\.[\w.]+)\"?\s*\n((?:\s+\w+:.*\n?)+)", text):
        body = match.group(2)
        kind = re.search(r"type:\s*\"?([^\"\n]+)\"?", body)
        value = re.search(r"value:\s*(.*)", body)
        if kind:
            text_value = value.group(1).strip().strip("'\"") if value else ""
            found[match.group(1)] = (kind.group(1).strip(), text_value, "runtime: true" in body)
    return found


def far_value(kind, current):
    """A value of the type as far from the current one as it goes."""
    if kind == "bool":
        return FALSE if current == TRUE else TRUE
    if kind.startswith("enum:"):
        values = kind[5:].split("-")
        return values[(values.index(current) + 1) % len(values)] if current in values else values[0]
    match = re.match(r"(int|float)_range:(-?[\d.]+)-(-?[\d.]+)", kind)
    if not match:
        return None
    low, high = float(match.group(2)), float(match.group(3))
    try:
        now = float(current)
    except ValueError:
        now = low
    target = high if now - low <= high - now else low
    return str(int(target)) if match.group(1) == "int" else repr(target)


class Settings:
    """What the development client has stored, and the types the pack and the modules declare."""

    def __init__(self, loader):
        folders = [REPO / f"run-{loader}", REPO / "run-shared"]
        self.pack = {a["name"]: a for a in json.loads(PACK.read_text(encoding="utf-8"))["attributes"]}
        self.pack_values = {a["name"]: a["default_value"] for a in self.pack.values()}
        self.options = {}
        self.modules = {}
        for path in sorted(MODULES.glob("*.yaml")):
            self.modules.update(read_triplets(path.read_text(encoding="utf-8")))
        for folder in reversed(folders):
            stored = folder / "radiante/shaders/world/ray_tracing/radiante-pt.zip.txt"
            if stored.is_file():
                for line in stored.read_text(encoding="utf-8").splitlines():
                    name, _, value = line.partition("=")
                    if name in self.pack_values:
                        self.pack_values[name] = value.strip()
            options = folder / "radiante/options.properties"
            if options.is_file():
                for line in options.read_text(encoding="utf-8").splitlines():
                    if "=" in line and not line.startswith("#"):
                        name, _, value = line.partition("=")
                        self.options[name.strip()] = value.strip()
            pipeline = folder / "radiante/pipeline.yaml"
            if pipeline.is_file():
                for name, (kind, value, runtime) in read_triplets(pipeline.read_text(encoding="utf-8")).items():
                    if name in self.modules:
                        self.modules[name] = (self.modules[name][0], value, self.modules[name][2])

    def expand(self, entry):
        """The tests an entry of a view stands for: dictionaries with name, set, undo, structural, expect, why."""
        if isinstance(entry, str):
            entry = {"attr": entry}
        if "module" in entry:
            prefix = f"render_pipeline.module.{entry['module']}.attribute."
            skip = set(entry.get("skip", []))
            return [test for name in self.modules if name.startswith(prefix) and name[len(prefix):] not in skip
                    for test in self.expand({"mattr": f"{entry['module']}.{name[len(prefix):]}"})]
        extra = {"expect": entry.get("expect"), "why": entry.get("why", ""), "settle": entry.get("settle")}
        if "set" in entry:
            return [dict(extra, name=entry["name"], set=entry["set"], undo=entry.get("undo", []),
                         structural=entry.get("structural", True), what=" ".join(entry["set"]))]
        if "opt" in entry:
            name = entry["opt"]
            if name not in self.options:
                sys.exit(f"{name} is not in the stored options.properties")
            return [dict(extra, name=entry.get("name", name), set=[f"opt={name}={entry['to']}"],
                         undo=[f"opt={name}={self.options[name]}"], structural=False,
                         what=f"{self.options[name]} -> {entry['to']}")]
        if "mattr" in entry:
            module, _, short = entry["mattr"].partition(".")
            name = f"render_pipeline.module.{module}.attribute.{short}"
            if name not in self.modules:
                sys.exit(f"no module declares {name}")
            kind, current, runtime = self.modules[name]
            to = self.value(kind, entry.get("to"), current, name)
            if to is None or kind == "string":
                return []
            module_name = f"render_pipeline.module.{module}.name"
            return [dict(extra, name=entry.get("name", f"{module}.{short}"),
                         set=[f"pipe=mattr:{module_name}:{name}:{to}"],
                         undo=[f"pipe=mattr:{module_name}:{name}:{current}"], structural=not runtime,
                         what=f"{self.short(current)} -> {self.short(to)}")]
        name = RT + entry["attr"]
        if name not in self.pack:
            sys.exit(f"the shader pack has no attribute {entry['attr']}")
        kind, current = self.pack[name]["type"], self.pack_values[name]
        to = self.value(kind, entry.get("to"), current, name)
        if to is None:
            sys.exit(f"{entry['attr']} ({kind}) needs a value given with \"to\"")
        return [dict(extra, name=entry.get("name", entry["attr"]), set=[f"pipe=attr:{name}:{to}"],
                     undo=[f"pipe=attr:{name}:{current}"], structural=not self.pack[name].get("runtime", False),
                     what=f"{self.short(current)} -> {self.short(to)}", attr=entry["attr"])]

    @staticmethod
    def short(value):
        return value.rsplit(".", 1)[-1] if value.startswith("render_pipeline.") else value

    @staticmethod
    def value(kind, to, current, name):
        if to is None:
            return far_value(kind, current)
        if kind == "bool":
            return TRUE if to in ("true", TRUE) else FALSE
        if kind.startswith("enum:") and not to.startswith("render_pipeline."):
            for value in kind[5:].split("-"):
                if value.rsplit(".", 1)[-1] == to:
                    return value
            sys.exit(f"{name} has no value {to}")
        return to


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sweep", default=str(HERE / "settings.json"))
    parser.add_argument("--out")
    parser.add_argument("--loader", default="fabric", choices=("fabric", "neoforge", "forge"))
    parser.add_argument("--only", help="only these views, by name, separated by commas")
    parser.add_argument("--timeout", type=int, default=5400)
    parser.add_argument("--list", action="store_true", help="print the tests and stop")
    parser.add_argument("--report", help="write the report of a run already made, from its output folder")
    args = parser.parse_args()

    config = json.loads(Path(args.sweep).read_text(encoding="utf-8"))
    settings = Settings(args.loader)
    settle = config.get("settle_ticks", 80)
    structural_settle = config.get("structural_settle_ticks", 110)

    steps = []
    tick = 100
    for action in config.get("setup", []):
        steps.append((tick, action))
        tick += 10
    tick += config.get("setup_ticks", 300)
    plan = []  # (view, [(test, its picture, the unchanged picture before it, the one after it)])
    covered = set()
    for view in config["views"]:
        tests = [test for entry in view["tests"] for test in settings.expand(entry)]
        covered.update(test["attr"] for test in tests if "attr" in test)
        if args.only and view["name"] not in args.only.split(","):
            continue
        for action in view.get("actions", []):
            steps.append((tick, action))
            tick += 4
        tick += view.get("settle_ticks", structural_settle)
        unchanged = []
        pictures = []

        def shot(name):
            nonlocal tick
            steps.append((tick, f"shot=sweep_{name}"))
            tick += 6
            return name

        unchanged.append(shot(f"{view['name']}__base0"))
        for test in tests:
            wait = test["settle"] or (structural_settle if test["structural"] else settle)
            for action in test["set"]:
                steps.append((tick, action))
                tick += 4
            tick += wait
            name = shot(f"{view['name']}__{test['name']}")
            for action in test["undo"]:
                steps.append((tick, action))
                tick += 4
            tick += wait
            unchanged.append(shot(f"{view['name']}__base{len(unchanged)}"))
            pictures.append((test, name, unchanged[-2], unchanged[-1]))
        for action in view.get("after", []):
            steps.append((tick, action))
            tick += 4
        tick += 20
        plan.append((view, pictures))
    steps.append((tick + 20, "quit"))

    count = sum(len(pictures) for _, pictures in plan)
    uncovered = sorted(name[len(RT):] for name in settings.pack if name[len(RT):] not in covered)
    if args.list:
        for view, pictures in plan:
            print(view["name"])
            for test, _, _, _ in pictures:
                print(f"  {test['name']:<46} {test['what']}")
        print(f"{count} tests, {tick} ticks; pack attributes in no view: {uncovered}")
        return 0

    if args.report:
        return report(Path(args.report).resolve(), plan, uncovered, REPO / "screenshots")
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    # Absolute: the client reads the script from its own working folder.
    out = (Path(args.out) if args.out else REPO / "build" / "settings-sweep" / stamp).resolve()
    out.mkdir(parents=True, exist_ok=True)
    folders = [REPO / "run-shared", REPO / f"run-{args.loader}"]
    backup = out / "settings-backup"
    smoke.backup_settings(folders, backup)
    screenshots = REPO / "screenshots"
    for stale in screenshots.glob("sweep_*.png"):
        stale.unlink()
    script = out / "script.txt"
    script.write_text("\n".join(f"{at}:{action}" for at, action in steps), encoding="utf-8")

    environment = dict(os.environ)
    environment["RADIANTE_DEV_PRESCRIPT"] = f"100:testworld={config['world']}"
    environment["RADIANTE_DEV_SCRIPT"] = "@" + str(script)
    gradlew = REPO / ("gradlew.bat" if os.name == "nt" else "gradlew")
    log_path = out / "client.log"
    print(f"starting the {args.loader} client: {count} settings over {len(plan)} views, about "
          f"{tick // 1200 + 1} minutes of game time, log in {log_path}", flush=True)
    try:
        with open(log_path, "w", encoding="utf-8", errors="replace") as log:
            subprocess.run([str(gradlew), f":{args.loader}:runClient"], cwd=REPO, env=environment, stdout=log,
                           stderr=subprocess.STDOUT, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        print(f"the client did not quit within {args.timeout} s")
    finally:
        smoke.restore_settings(folders, backup)
    return report(out, plan, uncovered, screenshots)


def compare(a, b):
    """How two pictures differ, in percent: the mean difference of full scale, the share of pixels that moved by
    more than a few steps, and the share that moved a lot. The last is what a thin line (the block outline) shows in,
    under noise that moves many pixels a little. None for pictures that cannot be compared (one is missing, or the
    window changed size between them)."""
    import numpy
    if a is None or b is None or a.shape != b.shape:
        return None
    delta = numpy.abs(a - b).max(axis=2)
    return (float(numpy.abs(a - b).mean() * 100.0), float((delta > 8.0 / 255.0).mean() * 100.0),
            float((delta > 32.0 / 255.0).mean() * 100.0))


def local_change(picture, before, after, size=32):
    """A change in one place of the picture (the sun's square turned, a block's outline): how many tiles of the
    picture differ from both unchanged pictures by clearly more than those differ from each other there, and where the
    largest such tile is. Noise that stays in one place (a sign's text, a flame) is in the unchanged pictures too and
    does not count."""
    import numpy
    if picture is None or before is None or after is None or not (picture.shape == before.shape == after.shape):
        return 0, None

    def tiles(a, b):
        delta = numpy.abs(a - b).max(axis=2)
        height, width = delta.shape
        return delta[:height // size * size, :width // size * size].reshape(
            height // size, size, width // size, size).mean(axis=(1, 3)) * 100.0

    over = numpy.minimum(tiles(picture, before), tiles(picture, after)) - 2.0 * tiles(before, after)
    y, x = numpy.unravel_index(over.argmax(), over.shape)
    return int((over > 1.0).sum()), (int(x) * size, int(y) * size)


def exceeds(change, noise):
    """Whether a difference stands clear of the difference between two unchanged pictures."""
    return (change[0] > 2.0 * noise[0] + 0.05 or change[1] > 2.0 * noise[1] + 0.3
            or change[2] > 3.0 * noise[2] + 0.02)


def report(out, plan, uncovered, screenshots):
    log = smoke.ANSI.sub("", (out / "client.log").read_text(encoding="utf-8", errors="replace"))
    # What the client said of each change: built anew, handed over live, or not a change at all.
    applied = {}
    for match in re.finditer(r"\[dev\] pipeline (\S+) rebuilt (true|false)( \(runtime\))?", log):
        applied[match.group(1)] = "rebuilt" if match.group(2) == "true" else "live" if match.group(3) else "no change"
    errors = [line.strip()[:200] for line in log.splitlines()
              if ("/ERROR]" in line or "failed to compile" in line) and not any(w in line for w in smoke.HARMLESS)]

    lines = [f"# Settings sweep, {out.name}", "",
             "Each setting's picture is compared with the unchanged picture taken before it and the one taken after "
             "it; the smaller difference counts. The drift is how much those two unchanged pictures differ from each "
             "other: noise, or a change that had not died down yet. Tiles counts the 32 pixel squares that changed "
             "clearly more than they drifted, which is what shows a change in one small place.", ""]
    summary = []
    for view, pictures in plan:
        loaded = {}

        def load(name):
            if name not in loaded:
                source = screenshots / f"sweep_{name}.png"
                if source.is_file():
                    shutil.move(str(source), out / f"{name}.png")
                loaded[name] = (smoke.picture_stats(out / f"{name}.png")[0]
                                if (out / f"{name}.png").is_file() else None)
            return loaded[name]

        drifts = [d for d in (compare(load(before), load(after)) for _, _, before, after in pictures) if d]
        # The view's own noise: the middle one of its drifts, which a few slow changes do not move.
        usual = tuple(sorted(d[i] for d in drifts)[len(drifts) // 2] for i in range(3)) if drifts else (0.0, 0.0, 0.0)
        lines += [f"## {view['name']}", "", view.get("what", ""), "",
                  f"Usual drift: {usual[0]:.3f} % mean, {usual[1]:.2f} % of pixels moved, {usual[2]:.3f} % moved a "
                  "lot.", "", "| Setting | Change | Applied | Mean diff | Pixels moved | Moved a lot | Tiles | Drift | Verdict |",
                  "|---|---|---|---|---|---|---|---|---|"]
        print(f"\n{view['name']}  (usual drift {usual[0]:.3f} % / {usual[1]:.2f} % / {usual[2]:.3f} %)")
        for test, name, before, after in pictures:
            how = applied.get(test["set"][-1].split("=", 1)[1], "") if test["set"][-1].startswith("pipe=") else "live"
            sides = [d for d in (compare(load(name), load(before)), compare(load(name), load(after))) if d]
            drift = compare(load(before), load(after))
            change = tuple(min(side[i] for side in sides) for i in range(3)) if sides else (0.0, 0.0, 0.0)
            tiles, where = local_change(load(name), load(before), load(after))
            if not sides:
                verdict = "no picture"
            else:
                noise = drift or usual
                # A change that swamps the picture works even when the picture had not come back from it by the
                # time the next unchanged one was taken (the exposure takes seconds to return).
                works = exceeds(change, noise) or (exceeds(sides[0], usual) and sides[0][0] > 4.0)
                slow = drift is not None and exceeds(drift, usual) and drift[0] > 2.0 * usual[0] + 0.3
                if how == "no change":
                    verdict = "NOT APPLIED"
                elif test["expect"] == "none":
                    verdict = ("nothing, as expected" if not works and tiles < 12
                               else "CHANGES (expected nothing)")
                else:
                    # A dozen tiles is a real change in one place; a few may be one, or something that moves.
                    verdict = ("works" if works or tiles >= 12 else "unsure (the view was still changing)" if slow
                               else f"look: {tiles} tiles at {where}" if tiles >= 3 else "NOTHING")
            summary.append({"view": view["name"], "setting": test["name"], "change": test["what"], "applied": how,
                            "mean": change[0], "share": change[1], "strong": change[2],
                            "drift": drift[0] if drift else None, "tiles": tiles, "where": where, "verdict": verdict, "why": test["why"]})
            drift_text = f"{drift[0]:.3f} %" if drift else "-"
            lines.append(f"| {test['name']} | {test['what']} | {how} | {change[0]:.3f} % | {change[1]:.2f} % | "
                         f"{change[2]:.3f} % | {tiles} | {drift_text} | {verdict}"
                         f"{' (' + test['why'] + ')' if test['why'] else ''} |")
            print(f"  {verdict:<22} {test['name']:<46} {change[0]:7.3f} % {change[1]:7.2f} % {change[2]:7.3f} % {tiles:5d} tiles  "
                  f"drift {drift_text:<9} {how:<9} {test['what']}")
        lines.append("")
    if uncovered:
        lines += ["## Shader pack attributes no view covers", "", ", ".join(uncovered), ""]
        print("\npack attributes in no view: " + ", ".join(uncovered))
    if errors:
        lines += ["## Errors in the log", ""] + [f"- `{e}`" for e in errors[:40]] + [""]
        print(f"\n{len(errors)} error line(s) in the log, e.g. {errors[0]}")
    (out / "report.md").write_text("\n".join(lines), encoding="utf-8")
    (out / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    print(f"\nreport: {out / 'report.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
