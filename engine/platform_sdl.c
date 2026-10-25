/* platform_sdl.c — SDL2 platform for Android (and desktop Linux testing)
 * Renders via SDL2, multitouch virtual gamepad, WAV playback.
 */
#include "mugen.h"
#include <SDL.h>

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
            draw_builtin_text_centered("CHECK LOGCAT TAG: MUGEN / SDL", g_fbh / 2 + 130, 2, 0x909090);
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
    SDL_Texture *tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ABGR8888,
                                         SDL_TEXTUREACCESS_STATIC, spr->w, spr->h);
    if (!tex) return NULL;
    /* our RGBA is r<<24|g<<16|b<<8|a = ABGR8888 little-endian */
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

/* finger -> button binding for multitouch (slots free when btn_idx < 0) */
typedef struct { SDL_FingerID fid; int btn_idx; } FingerSlot;
static FingerSlot g_fingers[16];

static void handle_touch(SDL_Event *e) {
    int x = 0, y = 0;
    if (e->type == SDL_FINGERDOWN || e->type == SDL_FINGERMOTION || e->type == SDL_FINGERUP) {
        int ww, wh;
        SDL_GetWindowSize(g_win, &ww, &wh);
        x = (int)(e->tfinger.x * ww);
        y = (int)(e->tfinger.y * wh);
    } else if (e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) {
        x = e->button.x; y = e->button.y;
    } else return;

    /* map window pixels -> 1280x720 logical coords (letterboxed by SDL).
     * The old code used raw window pixels, so buttons landed in the wrong
     * place on every real device (window is never exactly 1280x720). */
    int lx = x, ly = y;
    {
        float lgx = 0.0f, lgy = 0.0f;
        SDL_RenderWindowToLogical(g_ren, (float)x, (float)y, &lgx, &lgy);
        lx = (int)lgx; ly = (int)lgy;
    }

    if (e->type == SDL_FINGERDOWN || e->type == SDL_MOUSEBUTTONDOWN) {
        for (int i = 0; i < (int)(sizeof(g_touchbtns)/sizeof(g_touchbtns[0])); i++) {
            TouchBtn *b = &g_touchbtns[i];
            int dx = lx - b->x, dy = ly - b->y;
            if (dx*dx + dy*dy <= b->r * b->r * 2) {
                g_touchbuttons |= b->bit;
                for (int f = 0; f < 16; f++)
                    if (g_fingers[f].btn_idx < 0) {
                        g_fingers[f].fid = e->tfinger.fingerId;
                        g_fingers[f].btn_idx = i;
                        break;
                    }
                return;
            }
        }
    } else if (e->type == SDL_FINGERUP || e->type == SDL_MOUSEBUTTONUP) {
        SDL_FingerID fid = (e->type == SDL_FINGERUP) ? e->tfinger.fingerId : 1;
        for (int f = 0; f < 16; f++) {
            if (g_fingers[f].btn_idx >= 0 &&
                (e->type == SDL_MOUSEBUTTONUP || g_fingers[f].fid == fid)) {
                g_touchbuttons &= ~g_touchbtns[g_fingers[f].btn_idx].bit;
                g_fingers[f].btn_idx = -1;
                g_fingers[f].fid = 0;
                if (e->type == SDL_FINGERUP) return;
            }
        }
    }
}

static void draw_touch_controls(void) {
    for (int i = 0; i < (int)(sizeof(g_touchbtns)/sizeof(g_touchbtns[0])); i++) {
        TouchBtn *b = &g_touchbtns[i];
        uint8_t a = (g_touchbuttons & b->bit) ? 160 : 90;
        mg_fill_rect(b->x - b->r, b->y - b->r, b->r*2, b->r*2, 200, 200, 220, a);
        if (g_game.fonts[0]) {
            int tw = strlen(b->label) * (g_game.fonts[0]->size_w + 1) * 3;
            int th = g_game.fonts[0]->size_h * 3;
            mg_render_text(g_game.fonts[0], b->label, b->x - tw/2, b->y - th/2, 3, 0x202030);
        }
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

static void poll_events(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) g_game.running = 0;
        else if (e.type == SDL_APP_TERMINATING) g_game.running = 0;
        else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
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
        else if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERUP) handle_touch(&e);
        else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) handle_touch(&e);
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
    SDL_Log("MUGEN Android port v0.1.1 starting");

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

    /* game dir: Android passes the extraction dir via argv[1] (see
     * MainActivity.getArguments); fallback to SDL storage paths. */
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
    SDL_Log("game dir: %s", gamedir);

    Uint32 load_t0 = SDL_GetTicks();
    mg_engine_init(gamedir);
    SDL_Log("engine init done in %dms: %d chars, %d stages, fonts %p/%p",
            (int)(SDL_GetTicks() - load_t0), g_game.nchars, g_game.nstages,
            (void*)g_game.fonts[0], (void*)g_game.fonts[1]);

    /* Hard data failure -> visible error, not a fake black "title screen" */
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
        /* feed input to entities */
        for (int i = 0; i < g_nentities; i++)
            if (!g_entities[i].is_cpu)
                g_entities[i].buttons = g_keybuttons[i & 1] | (i == 0 ? g_touchbuttons : 0);
        /* engine tick */
        mg_engine_frame();
        /* render */
        SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
        SDL_RenderClear(g_ren);
        mg_engine_render();
        draw_touch_controls();
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
