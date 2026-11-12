#!/usr/bin/env python3
"""Compiles every example shader pack on top of Radiante PT, without the game.

    python native/src/tools/check_example_packs.py --stage <shader_pack_stage> [--glslang <glslangValidator>]
                                                   [--jobs <n>] [--only <text>]

The packs in examples/shaderpacks hold only the hook files they change, so a hook whose signature changes in
radiante-pt, or a file one of them includes that moves, breaks them without breaking radiante-pt itself; one of them
ships inside the mod. For each pack this stages radiante-pt as the native build does (check_shaders.py), puts the pack
over it exactly as the loader does (shader_pack_stage, a target of the native build) and compiles all of it at the
pack's default settings (compile_pack.py).

--stage is the shader_pack_stage program: build it with

    cmake --build <build dir> --config Release --target shader_pack_stage

and find it in <build dir>/src/tools (under Release/ with Visual Studio). Exit status 0 when every pack compiles; the
CI runs it, and so should anyone who changes a hook, a file in radiante-pt's common/ or an example.
"""

import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import json

import check_shaders

TOOLS = Path(__file__).resolve().parent
EXAMPLES = TOOLS.parents[2] / "examples" / "shaderpacks"
BASE_CONFIG = TOOLS.parent / "shader" / "world" / "ray_tracing" / "internal" / "radiante-pt" / "configs.json"
# The binding numbers a pack's own textures and buffers go on (docs/dev/shader-packs.md); the rest are Radiante PT's.
PACK_BINDINGS = range(64, 120)


def bindings(config):
    """Every runtime binding a configs.json claims, as (number, what claims it)."""
    claimed = []
    for texture in config.get("textures", []):
        for key in ("sampled_binding", "storage_binding"):
            if key in texture:
                claimed.append((int(texture[key]), f"texture {texture.get('name', '?')} ({key})"))
    for buffer in config.get("buffers", []):
        if "binding" in buffer:
            claimed.append((int(buffer["binding"]), f"buffer {buffer.get('name', '?')}"))
    return claimed


def binding_problems(pack):
    """What the loader would refuse the pack for (two resources on one binding), and bindings outside its range.

    Compiling does not find these: the shaders compile, and the pack then fails to load in the game. That is how the
    Mellow port broke when Radiante PT took bindings 48 to 50, with this check green.
    """
    base = json.loads(BASE_CONFIG.read_text(encoding="utf-8"))
    own = json.loads((pack / "configs.json").read_text(encoding="utf-8"))
    owners = {}
    for number, owner in bindings(base):
        owners.setdefault(number, "Radiante PT's " + owner)
    problems = []
    for number, owner in bindings(own):
        if number in owners:
            problems.append(f"binding {number}: {owner} and {owners[number]}")
        elif number not in PACK_BINDINGS:
            problems.append(f"binding {number}: {owner} is outside {PACK_BINDINGS.start} to {PACK_BINDINGS.stop - 1}")
        owners[number] = owner
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--stage", required=True, help="the shader_pack_stage program of the native build")
    parser.add_argument("--glslang", default=shutil.which("glslangValidator") or "glslangValidator")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--only")
    args = parser.parse_args()
    if not shutil.which(args.glslang) and not Path(args.glslang).exists():
        sys.exit("glslangValidator was not found: install the Vulkan SDK, or pass --glslang <path>")
    if not Path(args.stage).is_file():
        sys.exit(f"{args.stage} was not found: build the shader_pack_stage target of the native build")

    packs = sorted(folder for folder in EXAMPLES.iterdir() if (folder / "configs.json").is_file())
    packs = [pack for pack in packs if not args.only or args.only in pack.name]
    if not packs:
        sys.exit(f"no example pack found in {EXAMPLES}")
    failed = []
    with tempfile.TemporaryDirectory() as temp:
        def run(pack):
            problems = binding_problems(pack)
            if problems:
                return pack.name, 1, "\n".join(problems) + "\nbindings clash"
            base = Path(temp) / f"{pack.name}-base"
            staged = Path(temp) / f"{pack.name}-staged"
            check_shaders.stage(base)
            merged = subprocess.run([args.stage, str(base), str(pack), str(staged)], capture_output=True, text=True)
            if merged.returncode != 0:
                return pack.name, merged.returncode, (merged.stdout + merged.stderr).strip() + "\nnot staged"
            # The pack's own files went in as they are: name the public headers in quotes there too.
            check_shaders.quote_public_includes(staged)
            result = subprocess.run([sys.executable, str(TOOLS / "compile_pack.py"), str(staged), "--quiet",
                                     "--glslang", args.glslang], capture_output=True, text=True)
            shutil.rmtree(base, ignore_errors=True)
            shutil.rmtree(staged, ignore_errors=True)
            return pack.name, result.returncode, (result.stdout + result.stderr).strip()

        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            for name, status, output in pool.map(run, packs):
                last = output.splitlines()[-1] if output else ""
                print(f"{'ok  ' if status == 0 else 'FAIL'} {name}: {last}", flush=True)
                if status != 0:
                    failed.append(name)
                    print("\n".join("     " + line for line in output.splitlines()[:-1][:40]), flush=True)
    print(f"{len(packs) - len(failed)} of {len(packs)} example packs compile")
    if failed:
        print("failed: " + ", ".join(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
