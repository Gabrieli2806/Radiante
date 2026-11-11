#!/usr/bin/env python3
"""Compiles Radiante PT under every setting that changes its shaders, without the game.

    python native/src/tools/check_shaders.py [--glslang <glslangValidator>] [--jobs <n>] [--only <text>]
                                             [--out <folder>]

The path tracer is compiled in game, with the player's settings as defines, so a shader that only breaks with one
setting off the default is not caught by building the mod. This stages the pack as the native build does (the pack,
util/, common/ and SHaRC's headers), then compiles all of it (compile_pack.py) once at the defaults and once for each
compiled-in setting moved to each of its other values, and once for each debug view. A setting marked "runtime" is a
uniform read and needs no build of its own.

It needs Python 3 and glslangValidator, which the Vulkan SDK brings. Exit status 0 when every build compiles; the CI
runs it, and so should anyone who changes a shader:

    python native/src/tools/check_shaders.py

--only keeps the builds whose name contains the text (--only restir). --out keeps the SPIR-V of every build, one
folder a build: after a change that should not alter what the shaders do (moving code between files), two such folders
must hold the same bytes.
"""

import argparse
import concurrent.futures
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
SOURCE = TOOLS.parent  # native/src
PACK = SOURCE / "shader" / "world" / "ray_tracing" / "internal" / "radiante-pt"


def stage(folder):
    """The pack as the install step of native/src/shader/CMakeLists.txt lays it out."""
    shutil.copytree(PACK, folder)
    shutil.copytree(SOURCE / "shader" / "util", folder / "util")
    shutil.copytree(SOURCE / "common", folder / "common", dirs_exist_ok=True)
    shutil.copytree(SOURCE.parent / "extern" / "sharc" / "include", folder / "extern" / "sharc" / "include")


def builds(config):
    """(name, arguments for compile_pack.py) for the defaults and for each setting on each other value."""
    found = [("defaults", [])]
    for attribute in config.get("attributes", []):
        if attribute.get("runtime") or attribute.get("define") is None:
            continue
        name = attribute.get("name", "").rsplit(".", 1)[-1]
        kind = attribute.get("type", "")
        default = str(attribute.get("default_value", ""))
        if kind == "bool":
            other = "false" if default.rsplit(".", 1)[-1] == "true" else "true"
            found.append((f"{name}={other}", ["--set", f"{name}={other}"]))
        elif kind.startswith("enum:"):
            for choice in kind[len("enum:"):].split("-"):
                if choice and choice != default:
                    short = choice.rsplit(".", 1)[-1]
                    found.append((f"{name}={short}", ["--set", f"{name}={choice}"]))
    for view in config.get("debug_views", []):
        if isinstance(view, dict) and view.get("name"):
            found.append((f"debug view {view['name']}", ["--debug-view", view["name"]]))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--glslang", default=shutil.which("glslangValidator") or "glslangValidator")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--only")
    parser.add_argument("--out")
    args = parser.parse_args()
    if not shutil.which(args.glslang) and not Path(args.glslang).exists():
        sys.exit("glslangValidator was not found: install the Vulkan SDK, or pass --glslang <path>")
    if not (SOURCE.parent / "extern" / "sharc" / "include").is_dir():
        sys.exit("native/extern/sharc/include is missing from this checkout")

    config = json.loads((PACK / "configs.json").read_text(encoding="utf-8"))
    todo = [build for build in builds(config) if not args.only or args.only in build[0]]
    failed = []
    with tempfile.TemporaryDirectory() as temp:
        def run(build):
            # A copy of its own: compile_pack.py writes its temporary sources next to the shaders.
            folder = Path(temp) / f"pack{todo.index(build)}"
            stage(folder)
            keep = ["--out", str(Path(args.out) / build[0].replace(" ", "_").replace("=", "-"))] if args.out else []
            result = subprocess.run([sys.executable, str(TOOLS / "compile_pack.py"), str(folder), "--quiet",
                                     "--glslang", args.glslang, *build[1], *keep], capture_output=True, text=True)
            shutil.rmtree(folder, ignore_errors=True)
            return build[0], result.returncode, (result.stdout + result.stderr).strip()

        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            for name, status, output in pool.map(run, todo):
                last = output.splitlines()[-1] if output else ""
                print(f"{'ok  ' if status == 0 else 'FAIL'} {name}: {last}", flush=True)
                if status != 0:
                    failed.append(name)
                    print("\n".join("     " + line for line in output.splitlines()[:-1][:40]), flush=True)
    print(f"{len(todo) - len(failed)} of {len(todo)} builds compile")
    if failed:
        print("failed: " + ", ".join(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
