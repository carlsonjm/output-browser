#!/bin/bash
# Builds Output Browser against a CEF binary distribution.
#   CEF=/path/to/cef_binary_... bash tools/build.sh [output folder]
# Needs cmake, make or ninja, and g++. The output folder defaults to ./out.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
: "${CEF:?Set CEF to the folder of a CEF binary distribution}"
OUT=${1:-$ROOT/out}

cmake -S "$CEF" -B "$CEF/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$CEF/build" --target libcef_dll_wrapper -j"$(nproc)"
WRAP=$(find "$CEF/build" -name libcef_dll_wrapper.a | head -1)

mkdir -p "$OUT"
cp -r "$CEF"/Release/* "$CEF"/Resources/* "$OUT"/
rm -rf "$OUT/ui"
cp -r "$ROOT/ui" "$OUT/ui"
g++ -std=c++20 -O2 -DNDEBUG -fno-stack-protector -I"$CEF" "$ROOT"/src/*.cc "$WRAP" \
    -L"$CEF/Release" -lcef -Wl,-rpath,'$ORIGIN' -lpthread -ldl -o "$OUT/output-browser"
echo "built: $OUT/output-browser"
