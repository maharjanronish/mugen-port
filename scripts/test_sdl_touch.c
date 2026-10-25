/* test_sdl_touch.c — END-TO-END test of the REAL SDL platform layer:
 * includes platform_sdl.c textually (access to statics), drives the actual
 * poll_events()/main-loop code path with SYNTHETIC SDL touch & keyboard
 * events, exactly like a real device would deliver them.
 *
 * Verifies:
 *   T1. color byte order: spr_texture upload via SDL_PIXELFORMAT_* (ABGR vs RGBA)
 *   T2. title -> select -> fight navigation via synthetic KEYBOARD events
 *   T3. FINGERDOWN on virtual button X -> punch state 200 fires ONCE, exits
 *   T4. release -> g_touchbuttons cleared; no re-trigger while held
 *   T5. repeated taps -> one move per tap (edge semantics)
 *   T6. 600 idle frames with NO input (P2 human too) -> zero attack states
 *   T7. touch coordinate mapping on a NON-1280x720 window (device-like)
 *
 * Build:
 *   gcc -O1 -o test_sdl_touch test_sdl_touch.c -lm \
 *       -I/home/z/my-project/mugen-port/engine \
 *       -I/home/z/my-project/mugen-android/app/jni/SDL/include \
 *       -L/usr/lib/x86_64-linux-gnu -l:libSDL2-2.0.so.0
 */
#define main platform_sdl_main_unused
#include "../../mugen-port/engine/platform_sdl.c"
#undef main

#include <stdio.h>

static int g_fail = 0;
#define CHECK(cond, ...) do { \
    if (cond) printf("  PASS: " __VA_ARGS__), printf("\n"); \
    else { printf("  FAIL: " __VA_ARGS__); printf("\n"); g_fail++; } \
} while (0)

/* --- synthetic input helpers --- */
static void push_key(SDL_Keycode sym, int down) {
    SDL_Event e; SDL_zero(e);
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.keysym.sym = sym;
    SDL_PushEvent(&e);
}
static void push_finger(SDL_FingerID fid, float nx, float ny, int down) {
    SDL_Event e; SDL_zero(e);
    e.type = down ? SDL_FINGERDOWN : SDL_FINGERUP;
    e.tfinger.touchId = 1;
    e.tfinger.fingerId = fid;
    e.tfinger.x = nx; e.tfinger.y = ny;
    e.tfinger.dx = e.tfinger.dy = 0;
    e.tfinger.pressure = 1.0f;
    SDL_PushEvent(&e);
}
/* logical (1280x720) coords -> normalized window coords */
static void tap_button(int btn_idx, SDL_FingerID fid, int down) {
    TouchBtn *b = &g_touchbtns[btn_idx];
    int ww, wh; SDL_GetWindowSize(g_win, &ww, &wh);
    push_finger(fid, (float)b->x / ww, (float)b->y / wh, down);
}

/* the REAL main-loop body, copied verbatim from platform_sdl.c main() */
static void one_frame(void) {
    poll_events();
    if (!g_picker_on)
        for (int i = 0; i < g_nentities; i++)
            if (!g_entities[i].is_cpu)
                g_entities[i].buttons = g_keybuttons[i & 1] | (i == 0 ? g_touchbuttons : 0);
    mg_engine_frame();
}

static int attack_state(int st) {
    /* KFM basic attacks: 200 punch, 230 kick, 205/235 strong, 400s jumps */
    return st == 200 || st == 205 || st == 230 || st == 235 || st == 240 ||
           st == 245 || st == 400 || st == 410 || st == 420 || st == 430;
}

int main(int argc, char **argv) {
    const char *gamedir = argc > 1 ? argv[1] : "../../mugen/mugen-1.1b1";
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    printf("== SDL platform end-to-end test (real poll_events path) ==\n");

    /* ---- platform init: mirror platform_sdl.c main() ---- */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("SDL_Init: %s\n", SDL_GetError()); return 1; }
    for (int f = 0; f < 16; f++) { g_fingers[f].btn_idx = -1; g_fingers[f].fid = 0; }
    g_fbw = 1280; g_fbh = 720;
    g_win = SDL_CreateWindow("t", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             1280, 720, 0);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (!g_ren) { printf("no renderer: %s\n", SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(g_ren, 1280, 720);

    /* ---------------- T1: color byte order ---------------- */
    printf("[T1 sprite color order]\n");
    {
        /* our format from sff.c: rgba = r<<24 | g<<16 | b<<8 | a */
        MgSprite spr; memset(&spr, 0, sizeof(spr));
        spr.w = 1; spr.h = 1;
        uint32_t px = ((uint32_t)255 << 24) | ((uint32_t)60 << 16) | ((uint32_t)30 << 8) | 255;
        spr.rgba = &px;
        SDL_Texture *tex = spr_texture(&spr);   /* real upload path */
        CHECK(tex != NULL, "texture created");
        SDL_SetRenderTarget(g_ren, tex);
        /* read back through a 1x1 target copy is not possible for STATIC;
         * instead render the texture and read pixels from the backbuffer */
        SDL_SetRenderTarget(g_ren, NULL);
        SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
        SDL_RenderClear(g_ren);
        SDL_RenderCopy(g_ren, tex, NULL, NULL);
        SDL_RenderPresent(g_ren);
        SDL_Rect r = {0, 0, 1, 1};
        Uint32 got = 0;
        SDL_RenderReadPixels(g_ren, &r, SDL_PIXELFORMAT_RGBA8888, &got, 4);
        Uint8 rr = (got >> 24) & 0xFF, gg = (got >> 16) & 0xFF, bb = (got >> 8) & 0xFF;
        printf("  sent r=255 g=60 b=30 -> screen r=%d g=%d b=%d (0x%08X)\n", rr, gg, bb, got);
        CHECK(rr == 255 && gg == 60 && bb == 30,
              "colors survive round-trip (no channel swap)");
    }

    /* ---------------- engine init + menu nav (keyboard) ---------------- */
    printf("[T2 menu navigation via keyboard events]\n");
    mg_engine_init(gamedir);
    CHECK(g_game.nchars >= 2, "roster loaded (%d chars)", g_game.nchars);
    if (g_game.nchars < 2) return 1;
    g_game.p2_cpu = 0;             /* isolate P1: P2 stays idle human */

    for (int i = 0; i < 5; i++) { push_key(SDLK_RETURN, 1); one_frame(); push_key(SDLK_RETURN, 0); one_frame(); }
    printf("  phase=%d\n", g_game.game_phase);
    CHECK(g_game.game_phase >= 1, "keyboard events drove title -> select (phase=%d)", g_game.game_phase);
    /* select screen: confirm via touch on START area too */
    for (int i = 0; i < 6 && g_game.game_phase == 1; i++) {
        tap_button(10, 99, 1); one_frame(); tap_button(10, 99, 0); one_frame();   /* START btn */
    }
    printf("  phase=%d (touch nav %s)\n", g_game.game_phase,
           g_game.game_phase >= 2 ? "worked" : "did not advance");
    CHECK(g_game.game_phase == 2, "touch events drove select -> fight (phase=%d)", g_game.game_phase);

    /* reach round_state 2 (fight!) */
    for (int i = 0; i < 130 && g_game.round_state != 2; i++) one_frame();
    /* isolate P1: the select-screen flow re-enables p2_cpu, so disable the
     * CPU directly on the entity (test only cares about P1's touch input) */
    g_entities[1].is_cpu = 0;
    for (int i = 0; i < g_nentities; i++)
        if (!g_entities[i].is_cpu && i != 0)
            g_entities[i].buttons = 0;
    printf("  round_state=%d p1=%s p2=%s\n", g_game.round_state,
           g_entities[0].ch->name, g_entities[1].ch->name);
    CHECK(g_game.round_state == 2, "reached FIGHT (round_state=%d)", g_game.round_state);

    /* ---------------- T3/T4: single tap -> single move ---------------- */
    printf("[T3 tap X -> punch ONCE]\n");
    int entries200 = 0, cur200 = 0, prev_state = g_entities[0].state_no;
    int held_frames = 0, released_frames = 0;
    /* press X (1080,500) hold 25 frames, then release, watch 90 frames */
    tap_button(7, 50, 1);   /* idx 7 = BTN_X */
    one_frame();
    CHECK(g_touchbuttons == BTN_X, "FINGERDOWN set g_touchbuttons=BTN_X (0x%X)", g_touchbuttons);
    CHECK(g_entities[0].buttons == BTN_X, "entity0 buttons = BTN_X (0x%X)", g_entities[0].buttons);
    for (int f = 0; f < 24; f++) {
        one_frame();
        if (g_entities[0].state_no != prev_state) {
            if (attack_state(g_entities[0].state_no) && !cur200) { cur200 = 1; entries200++; }
            else if (!attack_state(g_entities[0].state_no)) cur200 = 0;
            prev_state = g_entities[0].state_no;
        }
        if (attack_state(g_entities[0].state_no)) held_frames++;
    }
    printf("  while holding X: attack-state frames=%d entries=%d state_now=%d\n",
           held_frames, entries200, g_entities[0].state_no);
    CHECK(entries200 == 1, "holding X = exactly ONE move entry (got %d)", entries200);

    printf("[T4 release X]\n");
    tap_button(7, 50, 0);
    one_frame();
    CHECK(g_touchbuttons == 0, "FINGERUP cleared g_touchbuttons (0x%X)", g_touchbuttons);
    for (int f = 0; f < 90; f++) {
        one_frame();
        if (g_entities[0].state_no != prev_state) {
            if (attack_state(g_entities[0].state_no) && !cur200) { cur200 = 1; entries200++; }
            else if (!attack_state(g_entities[0].state_no)) cur200 = 0;
            prev_state = g_entities[0].state_no;
        }
        if (attack_state(g_entities[0].state_no)) released_frames++;
    }
    printf("  after release: total entries=%d still-attacking frames=%d state_now=%d\n",
           entries200, released_frames, g_entities[0].state_no);
    CHECK(released_frames < held_frames, "move finished after release (attack frames %d -> %d)",
          held_frames, released_frames);
    CHECK(g_entities[0].state_no == 0 || !attack_state(g_entities[0].state_no),
          "P1 back to neutral state (state=%d)", g_entities[0].state_no);

    /* ---------------- T5: three taps -> three moves ---------------- */
    printf("[T5 tap-tap-tap]\n");
    entries200 = 0; cur200 = 0; prev_state = g_entities[0].state_no;
    for (int t = 0; t < 3; t++) {
        for (int w = 0; w < 8; w++) one_frame();       /* idle gap */
        tap_button(7, 51, 1);
        for (int w = 0; w < 4; w++) one_frame();
        tap_button(7, 51, 0);
        for (int w = 0; w < 10; w++) {
            one_frame();
            if (g_entities[0].state_no != prev_state) {
                if (attack_state(g_entities[0].state_no) && !cur200) { cur200 = 1; entries200++; }
                else if (!attack_state(g_entities[0].state_no)) cur200 = 0;
                prev_state = g_entities[0].state_no;
            }
        }
    }
    printf("  3 taps -> %d attack entries (state=%d)\n", entries200, g_entities[0].state_no);
    CHECK(entries200 == 3, "3 taps -> 3 distinct moves (got %d)", entries200);

    /* ---------------- T6: long idle -> NO phantom attacks ---------------- */
    printf("[T6 idle stability]\n");
    int phantom = 0;
    prev_state = g_entities[0].state_no;
    for (int f = 0; f < 600; f++) {
        one_frame();
        if (attack_state(g_entities[0].state_no)) { phantom++; break; }
        if (attack_state(g_entities[1].state_no)) { phantom += 2; break; }
        if (g_touchbuttons || g_entities[0].buttons) { phantom += 4; break; }
        if (g_game.game_phase != 2) break;
    }
    CHECK(phantom == 0, "600 idle frames: no phantom input/attacks (flag=%d)", phantom);

    /* ---------------- T7: coordinate mapping on odd window size ---------------- */
    printf("[T7 coordinate mapping (device-like window)]\n");
    {
        SDL_Window *w2 = SDL_CreateWindow("t2", 0, 0, 2340, 1080, 0);
        SDL_Renderer *r2 = SDL_CreateRenderer(w2, -1, SDL_RENDERER_SOFTWARE);
        SDL_RenderSetLogicalSize(r2, 1280, 720);
        /* logical (1080,500) [button X] on 2340x1080 letterboxed:
         * scale = min(2340/1280, 1080/720) = 1.5; xoff=(2340-1920)/2=210
         * physical = (210 + 1080*1.5, (1080-1080)/2 + 500*1.5) = (1830, 750) */
        float gx, gy;
        SDL_RenderWindowToLogical(r2, 1830.0f, 750.0f, &gx, &gy);
        printf("  physical(1830,750) -> logical(%.0f,%.0f), want (1080,500)\n", gx, gy);
        CHECK(gx > 1075 && gx < 1085 && gy > 495 && gy < 505,
              "RenderWindowToLogical letterbox math correct");
        SDL_DestroyRenderer(r2);
        SDL_DestroyWindow(w2);
    }

    printf("== %s (%d failures) ==\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    return g_fail ? 1 : 0;
}
