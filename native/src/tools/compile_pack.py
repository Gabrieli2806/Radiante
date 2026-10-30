#!/usr/bin/env python3
"""Compiles every shader of a staged shader pack offline, the way the renderer would, without the game.

    python compile_pack.py <pack folder> [--out <folder for .spv>] [--glslang <glslangValidator>] [--opt]
                           [--debug-view <name>]

<pack folder> is a whole pack: radiante-pt as the native build stages it (<build>/src/shader/ray_tracing_internal_zip/
radiante-pt), or a pack that extends it, staged with shader_pack_stage (src/tools/shader_pack_stage.cpp). The defines
are the ones the loader gives a pack at its default settings (shader_pack/attribute_values.cpp): attributes, a
runtime attribute as its worldUBO.packParams read, the debug views' defines (0, or the view's value with
--debug-view), the passes' own defines, the execution variables, and the renderer's (no micromaps, no SER; SHaRC variants of the ray generation shader as the ray tracing module asks for
them). glslangValidator stands in for shaderc: same front end, so a shader that compiles here compiles in game, short
of driver limits. With --opt the SPIR-V also goes through spirv-opt -O --strip-debug (about shaderc's performance
level), so two builds can be compared byte for byte.

Exit status 0 when every shader compiles.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HIT_EXTENSIONS = (".rchit", ".rahit", ".rint", ".rmiss")


def is_true(value):
    return str(value).strip().lower() in ("render_pipeline.true", "true", "1", "yes", "on")


def enum_index(type_, value):
    choices = type_[len("enum:"):].split("-")
    if choices and choices[-1] == "":
        choices.pop()
    value = str(value)
    for candidate in (value, value.rsplit(".", 1)[-1]):
        if candidate in choices:
            return choices.index(candidate)
    return 0


def define_value(type_, value):
    if type_ == "bool":
        return "1" if is_true(value) else "0"
    if type_.startswith("enum:"):
        return str(enum_index(type_, value))
    return str(value)


def substitute(template, value):
    return re.sub(r"(?<![A-Za-z0-9_])X(?![A-Za-z0-9_])", lambda _: value, template)


def runtime_kind(type_):
    if type_ == "vec3":
        return "vec3"
    if type_ == "bool" or type_.startswith("int") or type_.startswith("enum:"):
        return "int"
    return "float"


def debug_view_defines(config, view):
    """As ShaderPackLoader::addDebugViewDefinitions: every view's define 0, the chosen one's its value."""
    defines = {}
    views = [v for v in config.get("debug_views", []) if isinstance(v, dict) and isinstance(v.get("define"), str)]
    for entry in views:
        defines.setdefault(entry["define"], "0")
    found = view is None
    for entry in views:
        if entry.get("name") == view:
            value = entry.get("value", 1)
            defines[entry["define"]] = str(int(value)) if isinstance(value, bool) else str(value)
            found = True
    if not found:
        raise SystemExit(f"no debug view named {view}")
    return defines


def attribute_defines(config):
    defines = {}
    slot = 0
    for attribute in config.get("attributes", []):
        type_ = attribute.get("type", "")
        value = attribute.get("default_value", "")
        define = attribute.get("define")
        if attribute.get("runtime"):
            read = f"worldUBO.packParams[{slot}]"
            slot += 1
            kind = runtime_kind(type_)
            direct = {"vec3": f"vec3({read}.xyz)", "int": f"int({read}.x)"}.get(kind, f"({read}.x)")
            template_value = {"vec3": f"{read}.xyz", "int": f"int({read}.x)"}.get(kind, f"{read}.x")
            if isinstance(define, str):
                defines[define] = direct
            elif isinstance(define, dict):
                for name, text in define.items():
                    if isinstance(text, str):
                        defines[name] = substitute(text, template_value)
            continue
        text_value = define_value(type_, value)
        if isinstance(define, str):
            defines[define] = text_value
        elif isinstance(define, dict):
            for name, text in define.items():
                if isinstance(text, str):
                    defines[name] = substitute(text, text_value)
            variant = define.get(value)
            if isinstance(variant, dict):
                for name, text in variant.items():
                    defines[name] = substitute(text, text_value)
    return defines


def script_of(config, post):
    script = config.get("execution_post" if post else "execution")
    if not isinstance(script, dict):
        return {}
    if not post and isinstance(script.get("ray_tracing"), dict):
        return script["ray_tracing"]
    return script


def execution_source(config, post, execution_set):
    """The execution variables as buildExecutionSource writes them (shader_pack_loader.cpp)."""
    script = script_of(config, post)
    variables = {}

    def type_of(value):
        if isinstance(value, bool):
            return "bool"
        if isinstance(value, int):
            return "int"
        return "float"

    for entry in script.get("global_variables", []):
        for name, value in entry.items():
            variables.setdefault(name, type_of(value))

    def walk(commands):
        for command in commands or []:
            if not isinstance(command, dict):
                continue
            for name, value in command.get("assign", {}).items():
                if isinstance(value, dict):
                    variables.setdefault(name, value.get("type", "float"))
                elif isinstance(value, (bool, int, float)):
                    variables.setdefault(name, type_of(value))
                else:
                    variables.setdefault(name, "float")
            for block in ("if_else", "while"):
                if isinstance(command.get(block), dict):
                    walk(command[block].get("then"))
                    walk(command[block].get("else"))

    walk(script.get("commands"))
    if not variables:
        return ""
    lines = [f"layout(set = {execution_set}, binding = 0, std430) readonly buffer RtExecutionVariables {{"]
    lines += [f"    float {name}_;" for name in variables]
    lines.append("} rtExecutionVariables;")
    for name, type_ in variables.items():
        stored = f"rtExecutionVariables.{name}_"
        if type_ == "bool":
            read = f"({stored} != 0.0)"
        elif type_.startswith("int") or type_.startswith("enum:"):
            read = f"int({stored})"
        else:
            read = stored
        lines.append(f"#define {name} {read}")
    return "\n".join(lines)


def collect(root, config):
    """(shader path, defines, post render?) for every shader the renderer would compile."""
    requests = []
    sharc = config.get("sharc") or {}
    for pass_ in config.get("passes", []):
        type_ = pass_.get("type")
        own = {}
        for key in ("definitions", "defines"):
            if isinstance(pass_.get(key), dict):
                own.update(pass_[key])
        post = str(pass_.get("stage", "ray_tracing")).lower() == "post_render"
        if type_ == "ray_tracing":
            base = dict(own, VPT_OMM_SUPPORTED="0")
            for miss in pass_.get("miss", []):
                requests.append((root / miss["shader"], base, False))
            hit_files = set()
            groups = pass_.get("hit_groups") if isinstance(pass_.get("hit_groups"), dict) else {}
            for group in groups.values():
                for shader in (group.get("shaders") or {}).values():
                    if isinstance(shader, str):
                        hit_files.add(root / shader)
            for path in sorted(root.rglob("*")):
                if path.suffix.lower() in (".rchit", ".rahit", ".rint") and "extern" not in path.parts:
                    hit_files.add(path)
            for path in sorted(hit_files):
                requests.append((path, base, False))
            rgen = dict(base, VPT_SER_SUPPORTED="0")
            query = dict(rgen)
            if pass_.get("query_sharc"):
                query.update(USE_SHARC="1", SHARC_QUERY="1")
            requests.append((root / pass_["rgen"], query, False))
            if sharc.get("update_pass") == pass_.get("name"):
                update = dict(rgen, USE_SHARC="1", SHARC_UPDATE="1")
                requests.append((root / pass_["rgen"], update, False))
                requests.append((root / sharc["resolve_comp"], update, False))
        elif type_ == "compute":
            requests.append((root / pass_["compute"], own, post))
        elif type_ == "full_screen":
            # A pass that draws a cube map is compiled once per face, with FACE set (ray_tracing_module.cpp).
            cubes = {t.get("name") for t in config.get("textures", []) if t.get("dimension") == "cube"}
            if pass_.get("target") in cubes:
                own = dict(own, FACE="0")
            requests.append((root / pass_["fragment"], own, post))
        elif type_ == "render":
            for key in ("vertex", "fragment"):
                if isinstance(pass_.get(key), str):
                    requests.append((root / pass_[key], own, post))
    unique, seen = [], set()
    for path, defines, post in requests:
        key = (str(path), tuple(sorted(defines.items())), post)
        if key not in seen:
            seen.add(key)
            unique.append((path, defines, post))
    return unique


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("pack")
    parser.add_argument("--out")
    parser.add_argument("--glslang", default=shutil.which("glslangValidator") or "glslangValidator")
    parser.add_argument("--opt", action="store_true")
    parser.add_argument("--debug-view")
    args = parser.parse_args()

    root = Path(args.pack).resolve()
    config = json.loads((root / "configs.json").read_text(encoding="utf-8"))
    attributes = attribute_defines(config)
    for name, value in debug_view_defines(config, args.debug_view).items():
        attributes.setdefault(name, value)
        if args.debug_view is not None:
            attributes[name] = value
    sources = {False: execution_source(config, False, 6), True: execution_source(config, True, 5)}
    out = Path(args.out).resolve() if args.out else None
    if out:
        out.mkdir(parents=True, exist_ok=True)
    spirv_opt = shutil.which("spirv-opt")

    failures = 0
    requests = collect(root, config)
    for path, own, post in requests:
        if not path.exists():
            print(f"MISSING {path.relative_to(root)}")
            failures += 1
            continue
        defines = dict(attributes)
        defines.update(own)
        text = path.read_text(encoding="utf-8")
        lines = text.split("\n")
        version = next((i for i, line in enumerate(lines) if line.strip().startswith("#version")), None)
        injected = ["#extension GL_GOOGLE_include_directive : enable", sources[post]]
        if version is not None:
            lines = lines[: version + 1] + injected + [f"#line {version + 2}"] + lines[version + 1:]
        # Next to the original, so its relative includes resolve the same way.
        fd, temp = tempfile.mkstemp(prefix=path.stem + ".offline.", suffix=path.suffix, dir=path.parent)
        os.close(fd)
        Path(temp).write_text("\n".join(lines), encoding="utf-8")
        tag = "_".join(f"{k}" for k in ("USE_SHARC", "SHARC_QUERY", "SHARC_UPDATE") if own.get(k) == "1")
        name = str(path.relative_to(root)).replace(os.sep, "_").replace("/", "_") + (f".{tag}" if tag else "") + ".spv"
        spv = (out / name) if out else Path(temp + ".spv")
        command = [args.glslang, "-V", "--target-env", "vulkan1.4", f"-I{root}", "-o", str(spv), temp]
        command[1:1] = [f"-D{key}={value}" for key, value in sorted(defines.items())]
        result = subprocess.run(command, capture_output=True, text=True)
        os.remove(temp)
        label = f"{path.relative_to(root)}" + (f" [{tag}]" if tag else "")
        if result.returncode != 0:
            failures += 1
            print(f"FAIL {label}")
            print((result.stdout + result.stderr).replace(temp, str(path)).strip())
        else:
            if args.opt and spirv_opt:
                opt = subprocess.run([spirv_opt, "-O", "--strip-debug", "--target-env=vulkan1.4", str(spv), "-o",
                                      str(spv)], capture_output=True, text=True)
                if opt.returncode != 0:
                    print(f"spirv-opt failed on {label}: {opt.stderr.strip()}")
            print(f"ok   {label}")
        if not out and spv.exists():
            spv.unlink()
    print(f"{len(requests) - failures}/{len(requests)} shaders compile")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
