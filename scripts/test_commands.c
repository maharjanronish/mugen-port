/* test_commands.c — unit tests for the Ikemen GO-style command matcher
 * (engine/input.c). Drives mg_input_update + mg_commands_step directly with
 * scripted per-tick button masks on a REAL loaded KFM (its actual kfm.cmd
 * command set), verifying Ikemen GO semantics:
 *
 *   C1  single press "x": fires ONLY on the press tick (edge), never while
 *       held, never on release — the core "one tap = one move" rule
 *   C2  holdfwd "/$F": continuously true while held (hold-only commands)
 *   C3  "FF" double-tap (autogreater F,>~F,>F): two distinct presses with a
 *       release between them, inside the time window
 *   C3b slow double-tap (outside window) -> NO match
 *   C4  QCF "~D, DF, F, x" with human-speed d-pad timing -> fires
 *   C4b QCF with sloppy slide (D held through the F phase) -> still fires
 *       via the strict-direction conflict rules? (must NOT fire from a
 *       mashed single direction hold)
 *   C5  "recovery" x+y: both edges on the SAME tick
 *   C5b x then y one tick apart -> NO recovery
 *   C6  "down_a" /$D,a: hold D, tap a -> same-tick match
 *   C7  "blocking" $F,x: forward edge + x edge same tick
 *   C8  idle: nothing fires
 *
 * Build:
 *   gcc -O1 -o test_commands test_commands.c \
 *       ../../mugen-port/engine/{util,sff,air,expr,char,state,input,fight,stage,game}.c \
 *       -I../../mugen-port/engine -lm
 * (platform hooks are stubbed locally, like debug_trace.c)
 */
#include "mugen.h"
#include <stdio.h>

/* ---- local platform stubs (headless, no SDL) ---- */
static uint32_t g_input[2] = {0, 0};
static int g_any = 0, g_confirm = 0;
void mg_headless_init(int w, int h) { (void)w; (void)h; }
void mg_headless_set_input(int player, uint32_t buttons) { g_input[player & 1] = buttons; }
void mg_render_sprite(MgSprite *s, int a, int b, int c, int d, int e, int f, int g2, int h2)
{ (void)s;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g2;(void)h2; }
void mg_fill_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{ (void)x;(void)y;(void)w;(void)h;(void)r;(void)g;(void)b;(void)a; }
void mg_render_text(MgFont *f, const char *t, int x, int y, int s, uint32_t rgb)
{ (void)f;(void)t;(void)x;(void)y;(void)s;(void)rgb; }
void mg_play_wav(uint8_t *d, int l, int c, int lo) { (void)d;(void)l;(void)c;(void)lo; }
void mg_play_music(const char *p, int l) { (void)p;(void)l; }
void mg_stop_channel(int c) { (void)c; }
void mg_platform_vibrate(int ms) { (void)ms; }
int mg_platform_any_button(void) { return g_any; }
int mg_platform_menu_action(int player, int *delta, int *confirm, int *back) {
    (void)player; *delta = 0; *confirm = 0; *back = 0;
    if (g_input[0] & BTN_FWD) *delta = 1;
    if (g_input[0] & BTN_BACK) *delta = -1;
    if (g_confirm) { *confirm = 1; g_confirm = 0; }
    return 1;
}

static int g_fail = 0;
#define CHECK(cond, ...) do { \
    if (cond) printf("  PASS: " __VA_ARGS__), printf("\n"); \
    else { printf("  FAIL: " __VA_ARGS__); printf("\n"); g_fail++; } \
} while (0)

/* one scripted input tick for BOTH players (P2 idle) */
static void p1_tick(uint32_t buttons) {
    g_entities[0].buttons = buttons;
    g_entities[1].buttons = 0;
    mg_input_update(&g_entities[0]);
    mg_input_update(&g_entities[1]);
    mg_commands_step(&g_entities[0]);
    mg_commands_step(&g_entities[1]);
}
static void idle(int n) { for (int i = 0; i < n; i++) p1_tick(0); }

int main(int argc, char **argv) {
    const char *gamedir = argc > 1 ? argv[1] : "../../mugen/mugen-1.1b1";
    printf("== Ikemen GO command matcher tests (gamedir=%s) ==\n", gamedir);
    g_game.win_w = 1280; g_game.win_h = 720;
    mg_engine_init(gamedir);
    if (g_game.nchars < 1) { printf("NO CHARS LOADED\n"); return 1; }

    /* skip to a fight so entities exist (title -> select -> fight) */
    g_any = 1;  mg_engine_frame(); g_any = 0;      /* title -> select */
    g_confirm = 1; mg_engine_frame();              /* select -> fight */
    if (g_game.game_phase != 2) { printf("NOT IN FIGHT (phase=%d)\n", g_game.game_phase); return 1; }
    printf("fight: p1=%s p2=%s (facing=%d)\n",
           g_entities[0].ch->name, g_entities[1].ch->name, g_entities[0].facing);
    idle(5);

    /* ---------------- C1: single press edge semantics ---------------- */
    printf("[C1 press x -> fires once, not while held, not on release]\n");
    {
        idle(10);
        p1_tick(BTN_X);
        CHECK(mg_check_command(&g_entities[0], "x"), "x fires on the press tick");
        p1_tick(BTN_X); p1_tick(BTN_X);
        CHECK(!mg_check_command(&g_entities[0], "x"), "x does NOT refire while held");
        CHECK(!mg_check_command(&g_entities[0], "y"), "y did not fire (only x was pressed)");
        p1_tick(0);
        CHECK(!mg_check_command(&g_entities[0], "x"), "x does not fire on release");
    }

    /* ---------------- C2: hold-only commands ---------------- */
    printf("[C2 holdfwd /$F while held]\n");
    {
        idle(10);
        p1_tick(BTN_FWD);
        CHECK(mg_check_command(&g_entities[0], "holdfwd"), "holdfwd true on first hold tick");
        p1_tick(BTN_FWD); p1_tick(BTN_FWD);
        CHECK(mg_check_command(&g_entities[0], "holdfwd"), "holdfwd continuously true while held");
        p1_tick(0); p1_tick(0);
        CHECK(!mg_check_command(&g_entities[0], "holdfwd"), "holdfwd false after release");
    }

    /* ---------------- C3: double-tap FF ---------------- */
    printf("[C3 FF double-tap (F,>~F,>F)]\n");
    {
        idle(10);
        p1_tick(BTN_FWD); p1_tick(BTN_FWD);      /* press F (2 ticks) */
        idle(3);                                  /* release gap */
        p1_tick(BTN_FWD);                         /* second press tick */
        CHECK(mg_check_command(&g_entities[0], "FF"), "FF fires on the 2nd distinct press");
        idle(2);
        CHECK(!mg_check_command(&g_entities[0], "FF"), "FF buffer expired (buffer.time=1)");
        /* too slow: second press outside the 10-tick window */
        idle(10);
        p1_tick(BTN_FWD); p1_tick(BTN_FWD);
        idle(14);                                  /* > time=10 */
        p1_tick(BTN_FWD);
        CHECK(!mg_check_command(&g_entities[0], "FF"), "slow double-tap does NOT fire FF");
        /* holding F continuously must not fire FF */
        idle(10);
        for (int i = 0; i < 12; i++) p1_tick(BTN_FWD);
        CHECK(!mg_check_command(&g_entities[0], "FF"), "holding F (no re-press) does not fire FF");
    }

    /* ---------------- C4: QCF motion ---------------- */
    printf("[C4 QCF ~D,DF,F,x with human timing]\n");
    {
        idle(10);
        for (int i = 0; i < 4; i++) p1_tick(BTN_D);   /* hold down */
        idle(4);                                       /* release D (~D edge) */
        for (int i = 0; i < 3; i++) p1_tick(BTN_D | BTN_FWD);  /* DF */
        p1_tick(BTN_FWD);                              /* roll to F (D released) */
        p1_tick(BTN_FWD | BTN_X);                      /* punch while F held */
        CHECK(mg_check_command(&g_entities[0], "QCF_x"), "QCF_x fires from the motion");
        CHECK(mg_check_command(&g_entities[0], "x"), "plain x also buffered (same tap)");
        /* mash counter-test: single direction held + button must NOT give QCF */
        idle(10);
        for (int i = 0; i < 6; i++) p1_tick(BTN_D | BTN_FWD);
        p1_tick(BTN_FWD);
        p1_tick(BTN_FWD | BTN_X);
        CHECK(!mg_check_command(&g_entities[0], "QCF_x"), "mashed DF->F without the ~D release does not fire");
    }

    /* ---------------- C5: x+y same tick ---------------- */
    printf("[C5 recovery x+y]\n");
    {
        idle(10);
        p1_tick(BTN_X | BTN_Y);
        CHECK(mg_check_command(&g_entities[0], "recovery"), "x+y same tick -> recovery");
        idle(10);
        p1_tick(BTN_X);
        p1_tick(BTN_Y);
        CHECK(!mg_check_command(&g_entities[0], "recovery"), "x then y one tick apart -> NO recovery");
    }

    /* ---------------- C6: /$D hold + a ---------------- */
    printf("[C6 down_a /$D,a]\n");
    {
        idle(10);
        for (int i = 0; i < 3; i++) p1_tick(BTN_D);
        p1_tick(BTN_D | BTN_A);
        CHECK(mg_check_command(&g_entities[0], "down_a"), "hold D + tap a -> down_a");
        CHECK(mg_check_command(&g_entities[0], "a"), "plain a also fired");
    }

    /* ---------------- C7: blocking $F,x ---------------- */
    printf("[C7 blocking $F,x]\n");
    {
        idle(10);
        p1_tick(BTN_FWD | BTN_X);   /* fresh buffer: F edge + x edge same tick */
        CHECK(mg_check_command(&g_entities[0], "blocking"), "F edge + x edge same tick -> blocking");
    }

    /* ---------------- C8: idle nothing fires ---------------- */
    printf("[C8 idle]\n");
    {
        int any = 0;
        idle(120);
        for (int i = 0; i < g_entities[0].ch->ncommands; i++)
            if (mg_check_command(&g_entities[0], g_entities[0].ch->commands[i].name)) { any = 1; break; }
        CHECK(!any, "120 idle ticks: no command buffered");
    }

    printf("== %s (%d failures) ==\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    return g_fail ? 1 : 0;
}
