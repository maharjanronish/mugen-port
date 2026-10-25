#!/bin/bash
# build-apk.sh — package + sign the APK without gradle.
# Pipeline: aapt2 -> javac -> d8 -> zip -> zipalign -> apksigner
#
# Env:
#   TOOLS_DIR  toolchain root (default: $HOME/mugen-tools; see scripts/setup-toolchain.sh).
#              A standard Android SDK root also works (build-tools/NN, platforms/).
#   JDK        JDK root (default: $TOOLS_DIR/jdk) — needs javac + keytool (JDK 17+)
#
# Produces: android/app/build/MUGEN.apk
set -e
# Works both from the repo root layout (android/app) and when the script
# lives next to app/ itself (this tree: mugen-android/app).
SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
if [ -d "$SELF_DIR/android/app" ]; then ROOT="$SELF_DIR"; else ROOT="$(dirname "$SELF_DIR")"; fi
APP="$ROOT/android/app"
[ -d "$APP" ] || APP="$SELF_DIR/app"
TOOLS_DIR="${TOOLS_DIR:-$HOME/mugen-tools}"
JDK="${JDK:-$TOOLS_DIR/jdk}"

first() { for p in "$@"; do [ -e "$p" ] && { echo "$p"; return 0; }; done; return 1; }

# aapt2/zipalign/apksigner — build-tools 34 (its d8 8.2.2-dev is unusable: crashes
# on anonymous inner classes, so dexing uses build-tools 35's d8 instead).
BT=$(first "$TOOLS_DIR/buildtools/android-14" "$TOOLS_DIR"/build-tools/34* "$TOOLS_DIR"/build-tools/*) \
    || { echo "error: build-tools (aapt2) not found under $TOOLS_DIR"; exit 1; }
D8BIN=$(first "$TOOLS_DIR"/bt35/*/d8 "$TOOLS_DIR"/build-tools/35*/d8 "$TOOLS_DIR"/build-tools/3[5-9]*/d8) \
    || { echo "error: build-tools 35 d8 not found under $TOOLS_DIR"; exit 1; }
PLATFORM=$(first "$TOOLS_DIR"/platforms/android-34/android-34/android.jar \
                  "$TOOLS_DIR"/platforms/android-34/android.jar \
                  "$TOOLS_DIR"/platforms/android-3[4-9]*/android.jar) \
    || { echo "error: platform android.jar not found under $TOOLS_DIR"; exit 1; }

for x in "$BT/aapt2" "$BT/zipalign" "$BT/apksigner" "$D8BIN" "$PLATFORM" "$JDK/bin/javac" "$JDK/bin/keytool"; do
    [ -e "$x" ] || { echo "error: missing toolchain piece: $x"; exit 1; }
done
command -v zip >/dev/null 2>&1 || { echo "error: 'zip' is required (apt install zip / brew install zip)"; exit 1; }

cd "$APP"
mkdir -p build build/gen

echo "=== 1. compile resources (aapt2) ==="
"$BT/aapt2" compile --dir src/main/res -o build/res.zip

echo "=== 2. link resources + manifest ==="
# Explicit SDK levels: without them the APK ships with targetSdk=0, which makes
# modern ROMs apply legacy-compat shims (e.g. ColorOS maxAspectRatio 1.86
# letterboxing). 26/28 keeps pre-scoped-storage behavior; we only ever touch
# our own app-specific dirs, which is allowed at any targetSdk.
"$BT/aapt2" link -o build/base.apk \
    -I "$PLATFORM" \
    --manifest src/main/AndroidManifest.xml \
    --min-sdk-version 26 \
    --target-sdk-version 28 \
    --version-code 4 \
    --version-name 0.2.0 \
    --java build/gen \
    --auto-add-overlay \
    build/res.zip

echo "=== 3. compile java ==="
rm -rf build/classes && mkdir -p build/classes
"$JDK/bin/javac" -source 8 -target 8 \
    -classpath "$PLATFORM" \
    -d build/classes \
    build/gen/com/mugen/android/R.java \
    src/main/java/org/libsdl/app/*.java \
    src/main/java/com/mugen/android/MainActivity.java 2>&1 | grep -v "warning" || true

echo "=== 4. dex (d8) ==="
"$JDK/bin/jar" cf build/classes.jar -C build/classes .
"$D8BIN" --release --lib "$PLATFORM" \
    --min-api 26 \
    --output build/ \
    build/classes.jar 2>&1 | grep -v "^$" || true

echo "=== 5. add dex + assets + libs to APK ==="
cd build
cp base.apk aligned.apk
zip -q -j aligned.apk classes.dex
# native libs must keep lib/<abi>/ structure (Android only extracts from lib/<abi>/)
rm -rf libpkg && mkdir -p libpkg/lib
cp -r ../libs/*/ libpkg/lib/
(cd libpkg && zip -q -r ../aligned.apk lib)
rm -rf libpkg
# bundled game data.
# NOTE: AssetManager ONLY sees entries under the "assets/" prefix inside the APK.
# (zipping the bare "mugen/" dir put files at APK root -> app saw zero assets.)
rm -rf assetstmp && mkdir -p assetstmp/assets
cp -R ../src/main/assets/. assetstmp/assets/
(cd assetstmp && zip -q -r ../aligned.apk assets)
rm -rf assetstmp

echo "=== 6. zipalign ==="
"$BT/zipalign" -f 4 aligned.apk aligned2.apk
mv aligned2.apk unsigned.apk

echo "=== 7. sign (debug key) ==="
KEY=debug.keystore
if [ ! -f "$KEY" ]; then
    "$JDK/bin/keytool" -genkeypair -keystore "$KEY" -alias androiddebugkey \
        -storepass android -keypass android -keyalg RSA -keysize 2048 \
        -validity 10000 -dname "CN=MUGEN Debug,O=MUGEN,C=US" 2>/dev/null
fi
"$BT/apksigner" sign --ks "$KEY" --ks-pass pass:android --key-pass pass:android \
    --ks-key-alias androiddebugkey --out MUGEN.apk unsigned.apk

"$BT/apksigner" verify MUGEN.apk
echo ""
echo "=== APK BUILT: $APP/build/MUGEN.apk ==="
ls -la MUGEN.apk
echo "ship it: cp $APP/build/MUGEN.apk releases/MUGEN-vX.Y.Z.apk"
