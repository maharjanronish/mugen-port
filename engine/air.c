/* air.c — .air animation parser + .snd sound bank + fonts */
#include "mugen.h"

/* ---------------- AIR ---------------- */
MgAir *mg_air_load(const char *path) {
    MgTextIter it;
    mg_text_open(&it, path);
    if (!it.text) { fprintf(stderr, "[air] cannot open %s\n", path); return NULL; }

    MgAir *air = (MgAir*)calloc(1, sizeof(MgAir));
    MgAnim *cur = NULL;
    int cap = 64;
    air->anims = (MgAnim*)malloc(cap * sizeof(MgAnim));

    /* clsn default state */
    MgClsn dclsn1[16], dclsn2[16]; int nd1 = 0, nd2 = 0;
    MgClsn tclsn1[16], tclsn2[16]; int nt1 = 0, nt2 = 0;  /* per-frame override */
    int clsn_default_valid = 0;

    char line[512];
    while (mg_text_next(&it)) {
        if (it.name[0] == 0) {
            /* section or bare line like "Clsn2Default: 2" */
            if (strncmp(it.section, "Begin Action", 12) == 0) {
                int num = atoi(it.section + 12);
                if (air->nanims >= cap) {
                    cap *= 2;
                    air->anims = (MgAnim*)realloc(air->anims, cap * sizeof(MgAnim));
                }
                cur = &air->anims[air->nanims++];
                memset(cur, 0, sizeof(*cur));
                cur->number = num;
                nd1 = nd2 = 0; clsn_default_valid = 0;
                cur->frames = NULL; cur->nframes = 0;
                int fcap = 16;
                cur->frames = (MgAnimFrame*)malloc(fcap * sizeof(MgAnimFrame));
                cur->loopstart = 0;
            }
            continue;
        }
        /* param line */
        if (!cur) continue;
        strncpy(line, it.name, 100); line[100] = 0;
        mg_str_lower(line);
        char *vl = it.value;

        if (strncmp(line, "clsn2default:", 13) == 0 || strncmp(line, "clsn1default:", 13) == 0) {
            char *colon = strchr(line, ':');
            int n = colon ? atoi(colon + 1) : 0;
            int is1 = (line[4] == '1');
            int *cnt = is1 ? &nd1 : &nd2;
            MgClsn *arr = is1 ? dclsn1 : dclsn2;
            *cnt = 0;
            for (int i = 0; i < n && i < 16; i++) {
                if (!mg_text_next(&it)) break;
                int v[4];
                if (sscanf(it.value, "%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3]) == 4) {
                    arr[*cnt].x1 = v[0]; arr[*cnt].y1 = v[1];
                    arr[*cnt].x2 = v[2]; arr[*cnt].y2 = v[3];
                    (*cnt)++;
                }
            }
            clsn_default_valid = 1;
        }
        else if (strncmp(line, "clsn2:", 6) == 0 || strncmp(line, "clsn1:", 6) == 0) {
            char *colon = strchr(line, ':');
            int n = colon ? atoi(colon + 1) : 0;
            int is1 = (line[4] == '1');
            int *cnt = is1 ? &nt1 : &nt2;
            MgClsn *arr = is1 ? tclsn1 : tclsn2;
            *cnt = 0;
            for (int i = 0; i < n && i < 16; i++) {
                if (!mg_text_next(&it)) break;
                int v[4];
                if (sscanf(it.value, "%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3]) == 4) {
                    arr[*cnt].x1 = v[0]; arr[*cnt].y1 = v[1];
                    arr[*cnt].x2 = v[2]; arr[*cnt].y2 = v[3];
                    (*cnt)++;
                }
            }
        }
        else if (strncmp(line, "loopstart", 9) == 0) {
            cur->loopstart = cur->nframes;
        }
        else if (strncmp(line, "a", 1) == 0 && (line[1] == 0 || strstr(line, "add"))) {
            /* Aadd / A1 etc: alpha blending */
        }
        else {
            /* frame: "group,number, xoff,yoff, ticks[, flip]" */
            int v[5] = {0,0,0,0,0};
            int n = mg_parse_ints(it.name, v, 5);
            /* ticks may be in value (rare) — normally 5 fields on the line */
            if (n >= 5) {
                MgAnimFrame f;
                memset(&f, 0, sizeof(f));
                f.group = v[0]; f.number = v[1];
                f.xoff = (int16_t)v[2]; f.yoff = (int16_t)v[3];
                f.ticks = v[4];
                f.alpha_src = -1; f.alpha_dst = -1;
                /* flip flags: H, V, HV after 5th field */
                char *fl = it.name;
                for (int q = 0; q < 5; q++) { while (*fl && *fl != ',') fl++; if (*fl) fl++; }
                if (fl && strstr(fl, "H")) f.flip_h = 1;
                if (fl && strstr(fl, "V")) f.flip_v = 1;
                /* Aadd alpha: "A" with src,dst params — keep -1 default */
                /* per-frame clsn override */
                if (nt1 > 8) nt1 = 8;
                if (nt2 > 8) nt2 = 8;
                if (nd1 > 16) nd1 = 16;
                if (nd2 > 16) nd2 = 16;
                if (nt1 > 0) { memcpy(f.clsn1, tclsn1, nt1 * sizeof(MgClsn)); f.nclsn1 = nt1; }
                if (nt2 > 0) { memcpy(f.clsn2, tclsn2, nt2 * sizeof(MgClsn)); f.nclsn2 = nt2; }
                if (f.nclsn1 == 0 && nd1 > 0 && clsn_default_valid) {
                    memcpy(f.clsn1, dclsn1, nd1 * sizeof(MgClsn)); f.nclsn1 = nd1;
                }
                if (f.nclsn2 == 0 && nd2 > 0 && clsn_default_valid) {
                    memcpy(f.clsn2, dclsn2, nd2 * sizeof(MgClsn)); f.nclsn2 = nd2;
                }
                /* append */
                int fi = cur->nframes;
                /* frames array grows: realloc in blocks of 16 */
                if (fi > 0 && (fi & 15) == 0) {
                    cur->frames = (MgAnimFrame*)realloc(cur->frames, (fi + 16) * sizeof(MgAnimFrame));
                }
                cur->frames[fi] = f;
                cur->nframes++;
                cur->total_ticks += (f.ticks <= 0 ? 1 : f.ticks);
                nt1 = nt2 = 0;  /* per-frame clsn reset */
            }
        }
    }
    free(it.text);
    return air;
}

MgAnim *mg_air_get(MgAir *air, int number) {
    if (!air) return NULL;
    for (int i = 0; i < air->nanims; i++)
        if (air->anims[i].number == number) return &air->anims[i];
    return NULL;
}

/* ---------------- SND ---------------- */
MgSnd *mg_snd_load(const char *path) {
    long len;
    uint8_t *d = (uint8_t*)mg_read_file(path, &len);
    if (!d) { fprintf(stderr, "[snd] cannot open %s\n", path); return NULL; }
    if (len < 20 || memcmp(d, "ElecbyteSnd\0", 12) != 0) {
        fprintf(stderr, "[snd] bad format: %s\n", path);
        free(d); return NULL;
    }
    uint32_t ver = d[12] | d[13]<<8 | d[14]<<16 | (uint32_t)d[15]<<24;
    uint32_t n = d[16] | d[17]<<8 | d[18]<<16 | (uint32_t)d[19]<<24;
    (void)ver;
    MgSnd *snd = (MgSnd*)calloc(1, sizeof(MgSnd));
    snd->entries = (MgSndEntry*)calloc(n ? n : 1, sizeof(MgSndEntry));
    for (uint32_t i = 0; i < n; i++) {
        int o = 20 + (int)i * 16;
        if (o + 16 > len) break;
        snd->entries[i].group = d[o] | d[o+1]<<8;
        snd->entries[i].number = d[o+2] | d[o+3]<<8;
        uint32_t off = d[o+4] | d[o+5]<<8 | d[o+6]<<16 | (uint32_t)d[o+7]<<24;
        uint32_t ln = d[o+8] | d[o+9]<<8 | d[o+10]<<16 | (uint32_t)d[o+11]<<24;
        if (off + ln > (uint32_t)len || ln == 0) continue;
        snd->entries[i].data = (uint8_t*)malloc(ln);
        memcpy(snd->entries[i].data, d + off, ln);
        snd->entries[i].len = (int)ln;
        snd->nentries++;
    }
    free(d);
    return snd;
}

MgSndEntry *mg_snd_get(MgSnd *snd, int group, int number) {
    if (!snd) return NULL;
    for (int i = 0; i < snd->nentries; i++)
        if (snd->entries[i].group == group && snd->entries[i].number == number)
            return &snd->entries[i];
    return NULL;
}

/* ---------------- fonts ---------------- */
/* FNT v0: "ElecbyteFnt\0" + ver(2+2) + numchars(4) + index (bytes: char, w, h, off) */
static MgFont *font_load_v0(const uint8_t *d, long len) {
    if (len < 32 || memcmp(d, "ElecbyteFnt\0", 12) != 0) return NULL;
    MgFont *f = (MgFont*)calloc(1, sizeof(MgFont));
    uint32_t nchars = d[16] | d[17]<<8 | d[18]<<16 | (uint32_t)d[19]<<24;
    int coff = 20 + (int)nchars * 4;   /* offset data start */
    for (uint32_t i = 0; i < nchars; i++) {
        int o = 20 + (int)i * 4;
        if (o + 4 > len) break;
        uint8_t ch = d[o], cw = d[o+1], chh = d[o+2];
        uint8_t off = d[o+3];
        if (ch >= 256) continue;
        if (cw == 0 || chh == 0) continue;
        int bytes = ((cw + 7) / 8) * chh;
        int src = coff + (int)off * 4;
        if (src + bytes > len) continue;
        /* 1-bit-per-pixel glyph, rows padded to bytes; color = white (tinted at render) */
        f->gw[ch] = cw; f->gh[ch] = chh;
        f->glyphs[ch] = (uint32_t*)malloc(cw * chh * 4);
        for (int y = 0; y < chh; y++) {
            for (int x = 0; x < cw; x++) {
                int bit = (d[src + y * ((cw + 7) / 8) + (x >> 3)] >> (7 - (x & 7))) & 1;
                f->glyphs[ch][y * cw + x] = bit ? 0xFFFFFFFF : 0;
            }
        }
    }
    f->size_w = f->size_h = 0;
    f->spacing_x = 0; f->spacing_y = 0; f->offset_x = 0; f->offset_y = 0;
    return f;
}

MgFont *mg_font_load(const char *dir, const char *name) {
    /* try .def (FNT v2: bitmap glyphs in sff) then legacy .fnt */
    char *defpath = mg_path_join(dir, name);
    /* if name has no extension, append .def */
    char full[600];
    const char *ext = strrchr(name, '.');
    if (!ext) snprintf(full, sizeof(full), "%s/%s.def", dir, name);
    else snprintf(full, sizeof(full), "%s/%s", dir, name);
    free(defpath);

    /* FNT v2 def file? */
    if (strstr(full, ".def")) {
        MgTextIter it;
        mg_text_open(&it, full);
        if (it.text) {
            char type[32] = "", file[256] = "";
            int size[2] = {0,0}, spacing[2] = {0,0}, offset[2] = {0,0};
            while (mg_text_next(&it)) {
                if (mg_str_ieq(it.section, "Def")) {
                    if (mg_str_ieq(it.name, "type")) strncpy(type, it.value, 31);
                    if (mg_str_ieq(it.name, "size")) mg_parse_ints(it.value, size, 2);
                    if (mg_str_ieq(it.name, "spacing")) mg_parse_ints(it.value, spacing, 2);
                    if (mg_str_ieq(it.name, "offset")) mg_parse_ints(it.value, offset, 2);
                }
                if (mg_str_ieq(it.section, "Files") || mg_str_ieq(it.section, "Def")) {
                    if (mg_str_ieq(it.name, "file")) strncpy(file, it.value, 255);
                }
            }
            free(it.text);
            if (type[0] && (mg_str_ieq(type, "bitmap") || mg_str_ieq(type, "ttf") || 1)) {
                if (file[0] && strstr(file, ".sff")) {
                    /* FNT v2: glyphs in sff, sprite number = char code, group = bank */
                    char *sffpath = mg_path_join(dir, file);
                    MgSff *sff = mg_sff_load_font(sffpath);
                    free(sffpath);
                    if (sff) {
                        MgFont *f = (MgFont*)calloc(1, sizeof(MgFont));
                        f->size_w = size[0]; f->size_h = size[1];
                        f->spacing_x = spacing[0]; f->spacing_y = spacing[1];
                        f->offset_x = offset[0]; f->offset_y = offset[1];
                        for (int g = 0; g < sff->nsprites; g++) {
                            MgSprite *s = &sff->sprites[g];
                            if (s->group != 0) continue;
                            int ch = s->number;
                            if (ch >= 0 && ch < 256) {
                                f->gw[ch] = s->w; f->gh[ch] = s->h;
                                f->glyphs[ch] = (uint32_t*)malloc(s->w * s->h * 4);
                                /* convert to white-on-transparent (tint at render) */
                                for (int p = 0; p < s->w * s->h; p++) {
                                    uint32_t px = s->rgba[p];
                                    uint8_t a = px & 0xFF;
                                    f->glyphs[ch][p] = 0xFFFFFF00u | a;
                                }
                            }
                        }
                        /* keep sff alive? glyphs copied; free */
                        for (int i = 0; i < sff->nsprites; i++) free(sff->sprites[i].rgba);
                        free(sff->sprites); free(sff->palettes); free(sff);
                        if (f->size_w == 0) { f->size_w = 8; }
                        return f;
                    }
                }
            }
        }
    }
    /* legacy .fnt */
    char fntpath[600];
    if (ext) snprintf(fntpath, sizeof(fntpath), "%s/%s", dir, name);
    else snprintf(fntpath, sizeof(fntpath), "%s/%s.fnt", dir, name);
    long len; uint8_t *d = (uint8_t*)mg_read_file(fntpath, &len);
    if (d) {
        MgFont *f = font_load_v0(d, len);
        free(d);
        if (f) return f;
    }
    fprintf(stderr, "[font] cannot load %s\n", full);
    return NULL;
}
