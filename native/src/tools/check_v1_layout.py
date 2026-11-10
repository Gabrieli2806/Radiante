"""Checks that radiante/v1/uniforms.glsl lays its structs out as common/shared.hpp lays out the engine's WorldUBO and
SkyUBO: the same type at the same offset, member by member, std140 as the engine binds them. A v1 struct may stop
short of the engine's, since members are only ever added at the end.

    python check_v1_layout.py [--glslang glslangValidator] [--spirv-dis spirv-dis] [--stamp <file>]

It compiles a small compute shader holding both versions of each block and compares what spirv-dis reports for them:
each member's type, Offset, MatrixStride and matrix order. The native build runs it (native/src/shader/CMakeLists.txt)
whenever Python 3 and spirv-dis are found, so a member added to WorldUBO or SkyUBO without its twin in v1, or the
other way round, stops the build."""
import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1]  # native/src
INCLUDE = SOURCE / "shader" / "include"
PAIRS = [("WorldUBO", "RadianteWorld"), ("SkyUBO", "RadianteSky")]
PROBE = """#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/shared.hpp"
#include "radiante/v1/uniforms.glsl"
layout(local_size_x = 1) in;
layout(std140, set = 0, binding = 0) uniform EngineWorld { WorldUBO engineWorld; };
layout(std140, set = 0, binding = 1) uniform PackWorld { RadianteWorld packWorld; };
layout(std140, set = 0, binding = 2) uniform EngineSky { SkyUBO engineSky; };
layout(std140, set = 0, binding = 3) uniform PackSky { RadianteSky packSky; };
layout(std430, set = 0, binding = 4) buffer Sink { float sink; };
void main() {
    sink = engineWorld.windTime + packWorld.windTime + engineSky.rainGradient + packSky.rainLevel;
}
"""


def layouts(disassembly):
    """Each struct type's members as (type, offset, matrix stride, matrix order). Types are spirv-dis' names, except
    arrays, which glslang may declare twice for one shape: those read as their element, length and stride."""
    members, decorations, arrays, strides, constants = {}, {}, {}, {}, {}
    for line in disassembly.splitlines():
        line = line.strip()
        if m := re.match(r"%(\S+) = OpTypeStruct ?(.*)$", line):
            members[m.group(1)] = m.group(2).split()
        elif m := re.match(r"%(\S+) = OpTypeArray %(\S+) %(\S+)$", line):
            arrays[m.group(1)] = (m.group(2), m.group(3))
        elif m := re.match(r"%(\S+) = OpConstant %\S+ (\S+)$", line):
            constants[m.group(1)] = m.group(2)
        elif m := re.match(r"OpDecorate %(\S+) ArrayStride (\d+)$", line):
            strides[m.group(1)] = int(m.group(2))
        elif m := re.match(r"OpMemberDecorate %(\S+) (\d+) (Offset|MatrixStride) (\d+)$", line):
            decorations.setdefault((m.group(1), int(m.group(2))), {})[m.group(3)] = int(m.group(4))
        elif m := re.match(r"OpMemberDecorate %(\S+) (\d+) (ColMajor|RowMajor)$", line):
            decorations.setdefault((m.group(1), int(m.group(2))), {})["Order"] = m.group(3)

    def shape(kind):
        name = kind.lstrip("%")
        if name not in arrays:
            return kind
        element, length = arrays[name]
        return f"{shape('%' + element)}[{constants.get(length, length)}] stride {strides.get(name)}"

    return {name: [(shape(kind), *(decorations.get((name, i), {}).get(key) for key in ("Offset", "MatrixStride", "Order")))
                   for i, kind in enumerate(types)] for name, types in members.items()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--glslang", default=shutil.which("glslangValidator") or "glslangValidator")
    parser.add_argument("--spirv-dis", default=shutil.which("spirv-dis") or "spirv-dis")
    parser.add_argument("--stamp")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temp:
        probe, spv = Path(temp) / "v1_layout.comp", Path(temp) / "v1_layout.spv"
        probe.write_text(PROBE, encoding="utf-8")
        compiled = subprocess.run([args.glslang, "-V", "--target-env", "vulkan1.4", f"-I{SOURCE}", f"-I{INCLUDE}",
                                   str(probe), "-o", str(spv)], capture_output=True, text=True)
        if compiled.returncode != 0:
            print(compiled.stdout + compiled.stderr)
            sys.exit("check_v1_layout: the probe shader does not compile")
        disassembly = subprocess.run([args.spirv_dis, str(spv)], capture_output=True, text=True, check=True).stdout

    found, problems = layouts(disassembly), []
    for engine, v1 in PAIRS:
        if engine not in found or v1 not in found:
            sys.exit(f"check_v1_layout: {engine} or {v1} is missing from the probe's SPIR-V")
        theirs, ours, before = found[engine], found[v1], len(problems)
        if len(ours) > len(theirs):
            problems.append(f"{v1} has {len(ours)} members, {engine} only {len(theirs)}")
        for index, (mine, engines) in enumerate(zip(ours, theirs)):
            if mine != engines:
                problems.append(f"{v1} member {index}: {mine} where {engine} has {engines}")
        print(f"check_v1_layout: {v1} lays out {len(ours)} of the {len(theirs)} members of {engine} "
              f"{'the same way' if len(problems) == before else 'differently'}")
    if problems:
        print("\n".join(problems))
        sys.exit(1)
    if args.stamp:
        Path(args.stamp).write_text("radiante/v1/uniforms.glsl matches common/shared.hpp\n", encoding="utf-8")


if __name__ == "__main__":
    main()
