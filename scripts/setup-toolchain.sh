#!/bin/bash
# setup-toolchain.sh — fetch the Android toolchain pieces used to build this port
# into $TOOLS_DIR (default: $HOME/mugen-tools). No sudo required.
#
# Layout produced (what android/build-*.sh expect):
#   $TOOLS_DIR/android-ndk-r26d/        NDK r26d (ndk-build)
#   $TOOLS_DIR/buildtools/android-14/   build-tools 34: aapt2, zipalign, apksigner
#                                      (the r34 zip extracts as "android-14" — a known
#                                      Google packaging quirk; its d8 8.2.2-dev crashes)
#   $TOOLS_DIR/bt35/<name>/             build-tools 35: d8 >= 8.3
#   $TOOLS_DIR/platforms/android-34/    platform-34 (android.jar)
#   $TOOLS_DIR/jdk/                     Temurin JDK 21 (javac, keytool)
#
# Requirements: curl, unzip, tar. Linux x86_64 only (for macOS/Windows see README).
set -e
TOOLS_DIR="${TOOLS_DIR:-$HOME/mugen-tools}"
DL="$TOOLS_DIR/_downloads"
mkdir -p "$TOOLS_DIR" "$DL"

if [ "$(uname -sm)" != "Linux x86_64" ]; then
    echo "error: automated setup supports Linux x86_64 (you have: $(uname -sm))"
    echo "on macOS/Windows install via Android Studio SDK manager and match the layout above"
    exit 1
fi

fetch() { # <url> <outfile>
    if [ -s "$2" ]; then echo "  cached: $(basename "$2")"; return 0; fi
    echo "  downloading $(basename "$2") ..."
    curl -fL --retry 3 --progress-bar -o "$2" "$1"
}

extracted_dir() { # <zip> -> prints the single top-level directory the zip created
    unzip -Z1 "$1" | cut -d/ -f1 | sort -u | sed -n 1p
}

echo "==> 1/5 Android NDK r26d"
NDK_ZIP="$DL/android-ndk-r26d-linux.zip"
fetch https://dl.google.com/android/repository/android-ndk-r26d-linux.zip "$NDK_ZIP"
if [ ! -x "$TOOLS_DIR/android-ndk-r26d/ndk-build" ]; then
    unzip -q -o "$NDK_ZIP" -d "$TOOLS_DIR"
fi

echo "==> 2/5 build-tools 34 (aapt2 / zipalign / apksigner)"
BT34_ZIP="$DL/build-tools_r34-linux.zip"
fetch https://dl.google.com/android/repository/build-tools_r34-linux.zip "$BT34_ZIP"
mkdir -p "$TOOLS_DIR/buildtools"
if [ ! -x "$TOOLS_DIR/buildtools/android-14/aapt2" ]; then
    unzip -q -o "$BT34_ZIP" -d "$TOOLS_DIR/buildtools"
    # zip extracts to "android-14" (r34 quirk); normalize just in case
    d=$(extracted_dir "$BT34_ZIP")
    [ "$d" != "android-14" ] && mv "$TOOLS_DIR/buildtools/$d" "$TOOLS_DIR/buildtools/android-14" || true
fi

echo "==> 3/5 build-tools 35 (d8) — try rc1 first (exact verified combo), then stable"
BT35_ZIP="$DL/build-tools_r35.zip"
BT35_URL=""
for u in build-tools_r35-rc1-linux.zip build-tools_r35_linux.zip build-tools_r35.0.1_linux.zip; do
    code=$(curl -sIL -o /dev/null -w '%{http_code}' --max-time 15 "https://dl.google.com/android/repository/$u")
    if [ "$code" = "200" ]; then BT35_URL="https://dl.google.com/android/repository/$u"; break; fi
done
[ -n "$BT35_URL" ] || { echo "error: no build-tools 35 URL reachable"; exit 1; }
fetch "$BT35_URL" "$BT35_ZIP"
mkdir -p "$TOOLS_DIR/bt35"
if ! ls "$TOOLS_DIR"/bt35/*/d8 >/dev/null 2>&1; then
    unzip -q -o "$BT35_ZIP" -d "$TOOLS_DIR/bt35"
fi

echo "==> 4/5 platform-34 (android.jar)"
PLAT_ZIP="$DL/platform-34-ext7_r03.zip"
fetch https://dl.google.com/android/repository/platform-34-ext7_r03.zip "$PLAT_ZIP"
mkdir -p "$TOOLS_DIR/platforms"
if [ ! -f "$TOOLS_DIR"/platforms/android-34*/android.jar ] && [ ! -f "$TOOLS_DIR"/platforms/android-34/android.jar ]; then
    unzip -q -o "$PLAT_ZIP" -d "$TOOLS_DIR/platforms"
    # normalize to the nested layout: platforms/android-34/android-34/android.jar
    d=$(extracted_dir "$PLAT_ZIP")
    if [ -d "$TOOLS_DIR/platforms/$d" ] && [ "$d" != "android-34" ] && [ ! -d "$TOOLS_DIR/platforms/android-34" ]; then
        mv "$TOOLS_DIR/platforms/$d" "$TOOLS_DIR/platforms/android-34"
    fi
fi

echo "==> 5/5 Temurin JDK 21"
JDK_TGZ="$DL/jdk21.tar.gz"
fetch "https://api.adoptium.net/v3/binary/latest/21/ga/linux/x64/jdk/hotspot/normal/eclipse" "$JDK_TGZ"
if [ ! -x "$TOOLS_DIR/jdk/bin/javac" ]; then
    tar -xzf "$JDK_TGZ" -C "$TOOLS_DIR"
    jd=$(tar -tzf "$JDK_TGZ" | cut -d/ -f1 | sort -u | sed -n 1p)
    rm -rf "$TOOLS_DIR/jdk"
    mv "$TOOLS_DIR/$jd" "$TOOLS_DIR/jdk"
fi

echo ""
echo "toolchain ready under: $TOOLS_DIR"
echo "  check: TOOLS_DIR=$TOOLS_DIR ./android/build-native.sh && TOOLS_DIR=$TOOLS_DIR ./android/build-apk.sh"
