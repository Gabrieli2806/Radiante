#!/usr/bin/env python3
"""Radiante's visual smoke test: starts a development client, walks it through the scenes in scenes.json and checks
what it drew.

    python dev/smoke/smoke.py [--out <folder>] [--loader fabric|neoforge|forge] [--only <text>] [--golden <folder>]
                              [--keep-settings]

Run it before a release and after a Minecraft version bump. It needs the native renderer built (or downloaded) into
common/src/main/resources/radiante-native, a GPU that can ray trace, and Python with Pillow and numpy.

What it does:

  1. Backs up the development settings (run-shared and the loader's folder), which the run changes, and puts them
     back afterwards.
  2. Builds one script for DevAutomation (RADIANTE_DEV_PRESCRIPT / RADIANTE_DEV_SCRIPT) from scenes.json: opens the
     test world, builds the showcase gallery, then for each scene runs its actions, waits for the picture to settle
     and takes a screenshot; for each toggle takes one with the feature off and one with it on, from the same view.
  3. Starts ./gradlew :<loader>:runClient with it and waits for the client to quit.
  4. Checks, and exits with status 1 when one fails:
       - the renderer started, and no shader failed to compile;
       - no error from the renderer in the log;
       - every screenshot was taken, and none is black or one flat colour;
       - each toggle's two pictures differ.
     With --golden, each picture is also compared with the one of the same name in that folder (kept per GPU vendor,
     since the vendors' denoisers and upscalers differ); a difference is reported, not failed.
  5. Writes report.html next to the screenshots, with every picture, what to look at in it and the frame rate there.

Scenes are data: add a regression that was fixed as a scene in scenes.json, with the commands that set it up and a
line saying what to check.
"""

import argparse
import datetime
import html
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]

# The files a run changes (docs/dev/testing.md, "Settings files").
SETTINGS = ["options.txt", "radiante/options.properties", "radiante/pipeline.yaml"]
PACK_SETTINGS = "radiante/shaders/world/ray_tracing"

ANSI = re.compile(r"\x1b\[[0-9;]*m")
# Lines the game or the loader write as errors without anything being wrong with the renderer.
HARMLESS = ("Realms", "Block-attached entity", "FabricLoader/Mixin", "Failed to parse into SignedJWT",
            "Error loading class", "sessionserver.mojang.com")


def build_script(config, only):
    """The steps as 'tick:action', and what each screenshot is: (name, kind, scene)."""
    steps = []
    shots = []
    tick = 100
    for action in config.get("setup", []):
        steps.append((tick, action))
        tick += 10
    tick += config.get("setup_ticks", 300)
    settle = config.get("settle_ticks", 70)

    def shot(name, kind, scene, wait):
        nonlocal tick
        tick += wait
        steps.append((tick, f"fps={name}"))
        steps.append((tick + 1, f"shot=smoke_{name}"))
        shots.append((name, kind, scene))
        tick += 6

    for scene in config.get("scenes", []):
        if only and only not in scene["name"]:
            continue
        for action in scene.get("actions", []):
            steps.append((tick, action))
            tick += 4
        shot(scene["name"], "scene", scene, scene.get("settle_ticks", settle))
        for action in scene.get("after", []):
            steps.append((tick, action))
            tick += 4
    for toggle in config.get("toggles", []):
        if only and only not in toggle["name"]:
            continue
        for action in toggle.get("view", []):
            steps.append((tick, action))
            tick += 4
        for state in ("off", "on"):
            for action in toggle[state]:
                steps.append((tick, action))
                tick += 4
            shot(f"{toggle['name']}_{state}", state, toggle, toggle.get("settle_ticks", settle))
    steps.append((tick + 20, "quit"))
    return ";".join(f"{at}:{action}" for at, action in steps), shots


def backup_settings(folders, into):
    for folder in folders:
        target = into / folder.name
        for name in SETTINGS:
            source = folder / name
            if source.is_file():
                (target / name).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target / name)
        packs = folder / PACK_SETTINGS
        if packs.is_dir():
            for source in packs.glob("*.txt"):
                (target / PACK_SETTINGS).mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target / PACK_SETTINGS / source.name)


def restore_settings(folders, backup):
    for folder in folders:
        source_root = backup / folder.name
        if not source_root.is_dir():
            continue
        for source in source_root.rglob("*"):
            if source.is_file():
                target = folder / source.relative_to(source_root)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)


def picture_stats(path):
    import numpy
    from PIL import Image
    with Image.open(path) as image:
        pixels = numpy.asarray(image.convert("RGB"), dtype=numpy.float32) / 255.0
    luma = pixels @ numpy.array([0.2126, 0.7152, 0.0722], dtype=numpy.float32)
    return pixels, float(luma.mean()), float(luma.std())


def difference(a, b):
    """The mean difference of two pictures of the same size, in percent of full scale."""
    import numpy
    if a.shape != b.shape:
        return 100.0
    return float(numpy.abs(a - b).mean() * 100.0)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", help="where the screenshots and the report go (default: build/smoke/<time>)")
    parser.add_argument("--loader", default="fabric", choices=("fabric", "neoforge", "forge"))
    parser.add_argument("--only", help="only the scenes and toggles whose name contains this")
    parser.add_argument("--golden", help="a folder of pictures from an earlier run to compare with")
    parser.add_argument("--keep-settings", action="store_true",
                        help="leave the development settings as the run left them")
    parser.add_argument("--timeout", type=int, default=1500, help="seconds before the client is given up on")
    args = parser.parse_args()

    try:
        import numpy  # noqa: F401
        from PIL import Image  # noqa: F401
    except ImportError:
        sys.exit("the smoke test needs Pillow and numpy: python -m pip install pillow numpy")

    config = json.loads((HERE / "scenes.json").read_text(encoding="utf-8"))
    script, shots = build_script(config, args.only)
    if not shots:
        sys.exit("no scene or toggle matches")
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out = Path(args.out) if args.out else REPO / "build" / "smoke" / stamp
    out.mkdir(parents=True, exist_ok=True)

    run_folder = REPO / f"run-{args.loader}"
    folders = [REPO / "run-shared", run_folder]
    backup = out / "settings-backup"
    backup_settings(folders, backup)
    screenshots = REPO / "screenshots"
    for stale in screenshots.glob("smoke_*.png"):
        stale.unlink()

    environment = dict(os.environ)
    environment["RADIANTE_DEV_PRESCRIPT"] = f"100:testworld={config['world']}"
    environment["RADIANTE_DEV_SCRIPT"] = script
    gradlew = REPO / ("gradlew.bat" if os.name == "nt" else "gradlew")
    log_path = out / "client.log"
    print(f"starting the {args.loader} client: {len(shots)} pictures, log in {log_path}", flush=True)
    timed_out = False
    try:
        with open(log_path, "w", encoding="utf-8", errors="replace") as log:
            try:
                status = subprocess.run([str(gradlew), f":{args.loader}:runClient"], cwd=REPO, env=environment,
                                        stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout).returncode
            except subprocess.TimeoutExpired:
                status = -1
                timed_out = True
    finally:
        if not args.keep_settings:
            restore_settings(folders, backup)

    log_text = ANSI.sub("", log_path.read_text(encoding="utf-8", errors="replace"))
    lines = log_text.splitlines()
    failures = []
    notes = []
    if timed_out:
        failures.append(f"the client did not quit within {args.timeout} s")
    elif status != 0:
        failures.append(f"the client ended with status {status}")
    started = [line for line in lines if "Radiante renderer initialised" in line]
    if started:
        notes.append(started[0].split(") ", 1)[-1])
    else:
        failures.append("the renderer did not start (no 'Radiante renderer initialised' in the log)")
    for line in lines:
        if "failed to compile" in line or "UnsatisfiedLinkError" in line:
            failures.append("log: " + line.strip()[:220])
        elif ("/ERROR]" in line or "Unreported exception" in line) and not any(word in line for word in HARMLESS):
            failures.append("log: " + line.strip()[:220])
    rates = {}
    for line in lines:
        match = re.search(r"\[dev\] fps (\S+) (\d+) presented \S+ gpu (.*)", line)
        if match:
            rates[match.group(1)] = (int(match.group(2)), match.group(3).strip())

    rows = []
    pixels_of = {}
    for name, kind, scene in shots:
        source = screenshots / f"smoke_{name}.png"
        row = {"name": name, "what": scene.get("what", ""), "problems": [], "file": None,
               "fps": rates.get(name, (None, ""))[0], "gpu": rates.get(name, (None, ""))[1]}
        if not source.is_file():
            row["problems"].append("no screenshot was taken")
        else:
            target = out / f"{name}.png"
            shutil.move(str(source), target)
            row["file"] = target.name
            pixels, mean, spread = picture_stats(target)
            pixels_of[name] = pixels
            row["mean"] = mean
            if mean < 0.01:
                row["problems"].append(f"the picture is black (mean brightness {mean:.4f})")
            elif spread < 0.004:
                row["problems"].append(f"the picture is one flat colour (spread {spread:.4f})")
            if args.golden:
                golden = Path(args.golden) / f"{name}.png"
                if golden.is_file():
                    row["golden"] = difference(pixels, picture_stats(golden)[0])
        rows.append(row)
    for toggle in config.get("toggles", []):
        off, on = pixels_of.get(f"{toggle['name']}_off"), pixels_of.get(f"{toggle['name']}_on")
        if off is None or on is None:
            continue
        changed = difference(off, on)
        least = toggle.get("min_difference", 0.5)
        for row in rows:
            if row["name"] == f"{toggle['name']}_on":
                row["toggle"] = changed
                if changed < least:
                    row["problems"].append(
                        f"on and off differ by {changed:.2f}% only (at least {least}% expected): the feature draws "
                        "nothing here")
    for row in rows:
        failures.extend(f"{row['name']}: {problem}" for problem in row["problems"])

    report = ["<!doctype html><meta charset='utf-8'><title>Radiante smoke test</title>",
              "<style>body{font-family:sans-serif;background:#111;color:#ddd;margin:24px}"
              "img{max-width:640px;display:block;margin:6px 0}.bad{color:#ff8080}.ok{color:#8fe3c8}"
              "section{margin-bottom:28px}code{color:#b0b8b4}</style>",
              f"<h1>Radiante smoke test, {html.escape(stamp)}</h1>",
              f"<p class='{'bad' if failures else 'ok'}'>{len(failures)} problem(s), {len(rows)} pictures</p>"]
    report += [f"<p><code>{html.escape(note)}</code></p>" for note in notes]
    if failures:
        report.append("<ul class='bad'>" + "".join(f"<li>{html.escape(f)}</li>" for f in failures) + "</ul>")
    for row in rows:
        report.append(f"<section><h2>{html.escape(row['name'])}</h2>")
        if row["what"]:
            report.append(f"<p>{html.escape(row['what'])}</p>")
        details = []
        if row["fps"] is not None:
            details.append(f"{row['fps']} fps")
        if row.get("toggle") is not None:
            details.append(f"differs from off by {row['toggle']:.2f}%")
        if row.get("golden") is not None:
            details.append(f"differs from the golden picture by {row['golden']:.2f}%")
        if details:
            report.append(f"<p><code>{html.escape(', '.join(details))}</code></p>")
        if row["gpu"]:
            report.append(f"<p><code>{html.escape(row['gpu'])}</code></p>")
        report += [f"<p class='bad'>{html.escape(problem)}</p>" for problem in row["problems"]]
        if row["file"]:
            report.append(f"<img src='{html.escape(row['file'])}' alt='{html.escape(row['name'])}'>")
        report.append("</section>")
    (out / "report.html").write_text("\n".join(report), encoding="utf-8")
    summary = {"failures": failures, "pictures": [{k: v for k, v in row.items() if k != "problems"} for row in rows]}
    (out / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")

    for row in rows:
        state = "FAIL" if row["problems"] else "ok  "
        rate = f"{row['fps']} fps" if row["fps"] is not None else ""
        print(f"{state} {row['name']:<22} {rate}")
    print(f"report: {out / 'report.html'}")
    if failures:
        print(f"{len(failures)} problem(s):")
        for failure in failures:
            print("  " + failure)
        return 1
    print("every check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
