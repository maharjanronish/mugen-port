/* sff.c — SFF sprite file loader (v1 + v2), decoders RE'd from Elecbyte sff2png.exe
 *
 * SFF v2 layout (verified empirically against kfm.sff, stage0.sff, kfm720.sff):
 *   header 512 bytes: sig[12] "ElecbyteSpr\0", ver u32@12 (byte15=2 → v2),
 *     sprite node table u32@36, count u32@40, palette table u32@44 (512),
 *     palette count u32@48, lump1 u32@52 (data section 1), lump2 u32@56 (size),
 *     lump3 u32@60 (data section 2, for sprites with flags&1)
 *   palette node (16B): group u16, number u16, numColors u32, dataOff u32, dataLen u32
 *   sprite node (28B): group u16, number u16, w u16, h u16, ax i16, ay i16,
 *     link u16, format u8, depth u8, dataOff u32, dataLen u32, palId u16, flags u16
 *   formats: 0 raw, 2 RLE8, 3 RLE5, 4 LZ5 (all except raw prefixed by u32 size)
 */
#include "mugen.h"

/* ---------------- decoders ---------------- */

int mg_decode_lz5(const uint8_t *src, int srclen, uint8_t *dst, int dstcap) {
    if (srclen < 4) return -1;
    int n = (int)(src[0] | src[1]<<8 | src[2]<<16 | src[3]<<24);
    const uint8_t *s = src + 4;
    int limit = srclen - 4;
    int si = 0, out = 0;
    uint8_t flagbyte = 0; int flagbits = 0;
    uint8_t roll = 0; int rollcnt = 0;
    while (si < limit && out < n && out < dstcap) {
        if (flagbits == 0) {
            flagbyte = s[si++]; flagbits = 8;
        }
        flagbits--;
        int bit = flagbyte & 1; flagbyte >>= 1;
        if (bit) { /* match */
            if (si >= limit) break;
            uint8_t b = s[si++];
            int len6 = b & 0x3f;
            int ln, dist;
            if (len6 != 0) { /* short match: 2-bit dist in rolled byte */
                uint8_t nb = (uint8_t)((roll << 2) | (b >> 6));
                roll = nb; rollcnt += 2;
                ln = len6 + 1;
                if (rollcnt != 8) {
                    if (si >= limit) break;
                    dist = s[si++];
                } else {
                    dist = nb; rollcnt = 0;
                }
            } else { /* long match: 10-bit dist + len byte */
                if (si + 2 > limit) break;
                int lo = s[si++];
                ln = s[si++] + 3;
                dist = lo | ((b & 0xc0) << 2);
            }
            if (dist + 1 > out || dist < 0) return -1; /* invalid backref */
            for (int i = 0; i < ln && out < n; i++) {
                dst[out] = dst[out - dist - 1];
                out++;
            }
        } else { /* RLE run: 5-bit color */
            if (si >= limit) break;
            uint8_t b = s[si++];
            int color = b & 0x1f;
            int count = b >> 5;
            if (count == 0) {
                if (si >= limit) break;
                count = s[si++] + 8;
            }
            for (int i = 0; i < count && out < n; i++) dst[out++] = (uint8_t)color;
        }
    }
    return (out == n) ? n : -1;
}

int mg_decode_rle8(const uint8_t *src, int srclen, uint8_t *dst, int dstcap) {
    if (srclen < 4) return -1;
    int n = (int)(src[0] | src[1]<<8 | src[2]<<16 | src[3]<<24);
    const uint8_t *s = src + 4;
    int limit = srclen - 4;
    int i = 0, out = 0;
    while (i < limit && out < n && out < dstcap) {
        uint8_t b = s[i++];
        if ((b & 0xc0) == 0x40) {
            if (i >= limit) break;
            uint8_t color = s[i++];
            int count = b & 0x3f;
            if (count == 0) break;
            for (int k = 0; k < count && out < n; k++) dst[out++] = color;
        } else {
            dst[out++] = b;
        }
    }
    return (out >= n) ? n : -1;
}

int mg_decode_rle5(const uint8_t *src, int srclen, uint8_t *dst, int dstcap) {
    if (srclen < 4) return -1;
    int n = (int)(src[0] | src[1]<<8 | src[2]<<16 | src[3]<<24);
    const uint8_t *s = src + 4;
    int limit = srclen - 4;
    int i = 0, out = 0;
    while (i + 1 < limit && out < n && out < dstcap) {
        int count = s[i] + 1;
        int flags = s[i+1];
        i += 2;
        int litcount = flags & 0x7f;
        int color = 0;
        if (flags & 0x80) {
            if (i >= limit) break;
            color = s[i++];
        }
        for (int k = 0; k < count && out < n; k++) dst[out++] = (uint8_t)color;
        if (litcount > 0) {
            for (int k = 0; k < litcount && out < n; k++) {
                if (i >= limit) break;
                dst[out++] = s[i++];
            }
        }
    }
    return (out >= n) ? n : -1;
}

/* raw PNG decode for png8/png24/png32 formats is not needed for the default
 * MUGEN 1.1b1 data set (all sprites use rle8/lz5); if encountered we fail
 * gracefully and the sprite renders as a placeholder box. */

/* ---------------- SFF v1 (classic, PCX-based) ---------------- */
/* rd32_v1 forward */
static uint32_t rd32_v1(const uint8_t *d, int off);

static uint8_t *decode_pcx(const uint8_t *d, long len, int *w, int *h, uint8_t pal[768]) {
    if (len < 128) return NULL;
    *w = d[4] | d[5]<<8;
    int y2 = d[8] | d[9]<<8;
    *h = y2 + 1;
    int bpp = d[65];
    if (bpp != 8) return NULL;
    int bytes_per_line = d[66] | d[67]<<8;
    uint8_t *out = (uint8_t*)malloc((*w) * (*h));
    if (!out) return NULL;
    const uint8_t *p = d + 128;
    const uint8_t *end = d + len - 769;
    for (int row = 0; row < *h; row++) {
        int x = 0;
        while (x < bytes_per_line && p < end) {
            uint8_t b = *p++;
            if (b >= 0xc0) {
                int cnt = b & 0x3f;
                uint8_t val = *p++;
                for (int k = 0; k < cnt && x < bytes_per_line; k++) {
                    if (x < *w) out[row * (*w) + x] = val;
                    x++;
                }
            } else {
                if (x < *w) out[row * (*w) + x] = b;
                x++;
            }
        }
    }
    /* palette: last 769 bytes: 0x0c + 768 rgb */
    if (len >= 769 && d[len-769] == 0x0c) {
        memcpy(pal, d + len - 768, 768);
    }
    return out;
}

static MgSff *sff_load_v1(const uint8_t *d, long len, int siglen) {
    MgSff *sff = (MgSff*)calloc(1, sizeof(MgSff));
    sff->palettes = (uint8_t(*)[1024])calloc(1, 1024);
    sff->npalettes = 1;
    uint8_t *pal = sff->palettes[0];
    /* v1 header: sig[siglen] + ver(4) + ngroups(4) + nimages(4) + suboff(4) + paloff(4) */
    int base = siglen + 4;   /* after version bytes */
    uint32_t suboff = rd32_v1(d, base + 8);
    int nimages = (int)rd32_v1(d, base + 4);
    /* iterate linked list of subheaders */
    int cap = 32;
    sff->sprites = (MgSprite*)malloc(cap * sizeof(MgSprite));
    uint32_t off = suboff;
    int guard = 0;
    uint8_t lastpal[768]; memset(lastpal, 0, 768);
    while (off && off + 32 <= (uint32_t)len && guard++ < nimages + 16) {
        uint32_t next = d[off] | d[off+1]<<8 | d[off+2]<<16 | (uint32_t)d[off+3]<<24;
        uint32_t dlen = d[off+4] | d[off+5]<<8 | d[off+6]<<16 | (uint32_t)d[off+7]<<24;
        int ax = (int16_t)(d[off+8] | d[off+9]<<8);
        int ay = (int16_t)(d[off+10] | d[off+11]<<8);
        int group = d[off+12] | d[off+13]<<8;
        int number = d[off+14] | d[off+15]<<8;
        int linked = d[off+20] | d[off+21]<<8;
        int sharedpal = d[off+22];
        if (sff->nsprites >= cap) {
            cap *= 2;
            sff->sprites = (MgSprite*)realloc(sff->sprites, cap * sizeof(MgSprite));
        }
        MgSprite *sp = &sff->sprites[sff->nsprites];
        memset(sp, 0, sizeof(*sp));
        sp->group = (int16_t)group; sp->number = (int16_t)number;
        sp->axis_x = (int16_t)ax; sp->axis_y = (int16_t)ay;
        if (linked) {
            /* linked sprite: copy image from link index */
            if (linked >= 0 && linked < sff->nsprites) {
                MgSprite *src2 = &sff->sprites[linked];
                sp->w = src2->w; sp->h = src2->h;
                if (src2->rgba) {
                    sp->rgba = (uint32_t*)malloc(src2->w * src2->h * 4);
                    memcpy(sp->rgba, src2->rgba, src2->w * src2->h * 4);
                }
            }
        } else if (dlen > 0 && off + 32 + dlen <= (uint32_t)len) {
            uint8_t palbuf[768];
            int w = 0, hgt = 0;
            uint8_t *idx = decode_pcx(d + off + 32, dlen, &w, &hgt, palbuf);
            if (idx) {
                if (!sharedpal || !pal[0]) { memcpy(pal, palbuf, 768); memcpy(lastpal, palbuf, 768); }
                sp->w = w; sp->h = hgt;
                sp->rgba = (uint32_t*)malloc(w * hgt * 4);
                for (int i = 0; i < w * hgt; i++) {
                    int ci = idx[i];
                    uint8_t r = pal[ci*3], g = pal[ci*3+1], b = pal[ci*3+2];
                    sp->rgba[i] = (ci == 0) ? 0 : ((uint32_t)r<<24 | (uint32_t)g<<16 | (uint32_t)b<<8 | 0xFF);
                }
                free(idx);
            }
        }
        sff->nsprites++;
        off = next;
    }
    return sff;
}

static uint32_t rd32_v1(const uint8_t *d, int off) {
    return d[off] | d[off+1]<<8 | d[off+2]<<16 | (uint32_t)d[off+3]<<24;
}

/* ---------------- SFF v2 ---------------- */
static uint32_t rd32(const uint8_t *d, int off) {
    return d[off] | d[off+1]<<8 | d[off+2]<<16 | (uint32_t)d[off+3]<<24;
}

static MgSff *sff_load_v2(const uint8_t *d, long len, int font_mode) {
    uint32_t spr_tbl = rd32(d, 36);
    uint32_t n_spr = rd32(d, 40);
    uint32_t pal_tbl = rd32(d, 44);
    uint32_t n_pal = rd32(d, 48);
    uint32_t lump1 = rd32(d, 52);
    uint32_t lump3 = rd32(d, 60);

    MgSff *sff = (MgSff*)calloc(1, sizeof(MgSff));
    sff->npalettes = (int)n_pal;
    sff->palettes = (uint8_t(*)[1024])calloc(n_pal ? n_pal : 1, 1024);

    /* palettes: (group, number, numColors, dataOff, dataLen) relative to lump1 */
    for (uint32_t i = 0; i < n_pal; i++) {
        int o = (int)pal_tbl + (int)i * 16;
        if (o + 16 > len) break;
        uint32_t nc = rd32(d, o+4), doff = rd32(d, o+8), dlen = rd32(d, o+12);
        if (lump1 + doff + dlen > (uint32_t)len) continue;
        const uint8_t *pd = d + lump1 + doff;
        for (uint32_t c = 0; c < nc && c < 256; c++) {
            uint8_t r = pd[c*4], g = pd[c*4+1], b = pd[c*4+2], a = pd[c*4+3];
            if (font_mode) {
                /* font sff convention: index 0 transparent, all others opaque */
                a = (c == 0) ? 0 : 255;
            } else if (a) a = 255;
            sff->palettes[i][c*4] = r; sff->palettes[i][c*4+1] = g;
            sff->palettes[i][c*4+2] = b; sff->palettes[i][c*4+3] = a;
        }
    }

    sff->sprites = (MgSprite*)calloc(n_spr ? n_spr : 1, sizeof(MgSprite));
    sff->nsprites = (int)n_spr;
    int nfail = 0;
    for (uint32_t i = 0; i < n_spr; i++) {
        int o = (int)spr_tbl + (int)i * 28;
        if (o + 28 > len) break;
        MgSprite *sp = &sff->sprites[i];
        sp->group = (int16_t)(d[o] | d[o+1]<<8);
        sp->number = (int16_t)(d[o+2] | d[o+3]<<8);
        sp->w = d[o+4] | d[o+5]<<8;
        sp->h = d[o+6] | d[o+7]<<8;
        sp->axis_x = (int16_t)(d[o+8] | d[o+9]<<8);
        sp->axis_y = (int16_t)(d[o+10] | d[o+11]<<8);
        uint8_t fmt = d[o+14], depth = d[o+15];
        uint32_t doff = rd32(d, o+16), dlen = rd32(d, o+20);
        uint16_t palid = d[o+24] | d[o+25]<<8;
        uint16_t flags = d[o+26] | d[o+27]<<8;
        if (dlen == 0) continue;

        uint32_t base = (flags & 1) ? lump3 : lump1;
        if (base + doff + dlen > (uint32_t)len) continue;
        const uint8_t *sdata = d + base + doff;
        int npix = sp->w * sp->h;
        if (npix <= 0 || npix > 64*1024*1024) continue;
        uint8_t *idx = (uint8_t*)malloc(npix);
        int got = -1;
        if (fmt == 2) got = mg_decode_rle8(sdata, (int)dlen, idx, npix);
        else if (fmt == 3) got = mg_decode_rle5(sdata, (int)dlen, idx, npix);
        else if (fmt == 4) got = mg_decode_lz5(sdata, (int)dlen, idx, npix);
        else if (fmt == 0 || fmt == 1) { /* raw */
            int n = (int)dlen; if (n > npix) n = npix;
            memcpy(idx, sdata, n); got = n;
        }
        if (got < 0) { nfail++; free(idx); continue; }
        /* apply palette: 5-bit indices index palette directly (32-entry palettes) */
        uint8_t *pal = (palid < n_pal) ? sff->palettes[palid] : sff->palettes[0];
        int palmax = (depth <= 5) ? 32 : 256;
        sp->rgba = (uint32_t*)malloc(npix * 4);
        for (int p2 = 0; p2 < npix; p2++) {
            int ci = idx[p2];
            if (ci >= palmax) ci = ci & (palmax - 1);
            uint8_t r = pal[ci*4], g = pal[ci*4+1], b = pal[ci*4+2], a = pal[ci*4+3];
            if (a) a = 255;
            sp->rgba[p2] = ((uint32_t)r<<24 | (uint32_t)g<<16 | (uint32_t)b<<8 | a);
        }
        free(idx);
    }
    if (nfail) fprintf(stderr, "[sff] %d sprites failed to decode in %s\n", nfail, "sff2");
    return sff;
}

MgSff *mg_sff_load(const char *path) {
    long len;
    uint8_t *d = (uint8_t*)mg_read_file(path, &len);
    if (!d) { fprintf(stderr, "[sff] cannot open %s\n", path); return NULL; }
    MgSff *sff = NULL;
    if (len > 512 && memcmp(d, "ElecbyteSpr\0", 12) == 0) {
        if (d[15] == 2) sff = sff_load_v2(d, len, 0);
        else sff = sff_load_v1(d, len, 12);   /* v1.01: 12-byte sig + 4 ver bytes */
    } else if (len > 512 && memcmp(d, "ElecbyteSprite\0", 15) == 0) {
        sff = sff_load_v1(d, len, 15);        /* classic v1.01 15-byte sig */
    } else {
        fprintf(stderr, "[sff] unknown format: %s\n", path);
    }
    free(d);
    return sff;
}

MgSff *mg_sff_load_font(const char *path) {
    long len;
    uint8_t *d = (uint8_t*)mg_read_file(path, &len);
    if (!d) { fprintf(stderr, "[sff] cannot open %s\n", path); return NULL; }
    MgSff *sff = NULL;
    if (len > 512 && memcmp(d, "ElecbyteSpr\0", 12) == 0 && d[15] == 2)
        sff = sff_load_v2(d, len, 1);
    else
        sff = mg_sff_load(path);   /* fallback */
    if (d) free(d);
    return sff;
}

MgSprite *mg_sff_get(MgSff *sff, int group, int number) {
    if (!sff) return NULL;
    for (int i = 0; i < sff->nsprites; i++) {
        if (sff->sprites[i].group == group && sff->sprites[i].number == number)
            return &sff->sprites[i];
    }
    return NULL;
}

void mg_act_load(const char *path, uint8_t out[768]) {
    memset(out, 0, 768);
    long len; uint8_t *d = (uint8_t*)mg_read_file(path, &len);
    if (!d) return;
    if (len >= 768) memcpy(out, d, 768);
    free(d);
}
