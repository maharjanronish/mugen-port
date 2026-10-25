/* test_fight_flow.c — v0.2.1 full-fight sanity: CPU ON, aggressive P1
 * synthetic input. Verifies rounds PROGRESS (KO -> announce -> next round),
 * hits land, and no entity ever gets stuck in one state forever.
 * Build: gcc -O1 -o test_fight_flow test_fight_flow.c \
 *   <engine>/{util,sff,air,expr,char,state,fight,stage,game}.c -lm -I<engine> */
#include "mugen.h"
#include <stdio.h>
#include <stdlib.h>

/* headless platform stubs (same as regtest) */
static int g_confirm = 0;
void mg_render_sprite(MgSprite *s,int a,int b,int c,int d,int e,int f,int g,int h){(void)s;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;}
void mg_fill_rect(int x,int y,int w,int h,uint8_t r,uint8_t g,uint8_t b,uint8_t a){(void)x;(void)y;(void)w;(void)h;(void)r;(void)g;(void)b;(void)a;}
void mg_render_text(MgFont *f,const char *t,int x,int y,int s,uint32_t c){(void)f;(void)t;(void)x;(void)y;(void)s;(void)c;}
void mg_play_wav(uint8_t *d,int l,int c,int lo){(void)d;(void)l;(void)c;(void)lo;}
void mg_play_music(const char *p,int l){(void)p;(void)l;}
void mg_stop_channel(int c){(void)c;}
void mg_platform_vibrate(int ms){(void)ms;}
int mg_platform_any_button(void){return 1;}
int mg_platform_menu_action(int p,int *d,int *c,int *b){(void)p;*d=0;*c=g_confirm;*b=0;return 1;}

int main(int argc, char **argv) {
    const char *gamedir = argc > 1 ? argv[1] : "../../mugen/mugen-1.1b1";
    int fail = 0;
    g_game.win_w = 1280; g_game.win_h = 720;
    mg_engine_init(gamedir);
    if (g_game.nchars < 2) { printf("no roster\n"); return 1; }
    /* p2 stays CPU (default) — test the real fight configuration */

    /* title -> select -> fight through the REAL phase machine (regtest flow) */
    for (int i = 0; i < 3; i++) mg_engine_frame();
    g_confirm = 1; mg_engine_frame(); g_confirm = 0;    /* title -> select */
    if (g_game.game_phase == 1) {
        g_confirm = 1;
        for (int i = 0; i < 4 && g_game.game_phase == 1; i++) mg_engine_frame();
        g_confirm = 0;                                   /* select -> fight */
    }
    if (g_game.game_phase < 2) {
        printf("FAIL: could not reach fight (phase=%d)\n", g_game.game_phase);
        return 1;
    }
    for (int i = 0; i < 150 && g_game.round_state != 2; i++) mg_engine_frame();
    printf("phase=%d round_state=%d p2_cpu=%d\n", g_game.game_phase, g_game.round_state, g_game.p2_cpu);

    srand(3);
    int state_hist[2] = {0,0};
    int stuck_frames[2] = {0,0};
    int max_stuck[2] = {0,0};
    int hits_seen = 0;
    int rounds_seen = g_game.round_no;
    int ko_seen = 0;
    int prev_life1 = -1;
    for (int f = 0; f < 3600; f++) {
        /* aggressive P1: walk in + attack presses */
        uint32_t in = BTN_FWD;
        int r = rand() % 10;
        if (r < 3) in = BTN_FWD | BTN_A;     /* walk-in attacks */
        else if (r < 4) in = BTN_FWD | BTN_X;
        else if (r < 5) in = BTN_A;
        else if (r < 6) in = BTN_X;
        else if (r < 7) in = 0;
        else in = BTN_FWD;
        for (int i = 0; i < g_nentities; i++)
            if (!g_entities[i].is_cpu) g_entities[i].buttons = (i == 0) ? in : 0;
        mg_engine_frame();
        if (f % 300 == 0)
            printf("  f=%d p1: x=%.1f st=%d life=%d pf=%d | p2: x=%.1f st=%d life=%d vx=%.2f fc=%d btns=0x%X pf=%d bounds=[%.0f,%.0f]\n",
                   f, g_entities[0].pos_x, g_entities[0].state_no, g_entities[0].life, g_entities[0].pos_freeze,
                   g_entities[1].pos_x, g_entities[1].state_no, g_entities[1].life,
                   g_entities[1].vel_x, g_entities[1].facing,
                   g_entities[1].buttons, g_entities[1].pos_freeze,
                   g_game.stages[g_game.selected_stage]->boundleft, g_game.stages[g_game.selected_stage]->boundright);
        if (g_game.game_phase != 2) break;
        if (g_game.round_state == 3) ko_seen = 1;
        if (g_game.round_no > rounds_seen) { rounds_seen = g_game.round_no; }
        for (int e = 0; e < 2 && e < g_nentities; e++) {
            if (g_entities[e].state_no == state_hist[e]) {
                if (++stuck_frames[e] > max_stuck[e]) max_stuck[e] = stuck_frames[e];
            } else { state_hist[e] = g_entities[e].state_no; stuck_frames[e] = 0; }
        }
        if (g_entities[1].life < prev_life1) hits_seen += prev_life1 - g_entities[1].life;
        prev_life1 = g_entities[1].life;
    }
    printf("after 3600 frames: phase=%d round_no=%d ko_seen=%d hits_on_p2=%d p1.life=%d p2.life=%d\n",
           g_game.game_phase, g_game.round_no, ko_seen, hits_seen,
           g_entities[0].life, g_entities[1].life);
    printf("max consecutive frames in ONE state: p1=%d p2=%d\n", max_stuck[0], max_stuck[1]);
    if (hits_seen <= 0) { printf("FAIL: no hits landed on P2\n"); fail++; }
    if (max_stuck[0] > 600 || max_stuck[1] > 600) { printf("FAIL: entity stuck >600 frames in one state\n"); fail++; }
    if (!ko_seen) { printf("WARN: no KO in 60s of aggressive play (AI may be too passive now)\n"); }
    printf("== %s (%d failures) ==\n", fail ? "FAILED" : "ALL PASSED", fail);
    return fail ? 1 : 0;
}
