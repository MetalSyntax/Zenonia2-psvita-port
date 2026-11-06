#!/bin/bash
# Build in /tmp (no spaces in path: vendored vitaGL's Makefile breaks on "PSVITA Develop"),
# same scheme as Zenonia3-vita. psvita-toolkit build uses this script when present.
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="/tmp/zenonia2-src"
BUILD_DIR="/tmp/zenonia2-build"
VPK_NAME="zenonia_2.vpk"

export VITASDK="${VITASDK:-$HOME/vitasdk}"
export PATH="$VITASDK/bin:$PATH" # vitaGL's Makefile calls arm-vita-eabi-gcc without a path

mkdir -p "$SRC_DIR" "$BUILD_DIR"
rsync -a --delete --exclude '.*' --exclude '/build' --exclude '/vitaGL' \
  --exclude '/apk_extract' --exclude '/apk_decompiled' --exclude '/output' \
  --exclude '/ux0_data' --exclude '/logs' --exclude '/uploads' --exclude '*.apk' --exclude '*.zip' \
  --exclude '*.o' --exclude 'libvitaGL.a' --exclude 'libvitashark.a' \
  "$PROJECT_DIR/" "$SRC_DIR/"

cd "$BUILD_DIR"
cmake "$SRC_DIR" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release "$@"
make -j"$(sysctl -n hw.ncpu)"

mkdir -p "$PROJECT_DIR/build"
cp "$VPK_NAME" "$PROJECT_DIR/build/"
cp zenonia_2.self "$PROJECT_DIR/build/eboot.bin" # same file the VPK packs as eboot.bin
echo "Build OK: $PROJECT_DIR/build/$VPK_NAME + eboot.bin"
