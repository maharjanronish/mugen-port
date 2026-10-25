/* platform_sdl.c — SDL2 platform for Android (and desktop Linux testing)
 * Renders via SDL2, multitouch virtual gamepad, WAV playback.
 */
#include "mugen.h"
#include <SDL.h>

static SDL_Window *g_win = NULL;
static SDL_Renderer *g_ren = NULL;
static int g_fbw = 1280, g_fbh = 720;
static SDL_Texture *g_target = NULL;

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

    /* map window coords to our virtual layout coords */
    double sx = (double)x / g_fbw, sy = (double)y / g_fbh;
    int lx = (int)(sx * 1280), ly = (int)(sy * 720);

    static struct { SDL_FingerID fid; int btn_idx; } fingers[16];
    if (e->type == SDL_FINGERDOWN) {
        for (int i = 0; i < (int)(sizeof(g_touchbtns)/sizeof(g_touchbtns[0])); i++) {
            TouchBtn *b = &g_touchbtns[i];
            int dx = lx - b->x, dy = ly - b->y;
            if (dx*dx + dy*dy <= b->r * b->r * 2) {
                g_touchbuttons |= b->bit;
                for (int f = 0; f < 16; f++)
                    if (fingers[f].btn_idx == -1) { fingers[f].fid = e->tfinger.fingerId; fingers[f].btn_idx = i; break; }
                return;
            }
        }
    } else if (e->type == SDL_FINGERUP) {
        for (int f = 0; f < 16; f++) {
            if (fingers[f].fid == e->tfinger.fingerId) {
                int idx = fingers[f].btn_idx;
                if (idx >= 0) {
                    g_touchbuttons &= ~g_touchbtns[idx].bit;
                }
                fingers[f].btn_idx = -1;
                fingers[f].fid = 0;
                return;
            }
        }
    }
    (void)y;
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
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_DisplayMode dm;
    SDL_GetCurrentDisplayMode(0, &dm);
    g_fbw = dm.w > 0 ? dm.w : 1280;
    g_fbh = dm.h > 0 ? dm.h : 720;
    if (g_fbw > 1920) { g_fbw = 1280; g_fbh = 720; }   /* desktop */

    g_win = SDL_CreateWindow("M.U.G.E.N", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             g_fbw, g_fbh, SDL_WINDOW_FULLSCREEN_DESKTOP);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, 0);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderSetLogicalSize(g_ren, 1280, 720);
    g_fbw = 1280; g_fbh = 720;

    /* audio */
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = 22050; want.format = AUDIO_S16SYS; want.channels = 1;
    want.samples = 1024; want.callback = audio_cb;
    g_audiodev = SDL_OpenAudioDevice(NULL, 0, &want, &g_audiospec, 0);
    if (g_audiodev) SDL_PauseAudioDevice(g_audiodev, 0);

    /* game dir: android assets extracted to internal storage */
    const char *gamedir = GAME_DIR_DEFAULT;
    char dirbuf[512];
#ifdef __ANDROID__
    {
        const char *base = SDL_AndroidGetExternalStoragePath();
        if (!base) base = SDL_AndroidGetInternalStoragePath();
        snprintf(dirbuf, sizeof(dirbuf), "%s/mugen", base);
        gamedir = dirbuf;
        SDL_Log("game dir: %s", gamedir);
    }
#endif
    if (argc > 1 && argv[1]) gamedir = argv[1];

    g_game.win_w = g_fbw; g_game.win_h = g_fbh;
    mg_engine_init(gamedir);

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
