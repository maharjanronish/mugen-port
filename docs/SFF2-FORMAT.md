# SFF2 Binary Format — Recovered Specification

This document describes the SFF v2 ("ElecbyteSpr", version 2) sprite container
as implemented by Elecbyte's own tools, recovered by reverse engineering
`sff2png.exe` (see `REVERSE-ENGINEERING.md`), and as implemented by this port's
loader `engine/sff.c`. All offsets are validated empirically against the stock
MUGEN 1.1b1 data set.

A runnable cross-check of every codec lives in `docs/sff2-decoder-reference.py`.

## File signature and version

```
0x00  12 bytes   "ElecbyteSpr\0"
0x0C  4  bytes   version, big-endian-ish: byte at 0x0F is the major version
                 (0x0F == 2 → SFF v2; anything else with this signature → SFF v1.01)
```

The engine dispatches in `mg_sff_load()`:

```c
if (memcmp(d, "ElecbyteSpr\0", 12) == 0)
    if (d[15] == 2) sff_load_v2(...) else sff_load_v1(...)     /* 12-byte sig */
else if (memcmp(d, "ElecbyteSprite\0", 15) == 0)
    sff_load_v1(...)                                            /* 15-byte sig, classic v1.01 */
```

Stock 1.1b1 data mixes both: `kfm.sff`, `fight.sff`, `system.sff`, `fightfx.sff`
are **SFF v1.01** (PCX sprites); `kfm720.sff`, `stage0.sff`, `stage0-720.sff`
and the SFF-based fonts are **SFF v2**.

## SFF v2 header (512 bytes total)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0x00 | 12 | signature | `"ElecbyteSpr\0"` |
| 0x0C | 4 | version | see above |
| 0x24 | 4 | sprite node table offset | array of 28-byte nodes |
| 0x28 | 4 | sprite count | |
| 0x2C | 4 | palette node table offset | array of 16-byte nodes |
| 0x30 | 4 | palette count | |
| 0x34 | 4 | lump1 offset | data area #1 (default) |
| 0x38 | 4 | lump2 (size of data area #1) | |
| 0x3C | 4 | lump3 offset | data area #2, used when sprite `flags & 1` |
| ... | ... | reserved | header padded to 512 bytes |

## Palette node (16 bytes)

| Offset | Size | Field |
|---|---|---|
| +0 | 2 | group |
| +2 | 2 | number |
| +4 | 4 | numColors |
| +8 | 4 | dataOff (relative to **lump1**) |
| +12 | 4 | dataLen |

Palette data is `numColors` RGBA quads (4 bytes/entry: R, G, B, A).
Alpha semantics: `A == 0` → transparent color entry; any nonzero A is treated
as opaque 255 (except font-mode SFFs, where index 0 is forced transparent and
all others opaque — that is the FNT convention that makes glyphs visible).

## Sprite node (28 bytes)

| Offset | Size | Field | Notes |
|---|---|---|---|
| +0 | 2 | group | e.g. 0 = standing, 9000 = portraits |
| +2 | 2 | number | |
| +4 | 2 | width | |
| +6 | 2 | height | |
| +8 | 2 | axis X (signed) | sprite anchor |
| +10 | 2 | axis Y (signed) | |
| +12 | 2 | link (unused by loader) | |
| +14 | 1 | format | see codec table |
| +15 | 1 | color depth | `<= 5` → 5-bit indices (32-entry palette) |
| +16 | 4 | dataOff | relative to lump1 **or lump3** (see flags) |
| +20 | 4 | dataLen | 0 → skip (linked/empty sprite) |
| +24 | 2 | palette id | indexes the palette node table |
| +26 | 2 | flags | `flags & 1` → data lives in **lump3** |

## Sprite codecs (format field)

| fmt | codec | stream |
|---|---|---|
| 0/1 | raw indices | `w*h` bytes, no prefix |
| 2 | RLE8 | `u32 size` prefix, then stream |
| 3 | RLE5 | `u32 size` prefix, then stream |
| 4 | LZ5 | `u32 size` prefix, then stream |
| 10/11/12 | PNG 8/24/32-bit | **not implemented** (not present in stock data; fails gracefully to a placeholder) |

The `u32 size` prefix is the **decompressed** byte count; it is what the
decoder validates against (`out == n`).

### RLE8 (`mg_decode_rle8`)

```
loop:
    b = next
    if (b & 0xc0) == 0x40:      # run marker
        color = next; count = b & 0x3f
        emit color × count
    else:
        emit b                  # literal index
```

### RLE5 (`mg_decode_rle5`)

```
loop (needs >= 2 bytes):
    count = next + 1
    flags = next
    if flags & 0x80: color = next else color = 0
    emit color × count
    litcount = flags & 0x7f
    copy litcount literal bytes
```

### LZ5 (`mg_decode_lz5`) — the interesting one

Bit-stream: a flag byte is consumed MSB→LSB; each bit selects the next op.

```
roll = 0, rollcnt = 0
loop:
    if flagbits == 0: flagbyte = next; flagbits = 8
    flagbits -= 1; bit = flagbyte & 1; flagbyte >>= 1
    if bit == 0:                 # RLE run, 5-bit color
        b = next; color = b & 0x1f; count = b >> 5
        if count == 0: count = next + 8
        emit color × count
    else:                        # match
        b = next; len6 = b & 0x3f
        if len6 != 0:            # short match
            nb = (roll << 2) | (b >> 6)     # 2 new high bits roll into an 8-bit
            roll = nb; rollcnt += 2         # "rolling distance" accumulator
            ln = len6 + 1
            if rollcnt != 8: dist = next    # distance byte from stream
            else:             dist = nb; rollcnt = 0   # every 4th short match:
        else:                    # long match  # distance comes from the roller
            lo = next; ln = next + 3
            dist = lo | ((b & 0xc0) << 2)   # 10-bit distance
        copy ln bytes from output[-dist-1]
```

The 2-bit rolling distance accumulator is the signature quirk of Elecbyte's
LZ5: across four consecutive short matches it assembles an 8-bit value from
pairs of bits, and every 4th short match takes its backreference distance
from that roller instead of the stream.

## Palette application

Decoded streams are index maps. Final RGBA per pixel:

```
pal = palettes[sprite.palid]           (fallback: palettes[0])
palmax = (depth <= 5) ? 32 : 256       # 5-bit indices wrap with (ci & (palmax-1))
A: pal entry alpha; 0 → fully transparent, else forced to 255
```

## SFF v1.01 (for contrast)

- Sub-headers form a **linked list** (not a table): each 32-byte subheader has
  `next` (offset of the next subheader), `dlen`, axis, group/number, `linked`
  (copy pixels from an earlier sprite), `sharedpal` flag.
- Pixels are **PCX** (8bpp, RLE `0xC0`-prefix runs); palette = last 769 bytes
  of the PCX blob (`0x0C` marker + 768 RGB).
- `sharedpal == 0` → use the PCX's own palette; otherwise keep the previous one.

## Validation results

- `docs/sff2-decoder-reference.py` decodes every sprite of the stock SFF2 set:
  **kfm720.sff, stage0.sff, stage0-720.sff, font/*.sff — 480/480 sprites exact-length**
  (`out == w*h` for every sprite, zero failures).
- The C decoders in `engine/sff.c` are line-for-line ports of the same logic and
  render the live game (verified frame-by-frame in the headless platform).
