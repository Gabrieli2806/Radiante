#!/usr/bin/env bash
# Checks that this machine can build Radiante, and says what is missing. It installs nothing but the DLSS SDK
# libraries, which it clones into native/extern/DLSS/lib when run with --fetch-dlss: packages differ too much between
# distributions to install them from here.
#
#   native/setup-linux.sh [--fetch-dlss]
#
# What the build needs (docs/dev/building.md): JDK 25, cmake, ninja or make, g++ 13 or newer, git, Python 3 and the
# Vulkan SDK (VULKAN_SDK set, glslangValidator on PATH).
set -uo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
DLSS_TAG="$(sed -n 's/^ *DLSS_SDK_TAG: *//p' "$REPO/.github/workflows/build.yml" | head -1)"
DLSS_TAG="${DLSS_TAG:-v310.9.1}"
missing=0

report() { # name, ok (0/1), detail
    if [[ "$2" == 0 ]]; then echo "[ok]      $1: $3"; else echo "[missing] $1: $3"; missing=1; fi
}

if command -v java >/dev/null && java -version 2>&1 | head -1 | grep -q '"25'; then
    report "JDK 25" 0 "$(java -version 2>&1 | head -1)"
else
    report "JDK 25" 1 "java 25 is not on PATH"
fi
for tool in cmake git python3; do
    if command -v "$tool" >/dev/null; then report "$tool" 0 "$(command -v "$tool")"; else report "$tool" 1 "not on PATH"; fi
done
if command -v ninja >/dev/null || command -v make >/dev/null; then
    report "ninja or make" 0 "$(command -v ninja || command -v make)"
else
    report "ninja or make" 1 "neither is on PATH"
fi
compiler="${CXX:-g++}"
if command -v "$compiler" >/dev/null && [[ "$("$compiler" -dumpversion | cut -d. -f1)" -ge 13 ]]; then
    report "g++ 13 or newer" 0 "$compiler $("$compiler" -dumpversion)"
else
    report "g++ 13 or newer" 1 "set CXX to a g++ of version 13 or newer"
fi
if [[ -n "${VULKAN_SDK:-}" ]] && command -v glslangValidator >/dev/null; then
    report "Vulkan SDK" 0 "$VULKAN_SDK"
else
    report "Vulkan SDK" 1 "source the SDK's setup-env.sh (VULKAN_SDK set, glslangValidator on PATH)"
fi

dlss="$REPO/native/extern/DLSS/lib/Linux_x86_64"
if [[ ! -d "$dlss" && "${1:-}" == "--fetch-dlss" ]]; then
    temp="$(mktemp -d)"
    git clone --depth 1 --branch "$DLSS_TAG" https://github.com/NVIDIA/DLSS.git "$temp/dlss" \
        && mkdir -p "$(dirname "$dlss")" && cp -r "$temp/dlss/lib/Linux_x86_64" "$dlss"
    rm -rf "$temp"
fi
if [[ -d "$dlss" ]]; then
    report "DLSS SDK libraries ($DLSS_TAG)" 0 "$dlss"
else
    report "DLSS SDK libraries ($DLSS_TAG)" 1 "run native/setup-linux.sh --fetch-dlss"
fi

echo
if [[ "$missing" == 0 ]]; then
    echo "Everything is here. Build with native/build-linux.sh, then ./gradlew build."
else
    echo "Install what is missing with your distribution's packages, then run this again."
    exit 1
fi
