#!/usr/bin/env python3
"""Validate recovered SFF2 decoders (LZ5, RLE8, RLE5) against real MUGEN data."""
import struct

def lz5_decode(data):
    n, = struct.unpack('<I', data[:4])
    src = data[4:]
    dst = bytearray()
    si = 0
    flagbyte = 0; flagbits = 0
    roll = 0; rollcnt = 0
    limit = len(src)
    while si < limit:
        if flagbits == 0:
            flagbyte = src[si]; si += 1; flagbits = 8
        flagbits -= 1
        bit = flagbyte & 1; flagbyte >>= 1
        if bit:  # match
            b = src[si]; si += 1
            len6 = b & 0x3f
            if len6 != 0:  # short match
                nb = ((roll << 2) | (b >> 6)) & 0xff
                roll = nb; rollcnt += 2
                ln = len6 + 1
                if rollcnt != 8:
                    dist = src[si]; si += 1
                else:
                    dist = nb; rollcnt = 0
            else:  # long match
                lo = src[si]; si += 1
                ln = src[si] + 3; si += 1
                dist = lo | ((b & 0xc0) << 2)
            if dist + 1 > len(dst):  # invalid backref
                return None
            for _ in range(ln):
                dst.append(dst[-dist-1])
        else:  # RLE run, 5-bit color
            b = src[si]; si += 1
            color = b & 0x1f
            count = b >> 5
            if count == 0:
                count = src[si] + 8; si += 1
            dst += bytes([color]) * count
        if len(dst) > n:
            return None
    return bytes(dst[:n]) if len(dst) == n else None

def rle8_decode(data):
    n, = struct.unpack('<I', data[:4])
    src = data[4:]
    dst = bytearray()
    i = 0
    while i < len(src):
        b = src[i]; i += 1
        if (b & 0xc0) == 0x40:
            if i >= len(src): break
            color = src[i]; i += 1
            count = b & 0x3f
            if count == 0: break
            dst += bytes([color]) * count
        else:
            dst.append(b)
    return bytes(dst[:n]) if len(dst) >= n else None

def rle5_decode(data):
    n, = struct.unpack('<I', data[:4])
    src = data[4:]
    dst = bytearray()
    i = 0
    while i + 1 < len(src):
        count = src[i] + 1
        flags = src[i+1]
        i += 2
        litcount = flags & 0x7f
        color = 0
        if flags & 0x80:
            color = src[i]; i += 1
        dst += bytes([color]) * count
        if litcount > 0:
            dst += src[i:i+litcount]; i += litcount
    return bytes(dst[:n]) if len(dst) >= n else None

def parse_sff2(path):
    d = open(path, 'rb').read()
    spr_off = struct.unpack('<I', d[36:40])[0]
    n_spr = struct.unpack('<I', d[40:44])[0]
    n_pal = struct.unpack('<I', d[48:52])[0]
    lump1 = struct.unpack('<I', d[52:56])[0]
    lump3 = struct.unpack('<I', d[60:64])[0]
    sprites = []
    for i in range(n_spr):
        o = spr_off + i*28
        g,n = struct.unpack('<HH', d[o:o+4])
        w,h = struct.unpack('<HH', d[o+4:o+8])
        fmt, depth = d[o+14], d[o+15]
        off, ln = struct.unpack('<II', d[o+16:o+24])
        palid = struct.unpack('<H', d[o+24:o+26])[0]
        flags = struct.unpack('<H', d[o+26:o+28])[0]
        sprites.append(dict(g=g,n=n,w=w,h=h,fmt=fmt,depth=depth,off=off,ln=ln,palid=palid,flags=flags))
    return d, lump1, lump3, sprites

def decode_sprite(d, lump1, lump3, s):
    base = lump3 if (s.get('flags', 0) & 1) else lump1
    data = d[base+s['off']:base+s['off']+s['ln']]
    fn = {2: rle8_decode, 3: rle5_decode, 4: lz5_decode}.get(s['fmt'])
    if fn is None: return None
    return fn(data)

def art(px, w, h, step=2):
    chars = ' .:-=+*#%@'
    lines = []
    for r in range(0, h, step):
        lines.append(''.join(chars[min(v,9)] if v else '.' for v in px[r*w:r*w+w]))
    return '\n'.join(lines)

if __name__ == '__main__':
    # 1) stage0 RLE8 gradient
    d, l1, l3, sprs = parse_sff2('stages/stage0.sff')
    s = [x for x in sprs if (x['g'],x['n'])==(0,0)][0]
    px = decode_sprite(d, l1, l3, s)
    print(f"stage0 (0,0) RLE8 flags={s.get('flags')}: {'OK' if px else 'FAIL'} {len(px) if px else 0}/{s['w']*s['h']}")
    if px: print('  row0 colors:', list(px[:10]), '... row239:', list(px[239*50:239*50+6]))

    # 2) kfm LZ5
    d, l1, l3, sprs = parse_sff2('chars/kfm/kfm.sff')
    for (g, n) in [(0,1), (9000,0)]:
        cand = [x for x in sprs if (x['g'],x['n'])==(g,n)]
        if not cand: print(f"kfm ({g},{n}): NOT FOUND"); continue
        s = cand[0]
        px = decode_sprite(d, l1, l3, s)
        status = 'OK' if px else 'FAIL'
        print(f"\nkfm ({g},{n}) fmt={s['fmt']} depth={s['depth']} {s['w']}x{s['h']}: {status}")
        if px:
            print(art(px, s['w'], s['h']))

    # 3) verify ALL kfm sprites decode
    ok = fail = 0
    for s in sprs:
        if s['ln'] == 0: continue
        px = decode_sprite(d, l1, l3, s)
        if px and len(px) == s['w']*s['h']: ok += 1
        else:
            fail += 1
            if fail <= 3: print(f"  FAIL: ({s['g']},{s['n']}) fmt={s['fmt']}")
    print(f"\n=== kfm.sff full validation: {ok} OK, {fail} FAIL ===")

    # 4) kfm720 too
    d, l1, l3, sprs = parse_sff2('chars/kfm720/kfm720.sff')
    ok = fail = 0
    for s in sprs:
        if s['ln'] == 0: continue
        px = decode_sprite(d, l1, l3, s)
        if px and len(px) == s['w']*s['h']: ok += 1
        else: fail += 1
    print(f"=== kfm720.sff full validation: {ok} OK, {fail} FAIL ===")
