# MUGEN for Android — Native Port

**A reverse-engineering-assisted native Android port of the Elecbyte M.U.G.E.N 1.1b1 engine.**
No emulator, no Wine, no x86 translation — the engine is reimplemented in portable C and runs
directly on Android via SDL2.

- 100% native execution (C engine + SDL2)
- Loads the **exact same characters, stages and data files** as the PC version
- Same folder layout as PC MUGEN: `mugen/chars/`, `mugen/stages/`, `mugen/data/` — drop-in compatible
- All three ABIs: `armeabi-v7a`, `arm64-v8a`, `x86_64` — Android 8.0 (API 26) and newer
- Gradle-free build (ndk-build + aapt2/d8/apksigner), fully scriptable
- LAN / Wi-Fi-hotspot netplay scaffolded (`engine/net.c`, experimental)

Current release: **v0.2.1** (`releases/MUGEN-v0.2.1.apk`, ~4.7 MB) — the fight actually
works: colors are correct, moves end, walking/hits/KO rounds all function.

**v0.2.1** fixes 15 engine bugs found by a new end-to-end test harness that drives
the REAL SDL event loop (the old PC tests bypassed the platform layer entirely,
which is why v0.2.0 looked green while broken on device):

- **Corrupted red/green graphics** — sprites were uploaded with the wrong SDL
  pixel format (`ABGR8888` instead of `RGBA8888`), scrambling channels and
  leaking red into transparency.
- **Moves never ended / "continuous punching"** — the animation loop wrapped
  `anim_time` before the `AnimTime = 0` tick was ever observable, so every
  character froze inside its first attack state forever.
- **No hits ever connected** — Clsn boxes authored with reversed corners
  (KFM's head box is `24,-324,-28,-384`) poisoned the collision union, and the
  hitflag/guardflag checker compared uppercase against lowercased characters so
  no hit or guard could ever match.
- **Nobody could walk** — ground friction was applied before position
  integration and then threshold-stopped in the same tick; the threshold was
  also compared in char-local units against world-unit velocity (HD characters
  stopped instantly).
- **Left-facing characters moonwalked** — MUGEN VelSet/VelAdd/PosAdd X
  parameters are facing-relative; the engine treated them as absolute.
- **Jump attacks fell through the floor forever** — there was no engine-side
  landing transition (state 52).
- **First knockdown froze a character permanently** — `PosFreeze` was latched
  with no reset on state change.
- **`AnimElem = 3` triggers never fired** — the space-form argument syntax
  wasn't parsed, so every animelem-triggered HitDef silently did nothing.
- **CPU AI spammed attacks nonstop** — reworked to discrete actions with
  cooldowns (approach / attack once / guard / retreat / idle).
- Touch d-pad now supports **finger sliding** between directions, and idle
  rounds are verified stable (no phantom input).

v0.2.0 fixed the tiny top-left characters (uninitialized `world_scale`), added
localcoord scaling for HD characters, per-button move mapping and the in-game
data folder picker. v0.1.2 fixed a path-join bug ("GAME DATA MISSING"), v0.1.1
fixed the black screen on launch.

## Install

1. Download `releases/MUGEN-v0.2.1.apk`, copy to your phone, install (allow "unknown sources").
2. First launch extracts the bundled MUGEN data to
   `Android/data/com.mugen.android/files/mugen/`
   and asks for storage access (used by the custom-folder picker).
3. Play! Default roster: Kung Fu Man + Kung Fu Man 720 on the Training Room stage.

## Choosing your own data folder (v0.2.0)

On the title screen, tap the **DATA** button (top-right) to open the folder
picker. Browse to any folder on your device (e.g. `Download/mugen`) and tap
**USE THIS FOLDER**. The folder must be a full MUGEN data root — it has to
contain `data/select.def`. Folders that qualify are tagged `MUGEN` in the list.

- Characters go in `<folder>/chars/<name>/`, stages in `<folder>/stages/` —
  exactly the PC MUGEN layout.
- The choice is saved and re-used on the next launch.
- **BUILT-IN DATA** switches back to the APK-bundled data at any time.
- This means you can keep your custom roster on shared storage (or an SD card
  path) without touching the app's private folder, and swap data sets instantly
  without reinstalling.

## Controls (touch)

- Left pad: `← → ↑ ↓` (double-tap `←←` / `→→` to run)
- Right pad: `X Y Z` / `A B C` (top/bottom rows) + `ST` (start/pause)
- Physical keyboards and gamepads also work (SDL2 joystick support).

## Adding characters / stages (exactly like PC)

Either edit the extracted folder above, or (recommended) create your own folder
via the **DATA** picker:

1. Put the character folder into `<your folder>/chars/<charname>/`
   (must contain `<charname>.def`, `.sff`, `.air`, `.cmd`, `.cns`, `.snd`).
2. Add a line to `<your folder>/data/select.def`:
   ```
   <charname>, stages/<stagename>.def
   ```
3. Put stages into `<your folder>/stages/` — every `*.def` found there is loaded.
4. Restart the app (or re-pick the folder). Done — same workflow as the PC engine.

## What's implemented (v0.1)

| System | Status |
|---|---|
| SFF v1 (PCX) + v2 (raw/RLE5/RLE8/LZ5) sprite loading | ✅ decoders recovered from Elecbyte `sff2png.exe` |
| AIR animations + Clsn1/Clsn2 collision boxes | ✅ |
| CNS states, ~45 state controllers, ~60 triggers, expressions | ✅ (KFM + common1.cns verified) |
| CMD commands (motion inputs incl. `$` directions, `/~` modifiers) | ✅ |
| SND sound bank + WAV playback | ✅ |
| FNT v0 + FNT v2 (SFF2) fonts | ✅ |
| ACT palettes | ✅ |
| Stages: bg elements, tiling, parallax delta, camera, bounds | ✅ |
| Fight flow: rounds, KO, time-over, guard, gethit states, juggle basics | ✅ |
| HUD: lifebars, power bars, timer, portraits, round announce | ✅ |
| Simple CPU opponent | ✅ |
| LAN netplay | 🔧 scaffold (`engine/net.c`), engine integration TODO |

Not yet: helper entities, projectiles (`Projectile` sctrl), true HitDef attr
filtering, throws (target states), AfterImage/PalFX, OGG/MP3 music,
PNG-compressed SFF sprites (png8/24/32), superpause effects. Unknown state
controllers are skipped gracefully, so most classic characters partially work.

## Building from source

### 1. Toolchain

Automated (Linux x86_64, no sudo needed):

```bash
./scripts/setup-toolchain.sh          # installs into ~/mugen-tools (override: TOOLS_DIR=...)
```

Manual / macOS / Windows: you need
Android **NDK r26+**, **build-tools 34** (aapt2/zipalign/apksigner) **and 35**
(d8 — 34's `d8 8.2.2-dev` crashes on anonymous inner classes), **platform-34**,
any **JDK 17+**, plus `zip`. Point `TOOLS_DIR` at a dir matching the layout in
`scripts/setup-toolchain.sh` (a plain Android SDK root also works).

### 2. Build

```bash
export TOOLS_DIR=~/mugen-tools

./android/build-native.sh             # ndk-build: SDL2 + engine, all ABIs
./android/build-apk.sh                # -> android/app/build/MUGEN.apk
cp android/app/build/MUGEN.apk releases/MUGEN-vX.Y.Z.apk   # ship it
```

### 3. PC headless validation (no Android needed)

```bash
cd engine
gcc -O1 -w -o /tmp/mugen_test util.c sff.c air.c expr.c char.c state.c fight.c \
    stage.c game.c platform_headless.c -lm
cd ../android/app/src/main/assets/mugen   # bundled data root (same layout as a PC install)
/tmp/mugen_test .                         # dumps BMP frames + prints state traces
```

Expected: `phase: fight (p1=Kung Fu Man vs p2=Kung Fu Man 720, stage=Training Room)`
and after the 3600-frame stability run `p2life=974` (P1's attack connected).

## Repo layout

```
engine/                  portable C core (no platform dependencies)
  mugen.h                all shared types
  util.c                 INI-style config parser (MUGEN quirks: case, comments)
  sff.c                  SFF v1 + v2 loaders, LZ5/RLE5/RLE8 decoders (RE'd)
  air.c                  AIR animations, Clsn boxes, SND banks, FNT fonts
  expr.c                 trigger expression parser/evaluator (60 triggers)
  char.c                 char .def loader, CNS/CMD parsers, constants
  state.c                state machine, ~45 state controllers, command matching
  fight.c                hits, physics, rounds, camera, HUD, explods
  stage.c                stage definitions + backgrounds
  game.c                 gameflow: title/select/fight/victory
  platform_sdl.c         SDL2 renderer, touch gamepad, audio (Android/Linux)
  platform_headless.c    PC validation platform (BMP frame dumps)
  net.c                  LAN lockstep netplay scaffold
  debug_trace.c          dev-only state tracing
android/
  app/jni/               ndk-build project (Android.mk; engine/ + SDL/ are
                         generated links to ../engine and third_party/)
  app/src/main/          Java (SDLActivity + MainActivity), res, manifest
  app/src/main/assets/mugen/   bundled game data (identical to PC layout)
  build-native.sh        link setup + ndk-build
  build-apk.sh           gradle-free APK packaging + signing
third_party/SDL2-2.30.10/   vendored SDL2 source (slimmed to Android/Linux parts)
scripts/setup-toolchain.sh  fetch NDK/build-tools/platform/JDK without sudo
docs/                    SFF2 format spec, RE notes, porting architecture
releases/                shipped APKs
```

## How it works

- `docs/SFF2-FORMAT.md` — the SFF2 binary format + the recovered LZ5/RLE5/RLE8 codecs
- `docs/REVERSE-ENGINEERING.md` — REA/Ghidra methodology, findings, evidence index
- `docs/PORTING-ARCHITECTURE.md` — engine design, Android integration, netplay plan

## Roadmap

- v0.2: netplay integration (LAN/hotspot), character compatibility drive
- later: helpers/projectiles, throws, AfterImage/PalFX, music, screenpack menus

## Legal

M.U.G.E.N is freeware by Elecbyte (1999–2013). This is a fan port for personal,
non-commercial use and interoperability study. Character/stage content belongs
to its respective creators; the bundled default data is the original Elecbyte
distribution. SDL2 is zlib-licensed (see `third_party/SDL2-2.30.10/LICENSE.txt`).
