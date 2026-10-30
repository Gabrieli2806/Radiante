"""Checks the loader's zip extraction (shader_pack::extractPackZip) against crafted zips: path escapes, a symbolic
link, a header that lies about its size, a decompression bomb, too many files, and a good one.

    python zip_safety_test.py <shader_pack_stage executable> <scratch folder>

Every bad zip must be refused with nothing left in its output folder; the good one extracted. Exit status 0 when so.
"""
import subprocess
import struct
import sys
import zipfile
from pathlib import Path

tool = sys.argv[1]
out = Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)


def simple(name, entries):
    with zipfile.ZipFile(out / name, "w", zipfile.ZIP_DEFLATED) as z:
        for entry, data in entries:
            z.writestr(entry, data)


simple("good.zip", [("configs.json", "{}"), ("hooks/sky.glsl", "void hookSky(inout vec3 r, vec3 d) {}")])
simple("dotdot.zip", [("configs.json", "{}"), ("../evil.txt", "x")])
simple("dotdot_deep.zip", [("configs.json", "{}"), ("a/../../evil.txt", "x")])
simple("absolute.zip", [("configs.json", "{}"), ("/evil.txt", "x")])
simple("drive.zip", [("configs.json", "{}"), ("C:/evil.txt", "x")])
simple("backslash.zip", [("configs.json", "{}"), ("a\\..\\..\\evil.txt", "x")])

# A symbolic link (Unix mode 0120777) pointing out of the folder, then a file under the link's path.
with zipfile.ZipFile(out / "symlink.zip", "w") as z:
    z.writestr("configs.json", "{}")
    link = zipfile.ZipInfo("link")
    link.create_system = 3
    link.external_attr = (0o120777 << 16)
    z.writestr(link, "../../outside")
    z.writestr("link/evil.txt", "x")

# A header that says 16 bytes for 1 MB of data (local header and central directory both patched).
data = b"A" * (1024 * 1024)
with zipfile.ZipFile(out / "liar.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("configs.json", "{}")
    z.writestr("big.bin", data)
raw = bytearray((out / "liar.zip").read_bytes())
size = struct.pack("<I", len(data))
small = struct.pack("<I", 16)
# Local header: uncompressed size at offset 22; central directory: at offset 24. Only the big entry has this size.
count = 0
start = 0
while True:
    i = raw.find(size, start)
    if i < 0:
        break
    raw[i:i + 4] = small
    count += 1
    start = i + 4
(out / "liar.zip").write_bytes(bytes(raw))

# A bomb: 600 MB of zeros, honest headers.
with zipfile.ZipFile(out / "bomb.zip", "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    z.writestr("configs.json", "{}")
    with z.open("zeros.bin", "w", force_zip64=True) as f:
        chunk = bytes(1024 * 1024)
        for _ in range(600):
            f.write(chunk)

# Too many files.
with zipfile.ZipFile(out / "many.zip", "w") as z:
    for i in range(20001):
        z.writestr(f"f/{i}.txt", "")

failures = 0
for name in ("good", "dotdot", "dotdot_deep", "absolute", "drive", "backslash", "symlink", "liar", "bomb", "many"):
    target = out / "x" / name
    result = subprocess.run([tool, "--extract", str(out / f"{name}.zip"), str(target)], capture_output=True, text=True)
    left = list(target.rglob("*")) if target.exists() else []
    ok = (result.returncode == 0 and len(left) == 3) if name == "good" else (result.returncode == 1 and not left)
    escaped = list(out.rglob("evil.txt")) + list(out.rglob("outside"))
    ok = ok and not escaped
    failures += 0 if ok else 1
    print(("ok   " if ok else "FAIL ") + name + ": " + result.stdout.strip())
print("all zips handled" if failures == 0 else f"{failures} failed")
sys.exit(1 if failures else 0)
