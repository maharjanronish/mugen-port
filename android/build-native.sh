#!/bin/bash
# build-native.sh — compile SDL2 + MUGEN engine for all ABIs with ndk-build.
#
# Env:
#   TOOLS_DIR  toolchain root (default: $HOME/mugen-tools; see scripts/setup-toolchain.sh)
#   NDK        explicit NDK dir (default: $TOOLS_DIR/android-ndk-r26d)
#   JOBS       parallel jobs (default: nproc)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/android/app"
TOOLS_DIR="${TOOLS_DIR:-$HOME/mugen-tools}"
NDK="${NDK:-$TOOLS_DIR/android-ndk-r26d}"
if [ ! -x "$NDK/ndk-build" ]; then
    echo "error: ndk-build not found at: $NDK"
    echo "  set NDK=<dir> or TOOLS_DIR=<dir>, or run scripts/setup-toolchain.sh first"
    exit 1
fi

# Provide engine/ and SDL/ inside jni/ (generated entries, git-ignored).
# ln -s works on Linux/macOS; git-bash on Windows silently deep-copies (still builds).
provide() { # <dst> <src> [keep-if-exists]
    local dst="$1" src="$2" keep="${3:-}"
    if [ -n "$keep" ] && [ -e "$dst" ]; then return 0; fi
    rm -rf "$dst"
    ln -sfn "$src" "$dst" 2>/dev/null || cp -r "$src" "$dst"
}
provide "$APP/jni/engine" "$ROOT/engine"                 # always refresh (small, may change)
provide "$APP/jni/SDL" "$ROOT/third_party/SDL2-2.30.10" keep   # vendored, rarely changes

"$NDK/ndk-build" -C "$APP" -j"${JOBS:-$(nproc)}"
echo ""
echo "native libs -> $APP/libs/<abi>/{libSDL2.so,libgame.so}"
echo "next: ./android/build-apk.sh"
