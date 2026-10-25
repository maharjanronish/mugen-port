# Porting Architecture

How the native Android port of MUGEN 1.1b1 is put together — engine design,
platform split, Android integration, and the netplay plan.

## Design rule: the engine is a library

`engine/` is plain, portable C with **zero platform dependencies** (no SDL, no
Android, no POSIX assumptions beyond libc + math). It parses and simulates;
it never touches a screen, speaker, or file system directly — all I/O funnels
through two thin platform backends:

- `platform_sdl.c` — the production backend (Android/Linux): SDL2 renderer,
  multitouch virtual gamepad, WAV audio, joystick.
- `platform_headless.c` — the validation backend (PC): writes BMP frame dumps
  and state traces. This is how the engine was developed and verified without
  a phone (exact MUGEN physics: KFM walks 2.04 px/frame, hits drop P2 to
  974/1000, gethit recovery, 63+ s stable runs).

Same engine object files build on PC (`gcc`) and Android (`ndk-build`).

## Data flow

```
MainActivity (Java)
  └─ first run: extract assets/mugen/ -> Android/data/com.mugen.android/files/mugen/
      (user-modifiable; identical layout to a PC MUGEN install)
  └─ SDLActivity -> JNI -> libgame.so
        └─ platform_sdl.c: window/renderer/gamepad/audio
             └─ game.c: title -> select -> fight -> victory loop
                  ├─ char.c: .def -> .cns/.cmd constants & commands
                  ├─ sff.c/air.c: sprites (LZ5/RLE5/RLE8), animations, Clsn
                  ├─ expr.c + state.c: trigger expressions, state controllers
                  └─ fight.c/stage.c: hits, physics, rounds, camera, HUD, bg
```

## Engine internals worth knowing

- **Config parsing** (`util.c`): INI-style with MUGEN quirks — case-insensitive
  section/key matching, `;` comments, quoted values, `include` handling.
- **State machine** (`state.c`): CNS states with `-1,-2,-3` special groups;
  ~45 state controllers (StateCtrl) implemented; unknown sctrls are skipped
  gracefully (logged) so foreign characters degrade instead of crashing.
- **Triggers** (`expr.c`): full expression parser (precedence, `&&/||/!`,
  arithmetic, redirects) with ~60 trigger functions; this is the
  compatibility surface for community characters.
- **Commands** (`char.c`): `[Command]` matching with time buffers, `$`
  direction chaining, `/` `~` `+` modifiers; engine-side default state
  transitions (walk/crouch/jump) mirror MUGEN's built-ins.
- **Fight** (`fight.c`): HitDef/guard/gethit/juggle, round flow (round
  announce, KO/DKO/TO), camera bounds, HUD bars via fight.def.

## Hard-won fixes (documented so they don't regress)

| Bug | Root cause | Fix |
|---|---|---|
| characters throw supers from wrong states | multi-line `triggerall` overwrote itself (lost AND semantics) | triggerall array, all lines AND-ed |
| all 37 commands empty | `[Command]` section compare was case-sensitive | lowercase before compare |
| hits never connect | Clsn boxes read wrong struct field | parse from `it.value` |
| triggers always false | `&&`/`||` lexer/evaluator mismatch | OP_LOGAND/OP_LOGOR mapping |
| no walk/crouch/jump transitions | engine-side default transitions missing | added in `state.c` |
| `const()` velocity aliases mismatched | `velocity.foo` vs `.x/.y` naming | alias map |
| font glyphs invisible | palette alpha 0 | font mode: index 0 transparent, rest opaque |
| d8 crash on anonymous inner classes | build-tools 34 d8 8.2.2-dev | dex with build-tools 35's d8 |

## Android packaging without gradle

`android/build-apk.sh`: aapt2 compile/link → javac `-source 8` (R.java, SDL
java glue, MainActivity) → d8 (min-api 26) → zip in `classes.dex` +
`lib/<abi>/*.so` + `assets/mugen/` → zipalign → apksigner (debug key).
Native side is `android/app/jni/Android.mk` — `game` module (the engine,
via the generated `jni/engine` link) linking against vendored SDL2
(`jni/SDL` link). Three ABIs, `APP_PLATFORM=android-26`, `c++_static`.

## Netplay design (engine/net.c scaffold)

UDP lockstep: both peers exchange input tuples per frame tick; the engine
advances a tick only when both inputs are present (deterministic engine =
identical outcomes). Host binds, client joins by IP. Remaining work is at
the platform layer (socket pump + UI), the engine integration points are
marked TODO in `game.c`.

## Known limitations (v0.1)

- No helper entities / projectiles / throws / AfterImage / PalFX
- OGG/MP3 music not parsed (WAV only)
- PNG-compressed SFF sprites fall back to placeholder
- Screenpack menus beyond title/select/victory are minimal
