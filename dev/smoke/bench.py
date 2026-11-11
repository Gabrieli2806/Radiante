#!/usr/bin/env python3
"""Measures what settings cost on the GPU, from fixed views, without the game's frame limiter in the way.

    python dev/smoke/bench.py [--bench <file.json>] [--out <folder>] [--loader fabric|neoforge|forge]

It walks a development client through the views of a bench file (default bench.json): at each view it applies each
variant's actions, waits for the picture to settle, and reads the renderer's own GPU time per pass (the numbers of
the F3 screen, averaged over 60 frames), then puts the settings back with the variant's "undo" actions. GPU times do
not move with the frame limiter or with what the CPU is doing, so two variants can be compared from one run; compare
within one run only, since the scene's lights differ a little between launches. Each view's variants are gone through
--repeat times (3) and the lowest time of each counts: another program using the GPU only adds time.

It prints a table per view (total and world pass in milliseconds, and the change against the first variant) and
writes it with a picture of every variant to <out>/bench.md, so what a cheaper setting does to the picture can be
seen next to what it saves. The development settings are backed up and put back, as smoke.py does.
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


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bench", default=str(HERE / "bench.json"))
    parser.add_argument("--out")
    parser.add_argument("--loader", default="fabric", choices=("fabric", "neoforge", "forge"))
    parser.add_argument("--repeat", type=int, default=3,
                        help="how many times each view's variants are gone through; the lowest time counts")
    parser.add_argument("--timeout", type=int, default=3000)
    args = parser.parse_args()

    config = json.loads(Path(args.bench).read_text(encoding="utf-8"))
    steps = []
    tick = 100
    for action in config.get("setup", []):
        steps.append((tick, action))
        tick += 10
    tick += config.get("setup_ticks", 300)
    settle = config.get("settle_ticks", 160)
    names = []
    for view in config["views"]:
        for action in view["actions"]:
            steps.append((tick, action))
            tick += 4
        tick += settle
        for variant, turn in [(v, t) for t in range(args.repeat) for v in config["variants"]]:
            name = f"{view['name']}__{variant['name']}"
            first = turn == 0
            for action in variant.get("actions", []):
                steps.append((tick, action))
                tick += 4
            tick += variant.get("settle_ticks", settle)
            steps.append((tick, f"fps={name}"))
            if first:
                steps.append((tick + 1, f"shot=bench_{name}"))
            tick += 6
            for action in variant.get("undo", []):
                steps.append((tick, action))
                tick += 4
            if first:
                names.append((view["name"], variant["name"], name))
    steps.append((tick + 20, "quit"))

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out = Path(args.out) if args.out else REPO / "build" / "bench" / stamp
    out.mkdir(parents=True, exist_ok=True)
    folders = [REPO / "run-shared", REPO / f"run-{args.loader}"]
    backup = out / "settings-backup"
    smoke.backup_settings(folders, backup)
    screenshots = REPO / "screenshots"
    for stale in screenshots.glob("bench_*.png"):
        stale.unlink()

    environment = dict(os.environ)
    environment["RADIANTE_DEV_PRESCRIPT"] = f"100:testworld={config['world']}"
    environment["RADIANTE_DEV_SCRIPT"] = ";".join(f"{at}:{action}" for at, action in steps)
    gradlew = REPO / ("gradlew.bat" if os.name == "nt" else "gradlew")
    log_path = out / "client.log"
    print(f"starting the {args.loader} client: {len(names)} measurements, log in {log_path}", flush=True)
    try:
        with open(log_path, "w", encoding="utf-8", errors="replace") as log:
            subprocess.run([str(gradlew), f":{args.loader}:runClient"], cwd=REPO, env=environment, stdout=log,
                           stderr=subprocess.STDOUT, timeout=args.timeout)
    finally:
        smoke.restore_settings(folders, backup)

    text = smoke.ANSI.sub("", log_path.read_text(encoding="utf-8", errors="replace"))
    measured = {}
    for match in re.finditer(r"\[dev\] fps (\S+) \d+ presented \S+ gpu ([\d.]+) ms \((.*?)\)", text):
        passes = dict(re.findall(r"(\w+) ([\d.]+)", match.group(3)))
        # Another program on the GPU only ever adds time, so of a variant's turns the lowest is the nearest to its
        # own cost.
        found = (float(match.group(2)), {k: float(v) for k, v in passes.items()})
        if match.group(1) not in measured or found[0] < measured[match.group(1)][0]:
            measured[match.group(1)] = found

    lines = [f"# GPU cost of settings, {stamp}", ""]
    for view in config["views"]:
        lines += [f"## {view['name']}", "", "| Variant | Total ms | World ms | Change in total | Picture |",
                  "|---|---|---|---|---|"]
        base = None
        print(f"\n{view['name']}")
        for view_name, variant_name, name in names:
            if view_name != view["name"]:
                continue
            picture = screenshots / f"bench_{name}.png"
            if picture.is_file():
                shutil.move(str(picture), out / f"{name}.png")
            if name not in measured:
                lines.append(f"| {variant_name} | not measured | | | |")
                print(f"  {variant_name:<28} not measured")
                continue
            total, passes = measured[name]
            if base is None:
                base = total
            change = (total - base) / base * 100.0
            lines.append(f"| {variant_name} | {total:.2f} | {passes.get('world', 0.0):.2f} | {change:+.1f}% | "
                         f"![{variant_name}]({name}.png) |")
            print(f"  {variant_name:<28} {total:6.2f} ms  world {passes.get('world', 0.0):6.2f}  {change:+6.1f}%")
        lines.append("")
    (out / "bench.md").write_text("\n".join(lines), encoding="utf-8")
    print(f"\nreport: {out / 'bench.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
