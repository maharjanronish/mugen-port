/* util.c — string/file helpers and MUGEN text config iteration */
#include "mugen.h"
#include <errno.h>
#include <stdarg.h>

char *mg_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *d = (char*)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

void mg_str_lower(char *s) { for (; *s; s++) *s = (*s >= 'A' && *s <= 'Z') ? *s + 32 : *s; }

int mg_str_ieq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

char *mg_trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1]==' '||e[-1]=='\t'||e[-1]=='\r'||e[-1]=='\n')) *--e = 0;
    return s;
}

int mg_file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

char *mg_read_file(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        /* surface WHY the open failed — on Android this goes to logcat
         * (ENOENT vs EACCES vs ...), which makes remote diagnosis trivial */
        static int s_nfail = 0;
        if (s_nfail++ < 8) mg_logf("[io] cannot open '%s' (%s)", path, strerror(errno));
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char*)malloc(n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (n > 0 && fread(buf, 1, n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    buf[n] = 0;
    fclose(f);
    if (out_len) *out_len = n;
    return buf;
}

char *mg_path_join(const char *dir, const char *name) {
    if (!dir || !*dir) return mg_strdup(name);
    size_t dl = strlen(dir);
    char *s = (char*)malloc(dl + strlen(name) + 2);
    if (!s) return NULL;
    /* BUGFIX (v0.1.2, was the "GAME DATA MISSING" cause on real devices):
     * the old code tested the *uninitialized* malloc buffer (s[dl-1]) instead
     * of dir[dl-1]. When that heap garbage byte happened to be '/' or '\\',
     * the join emitted "<dir><name>" WITHOUT a separator — so on Android
     * EVERY data file open failed (fonts/chars/stages all empty) and the
     * engine showed its fatal-error box. Undefined behavior: PC builds got
     * lucky (quiet heap -> zero byte -> correct branch), Android (SDL heap
     * full of path strings) hit it every run.
     * New logic: strip any trailing separators (both kinds — Windows accepts
     * '/' too), then append exactly one '/'. */
    size_t end = dl;
    while (end > 0 && (dir[end-1] == '/' || dir[end-1] == '\\')) end--;
    if (end == 0) sprintf(s, "/%s", name);   /* dir was "/", "\\" or "///" */
    else sprintf(s, "%.*s/%s", (int)end, dir, name);
    return s;
}

/* mg_logf: portable logging. Non-Android builds -> stderr.
 * The Android SDL build provides an SDL_Log-backed version in platform_sdl.c
 * (bionic apps have no usable stderr — logs must go through logcat). */
#ifndef __ANDROID__
void mg_logf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
#endif

void mg_path_fix(char *s) { for (; *s; s++) if (*s == '\\') *s = '/'; }

/* ---------------- MgTextIter ---------------- */
void mg_text_open(MgTextIter *it, const char *path) {
    memset(it, 0, sizeof(*it));
    it->text = mg_read_file(path, &it->len);
    if (it->text) {
        /* strip UTF-8 BOM */
        if (it->len >= 3 && (unsigned char)it->text[0]==0xEF && (unsigned char)it->text[1]==0xBB && (unsigned char)it->text[2]==0xBF)
            it->pos = 3;
    }
}

/* Strip a trailing comment from a line (unquoted ';'), in place. */
static void strip_comment(char *line) {
    int inq = 0;
    for (char *p = line; *p; p++) {
        if (*p == '"') inq = !inq;
        else if (*p == ';' && !inq) { *p = 0; break; }
    }
}

int mg_text_next(MgTextIter *it) {
    if (!it->text) return 0;
    char line[1024];
    while (it->pos < it->len) {
        /* read one line */
        long start = it->pos;
        while (it->pos < it->len && it->text[it->pos] != '\n') it->pos++;
        long n = it->pos - start;
        if (it->pos < it->len) it->pos++; /* skip \n */
        if (n >= (long)sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, it->text + start, n);
        line[n] = 0;
        strip_comment(line);
        char *s = mg_trim(line);
        if (!*s) continue;

        if (*s == '[') {
            char *e = strchr(s, ']');
            if (e) {
                *e = 0;
                char *sec = mg_trim(s + 1);
                strncpy(it->section, sec, sizeof(it->section) - 1);
                it->section[sizeof(it->section)-1] = 0;
                it->name[0] = 0; it->value[0] = 0;
                return 1;
            }
        }
        /* name = value */
        char *eq = NULL;
        int inq = 0;
        for (char *p = s; *p; p++) {
            if (*p == '"') inq = !inq;
            else if (*p == '=' && !inq) { eq = p; break; }
        }
        if (eq) {
            *eq = 0;
            char *nm = mg_trim(s);
            char *vl = mg_trim(eq + 1);
            /* strip quotes */
            if (*vl == '"') {
                char *q = strchr(vl + 1, '"');
                if (q) *q = 0;
                vl = vl + 1;
            }
            strncpy(it->name, nm, sizeof(it->name) - 1);
            it->name[sizeof(it->name)-1] = 0;
            strncpy(it->value, vl, sizeof(it->value) - 1);
            it->value[sizeof(it->value)-1] = 0;
        } else {
            strncpy(it->name, s, sizeof(it->name) - 1);
            it->name[sizeof(it->name)-1] = 0;
            it->value[0] = 0;
        }
        return 1;
    }
    return 0;
}

int mg_geti(const char *s, int defval) {
    if (!s || !*s) return defval;
    return (int)strtol(s, NULL, 10);
}

double mg_getf(const char *s, double defval) {
    if (!s || !*s) return defval;
    return strtod(s, NULL);
}

int mg_parse_ints(const char *s, int *out, int maxn) {
    int n = 0;
    const char *p = s;
    while (*p && n < maxn) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        char *end;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        out[n++] = (int)v;
        p = end;
    }
    return n;
}

int mg_parse_floats(const char *s, double *out, int maxn) {
    int n = 0;
    const char *p = s;
    while (*p && n < maxn) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        char *end;
        double v = strtod(p, &end);
        if (end == p) break;
        out[n++] = v;
        p = end;
    }
    return n;
}

char *mg_def_get(const char *text, const char *section, const char *param, char *out, int outlen) {
    out[0] = 0;
    if (!text) return out;
    MgTextIter it;
    memset(&it, 0, sizeof(it));
    it.text = mg_strdup(text);
    it.len = strlen(text);
    int in_sec = 0;
    char sec[128]; strncpy(sec, section, 127); sec[127]=0; mg_str_lower(sec);
    char par[128]; strncpy(par, param, 127); par[127]=0; mg_str_lower(par);
    while (mg_text_next(&it)) {
        char cursec[128]; strncpy(cursec, it.section, 127); cursec[127]=0; mg_str_lower(cursec);
        if (it.section[0]) in_sec = mg_str_ieq(cursec, sec);
        else in_sec = 0;
        if (in_sec && it.name[0]) {
            char nm[128]; strncpy(nm, it.name, 127); nm[127]=0; mg_str_lower(nm);
            if (mg_str_ieq(nm, par)) {
                strncpy(out, it.value, outlen - 1);
                out[outlen-1] = 0;
                free(it.text);
                return out;
            }
        }
    }
    free(it.text);
    return out;
}
