#!/bin/sh
# Compile check of everything (Windows x64 DLLs with zig). Usage: sh hotreload/check.sh <sdk dir>   (default: sdk15)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SDK=${1:-$HERE/../sdk15}
OUT=$HERE/build
CC="python3 -m ziglang cc -target x86_64-windows-gnu -shared -O2 -Wall -Werror -Wno-unused-parameter -I$SDK"
rm -rf "$OUT" && mkdir -p "$OUT"

# the socket as HellForge sees it: one mod folder's src/ with socket.c (named after the mod), the ABI header and a config
stage() { mkdir -p "$OUT/$1/src"; cp "$HERE/socket/src/socket.c" "$OUT/$1/src/$1.c"; cp "$HERE/hotreload_plug.h" "$OUT/$1/src/"; [ -n "$2" ] && cp "$2" "$OUT/$1/src/hr_config.h"; true; }
stage bare ""
stage example "$HERE/example/hr_config.h"
stage leva "$HERE/leva/hr_config.h"
for m in bare example leva; do $CC "$OUT/$m/src/$m.c" -o "$OUT/$m.dll"; echo "socket ($m config): ok"; done

$CC -I"$HERE" "$HERE/example/plug.c" -o "$OUT/example_plug.dll"; echo "example plug: ok"
$CC -I"$HERE" -DHR_RELEASE "$HERE/example/plug.c" -o "$OUT/example_release.dll"; echo "example release: ok"
