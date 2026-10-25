/* platform_sdl.c — SDL2 platform for Android (and desktop Linux testing)
 * Renders via SDL2, multitouch virtual gamepad, WAV playback.
 */
#include "mugen.h"
#include <SDL.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>

#ifdef __ANDROID__
/* Engine diagnostics -> logcat (tag "SDL/APP"). Bionic apps have no usable
 * stderr, so util.c skips its fprintf version of mg_logf on Android. */
void mg_logf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    SDL_Log("%s", buf);
}
#endif

#define MG_PORT_VERSION "0.2.1"

static SDL_Window *g_win = NULL;
static SDL_Renderer *g_ren = NULL;
static int g_fbw = 1280, g_fbh = 720;
static SDL_Texture *g_target = NULL;

/* ---------------- built-in 5x7 font (status / error screens) ----------------
 * Independent of game data: must render even when the mugen/ tree is missing
 * so failures are never an invisible black screen. */
static const uint8_t FONT5X7[128][7] = {
    ['A'] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['B'] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    ['C'] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    ['D'] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    ['E'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    ['F'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    ['G'] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    ['H'] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['I'] = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    ['J'] = {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    ['K'] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    ['L'] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    ['M'] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    ['N'] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    ['O'] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['P'] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    ['Q'] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    ['R'] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    ['S'] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
    ['T'] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    ['U'] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['V'] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    ['W'] = {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    ['X'] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    ['Y'] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    ['Z'] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    ['0'] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    ['1'] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    ['2'] = {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
    ['3'] = {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    ['4'] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    ['5'] = {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    ['6'] = {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    ['7'] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    ['8'] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    ['9'] = {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    [' '] = {0,0,0,0,0,0,0},
    ['.'] = {0,0,0,0,0,0x0C,0x0C},
    [','] = {0,0,0,0,0x0C,0x04,0x08},
    ['-'] = {0,0,0,0x1F,0,0,0},
    ['_'] = {0,0,0,0,0,0,0x1F},
    [':'] = {0,0x06,0x06,0,0x06,0x06,0},
    ['/'] = {0x01,0x01,0x02,0x04,0x08,0x10,0x10},
    ['!'] = {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    ['?'] = {0x0E,0x11,0x01,0x06,0x04,0x00,0x04},
    ['+'] = {0,0x04,0x04,0x1F,0x04,0x04,0},
    ['('] = {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    [')'] = {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
    ['\''] = {0x04,0x04,0x08,0,0,0,0},
};

static int builtin_text_w(const char *s, int scale) {
    int w = 0;
    for (; *s; s++) w += (6 * scale);
    return w - scale;
}

static void draw_builtin_text(const char *s, int x, int y, int scale, uint32_t rgb) {
    uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    int cx = x;
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch < 128) {
            const uint8_t *gl = FONT5X7[ch];
            for (int yy = 0; yy < 7; yy++)
                for (int xx = 0; xx < 5; xx++)
                    if (gl[yy] & (0x10 >> xx))
                        mg_fill_rect(cx + xx * scale, y + yy * scale, scale, scale, r, g, b, 255);
        }
        cx += 6 * scale;
    }
}

static void draw_builtin_text_centered(const char *s, int y, int scale, uint32_t rgb) {
    draw_builtin_text(s, g_fbw / 2 - builtin_text_w(s, scale) / 2, y, scale, rgb);
}

/* Show a status frame immediately (before engine init) so the app is never a
 * silent black screen while data loads. */
static void present_loading_frame(int dots) {
    SDL_SetRenderDrawColor(g_ren, 12, 16, 24, 255);
    SDL_RenderClear(g_ren);
    draw_builtin_text_centered("M.U.G.E.N ANDROID PORT", g_fbh / 2 - 90, 5, 0xFFB040);
    draw_builtin_text_centered("LOADING, PLEASE WAIT", g_fbh / 2 + 10, 3, 0xE0E0E0);
    char dotsbuf[8] = {0};
    for (int i = 0; i < dots % 4; i++) dotsbuf[i] = '.';
    draw_builtin_text_centered(dotsbuf, g_fbh / 2 + 60, 3, 0x808080);
    SDL_RenderPresent(g_ren);
    /* keep Android happy: pump the event queue during the load phase */
    SDL_Event e;
    while (SDL_PollEvent(&e)) { if (e.type == SDL_QUIT) g_game.running = 0; }
}

/* Fatal data error: stay visible instead of a black screen. Never returns
 * (until the user quits). */
static void fatal_error_loop(const char *line1, const char *line2, const char *line3) {
    SDL_Log("FATAL: %s | %s | %s", line1, line2, line3);
    int frame = 0;
    while (g_game.running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || e.type == SDL_APP_TERMINATING) g_game.running = 0;
        }
        SDL_SetRenderDrawColor(g_ren, 48, 8, 8, 255);
        SDL_RenderClear(g_ren);
        draw_builtin_text_centered(line1, g_fbh / 2 - 120, 4, 0xFF6060);
        draw_builtin_text_centered(line2, g_fbh / 2 - 20, 3, 0xFFE0E0);
        draw_builtin_text_centered(line3, g_fbh / 2 + 40, 2, 0xC0C0C0);
        if ((frame / 30) % 2 == 0)
            draw_builtin_text_centered("CHECK LOGCAT TAG: SDL/APP", g_fbh / 2 + 130, 2, 0x909090);
        SDL_RenderPresent(g_ren);
        SDL_Delay(1000 / 30);
        frame++;
    }
}


/* ---------------- sprite texture cache ---------------- */
#define TEXCACHE_MAX 2048
typedef struct { MgSprite *spr; SDL_Texture *tex; } TexCache;
static TexCache g_texcache[TEXCACHE_MAX];
static int g_ntexcache = 0;

/* drop every cached texture (called when the engine re-inits with a new
 * game data folder — old MgSprite pointers become unreachable) */
static void reset_texcache(void) {
    for (int i = 0; i < g_ntexcache; i++)
        if (g_texcache[i].tex) SDL_DestroyTexture(g_texcache[i].tex);
    g_ntexcache = 0;
}

static SDL_Texture *spr_texture(MgSprite *spr) {
    for (int i = 0; i < g_ntexcache; i++)
        if (g_texcache[i].spr == spr) return g_texcache[i].tex;
    if (!spr || !spr->rgba) return NULL;
    if (g_ntexcache >= TEXCACHE_MAX) {
        /* flush half */
        for (int i = 0; i < TEXCACHE_MAX/2; i++) SDL_DestroyTexture(g_texcache[i].tex);
        memmove(g_texcache, g_texcache + TEXCACHE_MAX/2, sizeof(TexCache) * TEXCACHE_MAX/2);
        g_ntexcache -= TEXCACHE_MAX/2;
    }
    SDL_Texture *tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_STATIC, spr->w, spr->h);
    if (!tex) return NULL;
    /* our RGBA is r<<24|g<<16|b<<8|a — as a 32-bit value that is exactly
     * SDL_PIXELFORMAT_RGBA8888 (R in the MSB, A in the LSB). ABGR8888 was
     * wrong: it pulled R from the LOW byte, so every sprite rendered with
     * the alpha byte as red and green/blue swapped ("corrupted red/green"
     * colors on device). Verified via render round-trip in the e2e test. */
    SDL_UpdateTexture(tex, NULL, spr->rgba, spr->w * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    g_texcache[g_ntexcache].spr = spr;
    g_texcache[g_ntexcache].tex = tex;
    g_ntexcache++;
    return tex;
}

/* ---------------- render hooks ---------------- */
void mg_render_sprite(MgSprite *spr, int dx, int dy, int dw, int dh,
                      int flip_h, int flip_v, int alpha_src, int alpha_dst) {
    SDL_Texture *tex = spr_texture(spr);
    if (!tex) return;
    SDL_Rect dst = {dx, dy, dw > 0 ? dw : spr->w, dh > 0 ? dh : spr->h};
    int flip = (flip_h ? SDL_FLIP_HORIZONTAL : 0) | (flip_v ? SDL_FLIP_VERTICAL : 0);
    if (alpha_src >= 0 && alpha_dst >= 0 && alpha_src + alpha_dst < 510) {
        Uint8 alpha = (Uint8)(alpha_src * 255 / (alpha_src + alpha_dst > 0 ? alpha_src + alpha_dst : 1));
        SDL_SetTextureAlphaMod(tex, alpha);
    } else {
        SDL_SetTextureAlphaMod(tex, 255);
    }
    SDL_RenderCopyEx(g_ren, tex, NULL, &dst, 0, NULL, (SDL_RendererFlip)flip);
    SDL_SetTextureAlphaMod(tex, 255);
}

void mg_fill_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_ren, r, g, b, a);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(g_ren, &rc);
}

void mg_render_text(MgFont *f, const char *text, int x, int y, int scale, uint32_t rgb) {
    if (!f) return;
    uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    int cx = x;
    for (const char *p = text; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        uint32_t *glyph = f->glyphs[ch];
        int gw = f->gw[ch], gh = f->gh[ch];
        if (!glyph || !gw) {
            cx += (f->size_w ? f->size_w : 6) * scale + (f->spacing_x ? f->spacing_x : 1) * scale;
            continue;
        }
        for (int yy = 0; yy < gh; yy++)
            for (int xx = 0; xx < gw; xx++) {
                uint32_t px = glyph[yy * gw + xx];
                if (!(px & 0xFF)) continue;
                mg_fill_rect(cx + xx * scale, y + yy * scale, scale, scale, r, g, b, 255);
            }
        cx += gw * scale + (f->spacing_x ? f->spacing_x : 1) * scale;
    }
}

/* ---------------- audio ---------------- */
#define MIX_CHANNELS 16
static SDL_AudioDeviceID g_audiodev = 0;
static SDL_AudioSpec g_audiospec;
typedef struct {
    uint8_t *data; uint32_t len, pos;
    int loop, active;
    int volume;
} MixChan;
static MixChan g_mix[MIX_CHANNELS];

static void audio_cb(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    memset(stream, 0, len);
    SDL_LockAudioDevice(g_audiodev);
    for (int c = 0; c < MIX_CHANNELS; c++) {
        MixChan *ch = &g_mix[c];
        if (!ch->active) continue;
        for (int i = 0; i < len; i += 2) {
            if (ch->pos >= ch->len) {
                if (ch->loop) ch->pos = 0;
                else { ch->active = 0; break; }
            }
            int16_t s = *(int16_t *)(ch->data + ch->pos);
            int16_t *out = (int16_t *)(stream + i);
            *out = (int16_t) (*out + (s >> 2));
            ch->pos += 2;
        }
    }
    SDL_UnlockAudioDevice(g_audiodev);
}

/* find WAV data payload (skip RIFF headers) */
static uint8_t *wav_payload(uint8_t *data, int len, int *out_len, SDL_AudioFormat *fmt, int *channels, int *rate) {
    if (len < 44 || memcmp(data, "RIFF", 4) != 0) return NULL;
    uint8_t *p = data + 12;
    uint8_t *end = data + len;
    while (p + 8 <= end) {
        uint32_t sz = p[4] | p[5]<<8 | p[6]<<16 | (uint32_t)p[7]<<24;
        if (memcmp(p, "fmt ", 4) == 0) {
            *fmt = (SDL_AudioFormat)(p[8] | p[9]<<8);
            *channels = p[10] | p[11]<<8;
            *rate = p[12] | p[13]<<8 | p[14]<<16 | p[15]<<24;
        } else if (memcmp(p, "data", 4) == 0) {
            *out_len = (int)sz;
            return p + 8;
        }
        p += 8 + sz + (sz & 1);
    }
    return NULL;
}

void mg_play_wav(uint8_t *data, int len, int channel, int loop) {
    if (!g_audiodev) return;
    int plen; SDL_AudioFormat fmt; int ch, rate;
    uint8_t *payload = wav_payload(data, len, &plen, &fmt, &ch, &rate);
    if (!payload) return;
    if (channel < 0) {
        /* find free channel */
        for (int i = 1; i < MIX_CHANNELS; i++)
            if (!g_mix[i].active) { channel = i; break; }
        if (channel < 0) channel = 1;
    }
    channel %= MIX_CHANNELS;
    SDL_LockAudioDevice(g_audiodev);
    g_mix[channel].data = payload;
    g_mix[channel].len = plen;
    g_mix[channel].pos = 0;
    g_mix[channel].loop = loop;
    g_mix[channel].active = 1;
    SDL_UnlockAudioDevice(g_audiodev);
    (void)fmt; (void)ch; (void)rate;
}

void mg_play_music(const char *path, int loop) {
    if (!g_audiodev) return;
    long len; uint8_t *d = (uint8_t*)mg_read_file(path, &len);
    if (!d) return;
    /* MVP: WAV music only (ogg/mp3 in roadmap) */
    mg_play_wav(d, (int)len, 0, loop);
}

void mg_stop_channel(int channel) {
    if (!g_audiodev) return;
    SDL_LockAudioDevice(g_audiodev);
    g_mix[channel % MIX_CHANNELS].active = 0;
    SDL_UnlockAudioDevice(g_audiodev);
}

void mg_platform_vibrate(int ms) {
    SDL_Joystick *joy = NULL; (void)joy;
    SDL_Log("vibrate %dms", ms); (void)ms;
}

/* ---------------- input: keyboard + touch ---------------- */
static uint32_t g_keybuttons[2] = {0, 0};
static uint32_t g_touchbuttons = 0;

/* touch layout (normalized to g_fbw x g_fbh) */
typedef struct { int x, y, r; uint32_t bit; const char *label; } TouchBtn;
static TouchBtn g_touchbtns[] = {
    /* d-pad left side */
    { 110, 560, 64, BTN_BACK,  "<" },
    { 250, 560, 64, BTN_FWD,   ">" },
    { 180, 450, 64, BTN_U,     "^" },
    { 180, 660, 64, BTN_D,     "v" },
    /* buttons right side: bottom row A B C, top row X Y Z */
    { 1080, 620, 52, BTN_A, "A" },
    { 1170, 620, 52, BTN_B, "B" },
    { 1260, 620, 52, BTN_C, "C" },
    { 1080, 500, 52, BTN_X, "X" },
    { 1170, 500, 52, BTN_Y, "Y" },
    { 1260, 500, 52, BTN_Z, "Z" },
    { 640, 680, 40, BTN_START, "ST" },
};

/* finger -> button binding for multitouch (slots free when btn_idx < 0).
 * fid MOUSE_SLOT marks the single hardware-mouse pointer (a real mouse, not
 * a synthesized touch-mouse event — those are disabled via hint). */
#define MOUSE_SLOT ((SDL_FingerID)-2)
typedef struct { SDL_FingerID fid; int btn_idx; } FingerSlot;
static FingerSlot g_fingers[16];

static int touch_xy(SDL_Event *e, int *x, int *y) {
    if (e->type == SDL_FINGERDOWN || e->type == SDL_FINGERMOTION || e->type == SDL_FINGERUP) {
        int ww, wh;
        SDL_GetWindowSize(g_win, &ww, &wh);
        float gx, gy;
        /* window pixels -> 1280x720 logical coords (letterboxed by SDL).
         * The old code used raw window pixels, so buttons landed in the wrong
         * place on every real device (window is never exactly 1280x720). */
        SDL_RenderWindowToLogical(g_ren, e->tfinger.x * ww, e->tfinger.y * wh, &gx, &gy);
        *x = (int)gx; *y = (int)gy;
        return 1;
    }
    if (e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) {
        float gx, gy;
        SDL_RenderWindowToLogical(g_ren, (float)e->button.x, (float)e->button.y, &gx, &gy);
        *x = (int)gx; *y = (int)gy;
        return 1;
    }
    return 0;
}

/* NEAREST button wins. v0.1.x used first-match with an inflated radius
 * (r*sqrt(2)), and adjacent 52px buttons sit 90px apart — the circles
 * overlapped massively, so B/C/Y/Z presses usually landed on A/X and every
 * button "did the same move". With nearest-wins each tap maps to exactly one
 * button deterministically. */
static int pick_button(int x, int y) {
    int best = -1;
    long best_d2 = 0;
    for (int i = 0; i < (int)(sizeof(g_touchbtns)/sizeof(g_touchbtns[0])); i++) {
        TouchBtn *b = &g_touchbtns[i];
        int dx = x - b->x, dy = y - b->y;
        long d2 = (long)dx*dx + (long)dy*dy;
        long lim = (long)b->r * b->r + (long)b->r * 24;   /* r * ~1.21 */
        if (d2 <= lim && (best < 0 || d2 < best_d2)) { best = i; best_d2 = d2; }
    }
    return best;
}

static void handle_touch(SDL_Event *e) {
    int x, y;
    if (!touch_xy(e, &x, &y)) return;

    if (e->type == SDL_FINGERDOWN || e->type == SDL_MOUSEBUTTONDOWN) {
        /* title-screen DATA button opens the folder picker */
        extern void mg_picker_open(void);
        extern int mg_picker_active(void);
        if (!mg_picker_active() && g_game.game_phase == 0) {
            int dx = x - 1210, dy = y - 46;
            if ((long)dx*dx + (long)dy*dy <= 52L*52L) { mg_picker_open(); return; }
        }
        int hit = pick_button(x, y);
        if (hit >= 0) {
            g_touchbuttons |= g_touchbtns[hit].bit;
            for (int f = 0; f < 16; f++)
                if (g_fingers[f].btn_idx < 0) {
                    g_fingers[f].fid = (e->type == SDL_MOUSEBUTTONDOWN) ? MOUSE_SLOT : e->tfinger.fingerId;
                    g_fingers[f].btn_idx = hit;
                    break;
                }
        }
    } else if (e->type == SDL_FINGERUP || e->type == SDL_MOUSEBUTTONUP) {
        SDL_FingerID fid = (e->type == SDL_FINGERUP) ? e->tfinger.fingerId : MOUSE_SLOT;
        for (int f = 0; f < 16; f++) {
            if (g_fingers[f].btn_idx >= 0 && g_fingers[f].fid == fid) {
                g_touchbuttons &= ~g_touchbtns[g_fingers[f].btn_idx].bit;
                g_fingers[f].btn_idx = -1;
                g_fingers[f].fid = 0;
            }
        }
    } else if (e->type == SDL_FINGERMOTION) {
        /* v0.2.1: thumb sliding. Without this a finger that landed on the
         * d-pad kept its ORIGINAL direction until lifted — sliding between
         * directions (the normal way people hold a d-pad) did nothing, which
         * felt like "the buttons don't work". Now a tracked finger re-picks
         * its button as it moves: sliding off a button releases it, sliding
         * onto another presses that one. */
        for (int f = 0; f < 16; f++) {
            if (g_fingers[f].btn_idx < 0 || g_fingers[f].fid != e->tfinger.fingerId)
                continue;
            int cur = g_fingers[f].btn_idx;
            int hit = pick_button(x, y);
            if (hit != cur) {
                g_touchbuttons &= ~g_touchbtns[cur].bit;
                if (hit >= 0) {
                    g_touchbuttons |= g_touchbtns[hit].bit;
                    g_fingers[f].btn_idx = hit;
                } else {
                    g_fingers[f].btn_idx = -1;
                    g_fingers[f].fid = 0;
                }
            }
            break;   /* one slot per finger */
        }
    }
}

static void draw_circle(int cx, int cy, int r, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    for (int dy = -r; dy <= r; dy++) {
        int half = (int)(sqrt((double)(r*r - dy*dy)) + 0.5);
        if (half <= 0) half = 1;
        mg_fill_rect(cx - half, cy + dy, half * 2, 1, cr, cg, cb, ca);
    }
}

static void draw_touch_controls(void) {
    for (int i = 0; i < (int)(sizeof(g_touchbtns)/sizeof(g_touchbtns[0])); i++) {
        TouchBtn *b = &g_touchbtns[i];
        uint8_t a = (g_touchbuttons & b->bit) ? 170 : 90;
        draw_circle(b->x, b->y, b->r, 200, 200, 220, a);
        draw_circle(b->x, b->y, b->r - 4, 60, 60, 90, a);
        /* label: game font if loaded, builtin 5x7 otherwise */
        int drawn = 0;
        if (g_game.fonts[0]) {
            int tw = strlen(b->label) * (g_game.fonts[0]->size_w + 1) * 3;
            int th = g_game.fonts[0]->size_h * 3;
            mg_render_text(g_game.fonts[0], b->label, b->x - tw/2, b->y - th/2, 3, 0xE0E0F0);
            drawn = 1;
        }
        if (!drawn)
            draw_builtin_text(b->label, b->x - 2*strlen(b->label)*2, b->y - 3, 2, 0xE0E0F0);
    }
}

int mg_platform_any_button(void) {
    return g_keybuttons[0] || g_touchbuttons;
}

int mg_platform_menu_action(int player, int *delta, int *confirm, int *back) {
    (void)player;
    *delta = 0; *confirm = 0; *back = 0;
    uint32_t b = g_keybuttons[0] | g_touchbuttons;
    static uint32_t prev = 0;
    uint32_t pressed = b & ~prev;
    if (pressed & BTN_FWD) *delta = 1;
    if (pressed & BTN_BACK) *delta = -1;
    if (pressed & (BTN_S | BTN_A | BTN_X)) *confirm = 1;
    prev = b;
    return 1;
}

/* ---------------- game data folder picker ----------------
 * Lets the user point the engine at ANY folder on the device that holds a
 * full MUGEN tree (must contain data/select.def; characters go in chars/).
 * The choice is persisted to <internal>/game_dir.txt and re-used on the
 * next launch. Opened from the DATA button on the title screen. */
static char g_builtin_dir[512] = "";   /* APK-extracted bundled data */
static char g_active_dir[512] = "";    /* what the engine runs on now */
static int  g_picker_on = 0;
static char g_pick_path[512] = "";
static char g_pick_items[192][64];     /* [0] is always ".." (parent) */
static char g_pick_ok[192];            /* looks like a MUGEN root? */
static int  g_pick_n = 0;
static int  g_pick_scroll = 0;
static char g_pick_msg[96] = "";
static int  g_pick_msg_timer = 0;

#define PICK_ROWS 8
#define PICK_ROW_Y 110
#define PICK_ROW_H 46

static const char *storage_root(void) {
#ifdef __ANDROID__
    return "/storage/emulated/0";
#else
    const char *h = getenv("HOME");
    return (h && h[0]) ? h : "/";
#endif
}

static void picker_msg(const char *s) {
    snprintf(g_pick_msg, sizeof(g_pick_msg), "%s", s);
    g_pick_msg_timer = 300;
}

static void settings_path(char *out, int cap) {
#ifdef __ANDROID__
    const char *base = SDL_AndroidGetInternalStoragePath();
    snprintf(out, cap, "%s/game_dir.txt", base ? base : ".");
#else
    const char *h = getenv("HOME");
    snprintf(out, cap, "%s/.mugen_android_gamedir", (h && h[0]) ? h : ".");
#endif
}

/* a valid data folder must contain data/select.def */
static int is_mugen_dir(const char *dir) {
    char p[600];
    snprintf(p, sizeof(p), "%s/data/select.def", dir);
    mg_path_fix(p);
    return mg_file_exists(p);
}

static int cmp_names(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

static void picker_scan(void) {
    g_pick_n = 1;
    strcpy(g_pick_items[0], "..");
    g_pick_ok[0] = 0;
    g_pick_scroll = 0;
    const char *dir = g_pick_path[0] ? g_pick_path : "/";
    DIR *d = opendir(dir);
    if (!d) {
        picker_msg("CANNOT READ FOLDER - GRANT STORAGE PERMISSION");
        return;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL && g_pick_n < 192) {
        const char *nm = de->d_name;
        if (!nm[0] || nm[0] == '.') continue;
        if (strlen(nm) >= 60) continue;
        int isdir = 0;
        if (de->d_type == DT_DIR) isdir = 1;
        else if (de->d_type == DT_UNKNOWN) {
            char fp[700]; struct stat st;
            snprintf(fp, sizeof(fp), "%s/%s", dir, nm);
            isdir = (stat(fp, &st) == 0 && S_ISDIR(st.st_mode));
        }
        if (!isdir) continue;
        snprintf(g_pick_items[g_pick_n], 64, "%s", nm);
        char probe[760];
        snprintf(probe, sizeof(probe), "%s/%s/data/select.def", dir, nm);
        mg_path_fix(probe);
        g_pick_ok[g_pick_n] = mg_file_exists(probe) ? 1 : 0;
        g_pick_n++;
    }
    closedir(d);
    if (g_pick_n > 2) qsort(g_pick_items + 1, (size_t)(g_pick_n - 1), 64, cmp_names);
    if (g_pick_n == 1) picker_msg("NO SUBFOLDERS HERE");
    else g_pick_msg[0] = 0;
}

int mg_picker_active(void) { return g_picker_on; }

void mg_picker_open(void) {
    snprintf(g_pick_path, sizeof(g_pick_path), "%s", storage_root());
    picker_scan();
    g_pick_msg_timer = 0;
    g_picker_on = 1;
    /* release every held button so nothing leaks into the game */
    g_touchbuttons = 0;
    for (int f = 0; f < 16; f++) { g_fingers[f].btn_idx = -1; g_fingers[f].fid = 0; }
}

/* switch the live engine to another data folder */
static void picker_apply_dir(const char *dir) {
    char sp[600];
    settings_path(sp, sizeof(sp));
    FILE *f = fopen(sp, "w");
    if (f) { fprintf(f, "%s\n", dir); fclose(f); }
    reset_texcache();
    mg_stop_channel(0);
    SDL_Log("[picker] switching game dir to: %s", dir);
    Uint32 t0 = SDL_GetTicks();
    mg_engine_init(dir);
    SDL_Log("[picker] re-init done in %dms: %d chars, %d stages",
            (int)(SDL_GetTicks() - t0), g_game.nchars, g_game.nstages);
    snprintf(g_active_dir, sizeof(g_active_dir), "%s", dir);
    g_picker_on = 0;
    g_touchbuttons = 0;
    if (g_game.nchars == 0 || g_game.nstages == 0) {
        char l3[300];
        snprintf(l3, sizeof(l3), "DIR: %s", dir);
        fatal_error_loop("GAME DATA MISSING", "PICK ANOTHER FOLDER", l3);
    }
}

static int pt_in(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void picker_handle_tap(int x, int y) {
    /* folder rows */
    for (int r = 0; r < PICK_ROWS; r++) {
        int idx = g_pick_scroll + r;
        if (idx >= g_pick_n) break;
        if (pt_in(x, y, 60, PICK_ROW_Y + r * PICK_ROW_H, 1160, PICK_ROW_H - 6)) {
            if (idx == 0) {
                /* parent (stop at filesystem root) */
                char *slash = strrchr(g_pick_path, '/');
                if (slash && slash != g_pick_path) *slash = 0;
                else snprintf(g_pick_path, sizeof(g_pick_path), "%s", "/");
                picker_scan();
            } else {
                char next[600];
                const char *sep = (g_pick_path[strlen(g_pick_path)-1] == '/') ? "" : "/";
                snprintf(next, sizeof(next), "%s%s%s", g_pick_path, sep, g_pick_items[idx]);
                if (opendir(next)) {
                    snprintf(g_pick_path, sizeof(g_pick_path), "%s", next);
                    picker_scan();
                } else picker_msg("CANNOT OPEN FOLDER");
            }
            return;
        }
    }
    /* page nav */
    if (pt_in(x, y, 80, 520, 220, 60)) {
        g_pick_scroll -= PICK_ROWS;
        if (g_pick_scroll < 0) g_pick_scroll = 0;
        return;
    }
    if (pt_in(x, y, 340, 520, 220, 60)) {
        if (g_pick_scroll + PICK_ROWS < g_pick_n) g_pick_scroll += PICK_ROWS;
        return;
    }
    /* actions */
    if (pt_in(x, y, 60, 600, 360, 60)) {          /* USE THIS FOLDER */
        if (is_mugen_dir(g_pick_path)) {
            picker_msg("LOADING...");
            picker_apply_dir(g_pick_path);
        } else {
            picker_msg("FOLDER MUST CONTAIN data/select.def");
        }
        return;
    }
    if (pt_in(x, y, 450, 600, 360, 60)) {         /* BUILT-IN DATA */
        char sp[600];
        settings_path(sp, sizeof(sp));
        remove(sp);
        picker_apply_dir(g_builtin_dir[0] ? g_builtin_dir : ".");
        return;
    }
    if (pt_in(x, y, 840, 600, 380, 60)) {         /* CANCEL */
        g_picker_on = 0;
        return;
    }
}

static void draw_pick_btn(const char *label, int rx, int ry, int rw, int rh, uint32_t rgb) {
    mg_fill_rect(rx, ry, rw, rh, (rgb>>16)&0xFF, (rgb>>8)&0xFF, rgb&0xFF, 235);
    int tw = builtin_text_w(label, 3);
    draw_builtin_text(label, rx + rw/2 - tw/2, ry + rh/2 - 11, 3, 0xFFFFFF);
}

static void mg_picker_draw(void) {
    /* dim the game behind */
    mg_fill_rect(0, 0, g_fbw, g_fbh, 0, 0, 0, 200);
    draw_builtin_text_centered("CHOOSE MUGEN DATA FOLDER", 24, 4, 0xFFB040);
    /* current path (tail if too long) */
    const char *p = g_pick_path;
    while (builtin_text_w(p, 2) > 1240 && p[0]) p++;
    draw_builtin_text_centered(p, 62, 2, 0xA0D0FF);
    if (g_pick_path[0] && is_mugen_dir(g_pick_path))
        draw_builtin_text_centered("(THIS FOLDER HAS MUGEN DATA)", 84, 2, 0x60FF80);
    else
        draw_builtin_text_centered("(PICK THE FOLDER THAT CONTAINS data/select.def)", 84, 2, 0x909090);

    /* rows */
    for (int r = 0; r < PICK_ROWS; r++) {
        int idx = g_pick_scroll + r;
        if (idx >= g_pick_n) break;
        int ry = PICK_ROW_Y + r * PICK_ROW_H;
        int cur = g_pick_ok[idx];
        mg_fill_rect(60, ry, 1160, PICK_ROW_H - 6,
                     cur ? 20 : 34, cur ? 60 : 40, cur ? 40 : 64, 235);
        char buf[84];
        snprintf(buf, sizeof(buf), "%s%s", idx == 0 ? "<UP> " : "[] ", g_pick_items[idx]);
        draw_builtin_text(buf, 84, ry + 12, 3, cur ? 0x70FF90 : 0xE0E8FF);
        if (cur) draw_builtin_text("MUGEN", 1100, ry + 12, 3, 0x70FF90);
    }
    /* scroll indicator */
    char nav[64];
    snprintf(nav, sizeof(nav), "%d-%d OF %d", g_pick_scroll + 1,
             g_pick_scroll + PICK_ROWS < g_pick_n ? g_pick_scroll + PICK_ROWS : g_pick_n, g_pick_n);
    draw_builtin_text_centered(nav, 500, 2, 0x808090);
    draw_pick_btn("< PREV", 80, 520, 220, 60, 0x3850A0);
    draw_pick_btn("NEXT >", 340, 520, 220, 60, 0x3850A0);
    draw_pick_btn("USE THIS FOLDER", 60, 600, 360, 60, 0x207030);
    draw_pick_btn("BUILT-IN DATA", 450, 600, 360, 60, 0x806020);
    draw_pick_btn("CANCEL", 840, 600, 380, 60, 0x702020);
    if (g_pick_msg_timer > 0 && g_pick_msg[0]) {
        g_pick_msg_timer--;
        if ((g_pick_msg_timer / 20) % 4 != 3)
            draw_builtin_text_centered(g_pick_msg, 676, 2, 0xFF8060);
    }
}

static void poll_events(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) g_game.running = 0;
        else if (e.type == SDL_APP_TERMINATING) g_game.running = 0;
        else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
            if (g_picker_on && e.type == SDL_KEYDOWN &&
                (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_BACKSPACE)) {
                g_picker_on = 0;
                continue;
            }
            uint32_t mask = 0;
            switch (e.key.keysym.sym) {
                case SDLK_LEFT: mask = BTN_BACK; break;
                case SDLK_RIGHT: mask = BTN_FWD; break;
                case SDLK_UP: mask = BTN_U; break;
                case SDLK_DOWN: mask = BTN_D; break;
                case SDLK_z: mask = BTN_A; break;
                case SDLK_x: mask = BTN_B; break;
                case SDLK_c: mask = BTN_C; break;
                case SDLK_a: mask = BTN_X; break;
                case SDLK_s: mask = BTN_Y; break;
                case SDLK_d: mask = BTN_Z; break;
                case SDLK_RETURN: mask = BTN_S; break;
            }
            if (mask) {
                if (e.type == SDL_KEYDOWN) g_keybuttons[0] |= mask;
                else g_keybuttons[0] &= ~mask;
            }
        }
        else if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERUP || e.type == SDL_FINGERMOTION) {
            if (g_picker_on) {
                if (e.type == SDL_FINGERDOWN) {
                    int x, y;
                    if (touch_xy(&e, &x, &y)) picker_handle_tap(x, y);
                }
            } else handle_touch(&e);
        }
        else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
            if (g_picker_on) {
                if (e.type == SDL_MOUSEBUTTONDOWN) {
                    int x, y;
                    if (touch_xy(&e, &x, &y)) picker_handle_tap(x, y);
                }
            } else handle_touch(&e);
        }
        else if (e.type == SDL_APP_WILLENTERBACKGROUND || e.type == SDL_APP_DIDENTERBACKGROUND) {
            /* release held buttons so nothing sticks after resume */
            g_touchbuttons = 0;
            for (int f = 0; f < 16; f++) { g_fingers[f].btn_idx = -1; g_fingers[f].fid = 0; }
        }
    }
}

/* ---------------- main loop ---------------- */
#ifdef __ANDROID__
#define GAME_DIR_DEFAULT ""
#else
#define GAME_DIR_DEFAULT "."
#endif

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    SDL_Log("MUGEN Android port v%s starting", MG_PORT_VERSION);

    /* Touches must NOT be duplicated as mouse events: v0.1.x processed both,
     * and the synthesized MOUSEBUTTONUP released every held virtual button
     * (it cleared all finger slots), breaking multitouch. */
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    /* Video is required; audio/joystick are best-effort (a failed audio
     * subsystem must not kill the game like it did before). */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init(VIDEO) failed: %s", SDL_GetError());
        return 1;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        SDL_Log("audio disabled (%s)", SDL_GetError());
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0)
        SDL_Log("joystick disabled (%s)", SDL_GetError());

    /* touch finger slots start free */
    for (int f = 0; f < 16; f++) { g_fingers[f].btn_idx = -1; g_fingers[f].fid = 0; }

    SDL_DisplayMode dm;
    SDL_GetCurrentDisplayMode(0, &dm);
    g_fbw = dm.w > 0 ? dm.w : 1280;
    g_fbh = dm.h > 0 ? dm.h : 720;
    if (g_fbw > 1920) { g_fbw = 1280; g_fbh = 720; }   /* desktop */

    g_win = SDL_CreateWindow("M.U.G.E.N", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             g_fbw, g_fbh, SDL_WINDOW_FULLSCREEN_DESKTOP);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) {
        SDL_Log("accelerated renderer failed (%s), trying software", SDL_GetError());
        g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!g_ren) {
        SDL_Log("FATAL: no renderer: %s", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderSetLogicalSize(g_ren, 1280, 720);
    g_fbw = 1280; g_fbh = 720;

    /* audio device (non-fatal) */
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = 22050; want.format = AUDIO_S16SYS; want.channels = 1;
    want.samples = 1024; want.callback = audio_cb;
    g_audiodev = SDL_OpenAudioDevice(NULL, 0, &want, &g_audiospec, 0);
    if (g_audiodev) SDL_PauseAudioDevice(g_audiodev, 0);
    else SDL_Log("audio device unavailable: %s", SDL_GetError());

    /* First frame RIGHT NOW: never leave the user staring at a black screen,
     * not even while data is still loading. */
    g_game.running = 1;
    present_loading_frame(0);

    /* game dir: priority = user-picked folder (settings file) > argv[1]
     * extraction dir (MainActivity.getArguments) > SDL storage paths. */
    const char *gamedir = GAME_DIR_DEFAULT;
    char dirbuf[512] = {0};
#ifdef __ANDROID__
    if (!(argc > 1 && argv[1] && argv[1][0])) {
        const char *base = SDL_AndroidGetExternalStoragePath();
        if (!base) base = SDL_AndroidGetInternalStoragePath();
        snprintf(dirbuf, sizeof(dirbuf), "%s/mugen", base);
    }
#endif
    if (argc > 1 && argv[1] && argv[1][0]) {
        snprintf(dirbuf, sizeof(dirbuf), "%s", argv[1]);
    }
    if (dirbuf[0]) gamedir = dirbuf;
    snprintf(g_builtin_dir, sizeof(g_builtin_dir), "%s", gamedir);

    /* user-picked custom data folder overrides the bundled one */
    {
        char sp[600];
        settings_path(sp, sizeof(sp));
        FILE *sf = fopen(sp, "r");
        if (sf) {
            char custom[512] = {0};
            if (fgets(custom, sizeof(custom), sf)) {
                custom[strcspn(custom, "\r\n")] = 0;
                if (custom[0] && is_mugen_dir(custom)) {
                    gamedir = custom;
                    SDL_Log("custom game dir (from settings): %s", custom);
                } else if (custom[0]) {
                    SDL_Log("custom game dir invalid, using built-in: %s", custom);
                }
            }
            fclose(sf);
        }
    }
    snprintf(g_active_dir, sizeof(g_active_dir), "%s", gamedir);
    SDL_Log("game dir: %s", gamedir);

    Uint32 load_t0 = SDL_GetTicks();
    mg_engine_init(gamedir);
    SDL_Log("engine init done in %dms: %d chars, %d stages, fonts %p/%p",
            (int)(SDL_GetTicks() - load_t0), g_game.nchars, g_game.nstages,
            (void*)g_game.fonts[0], (void*)g_game.fonts[1]);

    /* Hard data failure -> visible error, not a fake black "title screen".
     * (v0.1.1 hit this on real devices: a path-join bug made every data file
     * open fail even though the data WAS extracted correctly — see util.c.) */
    if (g_game.nchars == 0 || g_game.nstages == 0) {
        char l3[256];
        snprintf(l3, sizeof(l3), "DIR: %s", gamedir);
        fatal_error_loop("GAME DATA MISSING",
                         "REINSTALL THE APK", l3);
        SDL_Quit();
        return 1;
    }

    /* main loop: 60fps fixed timestep */
    Uint32 next_tick = SDL_GetTicks();
    int running = 1;
    while (running && g_game.running) {
        poll_events();
        /* feed input to entities (frozen while the folder picker is up) */
        if (!g_picker_on)
            for (int i = 0; i < g_nentities; i++)
                if (!g_entities[i].is_cpu)
                    g_entities[i].buttons = g_keybuttons[i & 1] | (i == 0 ? g_touchbuttons : 0);
        /* engine tick */
        mg_engine_frame();
        /* render */
        SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
        SDL_RenderClear(g_ren);
        mg_engine_render();
        if (g_picker_on) {
            mg_picker_draw();
        } else {
            if (g_game.game_phase == 0) {
                /* title screen: DATA folder button + active folder hint */
                draw_circle(1210, 46, 48, 40, 56, 96, 220);
                draw_builtin_text("DATA", 1210 - builtin_text_w("DATA", 3)/2, 46 - 10, 3, 0xFFE080);
                const char *d = g_active_dir;
                while (builtin_text_w(d, 1) > 1200 && d[0]) d++;
                draw_builtin_text_centered(d, 700, 1, 0x707080);
            }
            draw_touch_controls();
        }
        SDL_RenderPresent(g_ren);
        /* frame pace */
        next_tick += 1000 / 60;
        Uint32 now = SDL_GetTicks();
        if (now < next_tick) SDL_Delay(next_tick - now);
        else next_tick = now;
    }

    SDL_Quit();
    return 0;
}
