/* platform_headless.c — PC validation platform: memory framebuffer + BMP dumps
 * Used to verify engine correctness (formats, states, fight flow) without SDL.
 */
#include "mugen.h"
#include <time.h>

static uint32_t *g_fb = NULL;    /* RGBA8888 */
static int FBW = 0, FBH = 0;

/* input scripting */
static uint32_t g_input[2] = {0, 0};
static int g_any_button_flag = 0;
static int g_menu_confirm = 0;

void mg_headless_init(int w, int h) {
    FBW = w; FBH = h;
    g_fb = (uint32_t*)malloc(w * h * 4);
    memset(g_fb, 0, w * h * 4);
    g_game.win_w = w; g_game.win_h = h;
}

void mg_headless_set_input(int player, uint32_t buttons) {
    g_input[player & 1] = buttons;
    if (buttons) g_any_button_flag = 1;
}

void mg_headless_set_confirm(int on) { g_menu_confirm = on; }

/* engine hooks */
void mg_render_sprite(MgSprite *spr, int dx, int dy, int dw, int dh,
                      int flip_h, int flip_v, int alpha_src, int alpha_dst) {
    if (!spr || !spr->rgba) return;
    (void)alpha_src; (void)alpha_dst;
    if (dw <= 0) dw = spr->w;
    if (dh <= 0) dh = spr->h;
    for (int y = 0; y < dh; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= FBH) continue;
        int sy = (int)((double)y * spr->h / dh);
        if (flip_v) sy = spr->h - 1 - sy;
        for (int x = 0; x < dw; x++) {
            int tx = dx + x;
            if (tx < 0 || tx >= FBW) continue;
            int sx = (int)((double)x * spr->w / dw);
            if (flip_h) sx = spr->w - 1 - sx;
            uint32_t px = spr->rgba[sy * spr->w + sx];
            uint8_t a = px & 0xFF;
            if (!a) continue;
            g_fb[ty * FBW + tx] = 0xFF000000 | (px & 0xFFFFFF00) >> 8 | 0; /* convert RGBA→BGRA-ish */
            /* store as 0xAARRGGBB for BMP writer (we convert there) */
            g_fb[ty * FBW + tx] = px;
        }
    }
}

void mg_fill_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= FBH) continue;
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= FBW) continue;
            if (a == 255) {
                g_fb[yy * FBW + xx] = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFF;
            } else {
                uint32_t dst = g_fb[yy * FBW + xx];
                uint8_t dr = (dst >> 24) & 0xFF, dg = (dst >> 16) & 0xFF, db = (dst >> 8) & 0xFF;
                dr = (dr * (255 - a) + r * a) / 255;
                dg = (dg * (255 - a) + g * a) / 255;
                db = (db * (255 - a) + b * a) / 255;
                g_fb[yy * FBW + xx] = ((uint32_t)dr << 24) | ((uint32_t)dg << 16) | ((uint32_t)db << 8) | 0xFF;
            }
        }
    }
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
            /* space or missing: advance */
            cx += (f->size_w ? f->size_w : 6) * scale + f->spacing_x;
            continue;
        }
        for (int yy = 0; yy < gh * scale; yy++)
            for (int xx = 0; xx < gw * scale; xx++) {
                int tx = cx + xx, ty = y + yy;
                if (tx < 0 || tx >= FBW || ty < 0 || ty >= FBH) continue;
                uint32_t px = glyph[(yy / scale) * gw + (xx / scale)];
                uint8_t a = px & 0xFF;
                if (!a) continue;
                g_fb[ty * FBW + tx] = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFF;
            }
        cx += gw * scale + (f->spacing_x ? f->spacing_x : 1) * scale;
    }
}

/* sound stubs */
void mg_play_wav(uint8_t *data, int len, int channel, int loop) { (void)data; (void)len; (void)channel; (void)loop; }
void mg_platform_vibrate(int ms) { (void)ms; }   /* no haptics headless */
void mg_play_music(const char *path, int loop) { (void)path; (void)loop; }
void mg_stop_channel(int channel) { (void)channel; }

int mg_platform_any_button(void) {
    int v = g_any_button_flag;
    return v;
}

int mg_platform_menu_action(int player, int *delta, int *confirm, int *back) {
    (void)player;
    *delta = 0; *confirm = 0; *back = 0;
    if (g_input[0] & 1024) *delta = 1;       /* F/right */
    if (g_input[0] & 512) *delta = -1;       /* B/left */
    if (g_menu_confirm) { *confirm = 1; g_menu_confirm = 0; }
    return 1;
}

/* BMP dump (24-bit) */
void mg_headless_dump(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int rowbytes = FBW * 3;
    int padding = (4 - (rowbytes % 4)) % 4;
    int datasize = (rowbytes + padding) * FBH;
    uint32_t headers[15] = {0};
    headers[0] = 0x4D42; /* later split */
    uint8_t fh[14];
    fh[0]='B'; fh[1]='M';
    uint32_t filesize = 14 + 40 + datasize;
    memcpy(fh + 2, &filesize, 4);
    uint32_t zero = 0;
    memcpy(fh + 6, &zero, 4);
    uint32_t offset = 14 + 40;
    memcpy(fh + 10, &offset, 4);
    fwrite(fh, 1, 14, f);
    uint8_t ih[40];
    uint32_t ihdr[10] = {40, (uint32_t)FBW, (uint32_t)FBH, 1, 24, 0, (uint32_t)datasize, 2835, 2835, 0};
    memcpy(ih, ihdr, 40);
    fwrite(ih, 1, 40, f);
    /* rows bottom-up */
    uint8_t *row = (uint8_t*)malloc(rowbytes + padding);
    for (int y = FBH - 1; y >= 0; y--) {
        for (int x = 0; x < FBW; x++) {
            uint32_t px = g_fb[y * FBW + x];
            row[x*3+0] = (px >> 8) & 0xFF;   /* B */
            row[x*3+1] = (px >> 16) & 0xFF;  /* G */
            row[x*3+2] = (px >> 24) & 0xFF;  /* R */
        }
        memset(row + rowbytes, 0, padding);
        fwrite(row, 1, rowbytes + padding, f);
    }
    free(row);
    fclose(f);
    printf("[dump] %s\n", path);
}

/* feed input into entities each frame (called by test main) */
void mg_headless_feed_entities(void) {
    for (int i = 0; i < g_nentities; i++) {
        if (!g_entities[i].is_cpu)
            g_entities[i].buttons = g_input[i & 1];
    }
}

/* ---------------- test main ---------------- */
int main(int argc, char **argv) {
    const char *gamedir = argc > 1 ? argv[1] : ".";
    printf("MUGEN-Android headless test | gamedir=%s\n", gamedir);

    mg_headless_init(1280, 720);
    mg_engine_init(gamedir);
    if (g_game.nchars == 0) { printf("NO CHARS LOADED\n"); return 1; }

    /* --- test 1: title → select → fight --- */
    printf("phase: title\n");
    mg_engine_frame(); mg_engine_render();
    mg_headless_dump("test-title.bmp");

    /* press button to enter select */
    mg_headless_set_input(0, 64);  /* start */
    mg_engine_frame();
    mg_headless_set_input(0, 0);
    printf("phase: select (%d chars)\n", g_game.nchars);
    mg_engine_frame(); mg_engine_render();
    mg_headless_dump("test-select.bmp");

    /* confirm selection */
    mg_headless_set_confirm(1);
    mg_engine_frame();
    mg_headless_set_input(0, 0);
    printf("phase: fight (p1=%s vs p2=%s, stage=%s)\n",
           g_game.chars[g_game.selected[0]]->name,
           g_game.chars[g_game.selected[1]]->name,
           g_game.stages[g_game.selected_stage]->name);

    /* world scale */
    g_game.world_scale = 3.0;
    MgStage *st = g_game.stages[g_game.selected_stage];
    g_game.world_w = (int)st->localcoord_w;
    g_game.world_h = (int)st->localcoord_h;
    g_game.camera_x = st->startx + st->boundleft + g_game.world_w/2 - g_game.world_w/2;

    /* run intro frames */
    for (int i = 0; i < 120; i++) {
        mg_headless_feed_entities();
        mg_engine_frame();
    }
    mg_engine_render();
    mg_headless_dump("test-fight-round1.bmp");

    /* P1 walks forward 60 frames then attacks */
    mg_headless_set_input(0, 1024);   /* F */
    for (int i = 0; i < 60; i++) {
        mg_headless_feed_entities();
        mg_engine_frame();
    }
    mg_headless_set_input(0, 0);
    /* punch: a */
    mg_headless_set_input(0, 1);
    for (int i = 0; i < 8; i++) {
        mg_headless_feed_entities();
        mg_engine_frame();
    }
    mg_headless_set_input(0, 0);
    for (int i = 0; i < 30; i++) {
        mg_headless_feed_entities();
        mg_engine_frame();
    }
    mg_engine_render();
    mg_headless_dump("test-fight-attack.bmp");

    /* long-run stability: 3600 frames (60s) with random input */
    srand(42);
    for (int i = 0; i < 3600; i++) {
        uint32_t in = 0;
        int r = rand() % 10;
        if (r < 3) in = 1024;
        else if (r < 5) in = 1;
        else if (r < 6) in = 2;
        else if (r < 7) in = 8;
        else if (r < 8) in = 512;
        else if (r < 9) in = 128;
        mg_headless_set_input(0, in);
        mg_headless_feed_entities();
        mg_engine_frame();
        if (g_game.game_phase != 2) break;
    }
    printf("after longrun: phase=%d round_state=%d p1life=%d p2life=%d frames=%d\n",
           g_game.game_phase, g_game.round_state,
           g_entities[0].life, g_entities[1].life, g_game.frames_done);
    mg_engine_render();
    mg_headless_dump("test-fight-longrun.bmp");
    printf("TEST COMPLETE\n");
    return 0;
}
