# Reverse Engineering Notes

Two binaries were studied: the game engine `mugen.exe` (architecture, boot
chain) and the utility `sff2png.exe` (SFF2 container + codecs). The port is a
clean-room reimplementation informed by these findings; no Elecbyte code is
included. Raw evidence is archived under `docs/re-evidence/`.

**Targets**

| Binary | SHA-256 (prefix) | Size |
|---|---|---|
| mugen.exe | `d5508f77f2a86f12` … (full hash in `re-evidence/mugen-rea-analysis.json`) | 1,092,608 bytes, PE32 x86 |
| sff2png.exe | see `re-evidence/sff2png-*.json` | Elecbyte 1.1 tool |

## Toolchain

- REA 6.7.0 ("Reverse Engineer Anything") driving Ghidra 12.1.4 as backend
- Temurin JDK 21 (`JAVA_HOME`), Ghidra at `GHIDRA_INSTALL_DIR`
- `objdump -p` for the PE import table; `rea decompile` for C output

```
GHIDRA_INSTALL_DIR=<...> JAVA_HOME=<...> rea analyze mugen.exe
rea inspect-pe-resources mugen.exe
rea search --kind strings mugen.exe
rea decompile <addr> mugen.exe
```

## mugen.exe — identity and architecture

- Product: **M.U.G.E.N 1.1.0 Beta 1 P1** (Aug 2013, the final Elecbyte release), © 1999–2013 Elecbyte
- Built with **Visual Studio 2008 Release**, `/GS` stack protection, SEH frames
- Imports: SDL / SDL_mixer / SDL_ttf / SDL_gfx, **OpenGL32+GLU32**, **zlib1**,
  libresample, pthreadVC2, WINMM → an SDL+OpenGL+zlib+pthread engine
- 2,751 procedures, 5,598 strings, .text 0x401000–0x4dda00

### Boot chain (fully decompiled — `re-evidence/mugen-decompiled-core.c`)

```
entry (0x4dc80a)
 └─ ___tmainCRTStartup (0x4dc54a)          MSVC CRT init
     └─ FUN_004dd240                       WinMain wrapper
         ├─ LoadLibraryA("DDRAW.DLL")       legacy DirectDraw probe
         ├─ FUN_004dd360                    command-line parser
         └─ FUN_004dcf80 (SDL main)
             ├─ SDL_Init(0x100000)
             └─ FUN_00421500(argc, argv)     GAME MAIN
                 ├─ FUN_0041a560  "Initializing..."    (loads data/mugen.cfg)
                 ├─ FUN_0046ba90  "Set up graphics..." (SDL/OpenGL)
                 ├─ FUN_00476ec0   timer subsystem
                 ├─ FUN_004124a0   gameflow loop   <-- THE GAME
                 └─ FUN_0041aea0   deinit
```

Engine string inventory (config paths, fight.def keys, CNS constants, font
defs) is in `re-evidence/mugen-search-formats.json` — these defined the DEF/CNS
key surface this port's parsers implement.

## sff2png.exe — the SFF2 campaign (the key breakthrough)

`sff2png.exe` reads SFF2 and writes PNGs, so it contains a complete decoder
chain. Recovered by decompiling the loader and the three codec functions
(evidence: `sff2png-loader*.json`, `sff2png-decomp-lz5.json`,
`sff2png-lz5real.json`, `sff2png-dec0x408750.json`, …):

1. **Container** — header fields at 0x24/0x28/0x2C/0x30/0x34/0x38/0x3C
   (sprite table, counts, palette table, lump1/lump3 data areas). Nodes:
   28-byte sprite entries, 16-byte palette entries. Full tables in
   `SFF2-FORMAT.md`.
2. **RLE8 / RLE5** — straightforward run-length codecs, recovered in one pass.
3. **LZ5** — the hard one. The decoder reads a flag-bit stream; short matches
   consume a distance byte from the stream, **except every 4th short match**,
   which takes its distance from an 8-bit "roller" assembled from the top 2
   bits of the preceding three short-match bytes (`(roll<<2)|(b>>6)`).
   This 2-bit rolling accumulator only became clear after stepping the
   decompiled function against known-good sprite data (the stage0 gradient
   provided a deterministic test vector).

### Validation methodology

`docs/sff2-decoder-reference.py` (Python) decodes **every sprite** in the
stock SFF2 files and requires exact output length (`out == w*h`):
**480/480 sprites, zero failures** — including a character sheet, a
720p-rescale of it, a gradient stage, and SFF-based fonts. The same logic was
then ported to C (`engine/sff.c`) and driven by the live game loop.

## Evidence index (`docs/re-evidence/`)

| File | Contents |
|---|---|
| `mugen-rea-analysis.json` | full REA analysis envelope |
| `mugen-pe-resources.json` | PE version resource / manifest |
| `mugen-search-formats.json` | engine string inventory (DEF/CNS keys) |
| `mugen-search-procs.json` | procedure search |
| `mugen-decomp-{crt,entry,main,gamemain,winmain}.json` | per-function decompilations |
| `mugen-decompiled-core.c` | combined readable decompiled core |
| `sff2png-loader{,2}.json`, `sff2png-sff{,2}read.json`, `sff2png-sffread.json` | container loaders |
| `sff2png-decomp-lz5.json`, `sff2png-lz5{,-asm,-func,real}.json` | LZ5 decoder work |
| `sff2png-dec0x{408750,40a4e0,40a500}.json`, `sff2png-spritedecomp.json`, `sff2png-sprites{,2}.json`, `sff2png-sprmake2.html*` | codec + sprite dumps |

Reverse engineering performed for educational/interoperability study of
freeware. M.U.G.E.N is freeware by Elecbyte.
