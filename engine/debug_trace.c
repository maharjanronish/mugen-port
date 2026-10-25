/* debug_trace.c — state machine diagnostic */
#include "mugen.h"
#include <stdio.h>

extern MgEntity g_entities[MG_MAX_ENTITIES];
extern int g_nentities;
static uint32_t g_input[2] = {0,0};
static int g_any = 0;

void mg_headless_init(int w, int h) {
    g_game.win_w = w; g_game.win_h = h;
}
void mg_headless_set_input(int player, uint32_t buttons) {
    g_input[player & 1] = buttons;
    if (buttons) g_any = 1;
}
void mg_headless_feed_entities(void) {
    for (int i = 0; i < g_nentities; i++)
        if (!g_entities[i].is_cpu) g_entities[i].buttons = g_input[i & 1];
}
int mg_platform_any_button(void);

void mg_render_sprite(MgSprite *s, int a, int b, int c, int d, int e, int f, int g2, int h2) { (void)s;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g2;(void)h2; }
void mg_fill_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) { (void)x;(void)y;(void)w;(void)h;(void)r;(void)g;(void)b;(void)a; }
void mg_render_text(MgFont *f, const char *t, int x, int y, int s, uint32_t rgb) { (void)f;(void)t;(void)x;(void)y;(void)s;(void)rgb; }
void mg_play_wav(uint8_t *d, int l, int c, int lo) { (void)d;(void)l;(void)c;(void)lo; }
void mg_play_music(const char *p, int l) { (void)p;(void)l; }
void mg_stop_channel(int c) { (void)c; }
int mg_platform_any_button(void) { return g_any; }
int mg_platform_menu_action(int player, int *delta, int *confirm, int *back) {
    (void)player; *delta=0; *confirm=0; *back=0;
    if (g_input[0] & 1024) *delta = 1;
    if (g_input[0] & 512) *delta = -1;
    if (g_input[0] & (BTN_S | BTN_A)) *confirm = 1;
    return 1;
}


int main(int argc, char **argv) {
    const char *gamedir = argc > 1 ? argv[1] : ".";
    mg_headless_init(320, 240);
    mg_engine_init(gamedir);
    if (g_game.nchars == 0) { printf("NO CHARS\n"); return 1; }

    /* skip to fight */
    g_any = 1;
    mg_engine_frame();   /* title → select */
    g_any = 0;
    g_input[0] = BTN_S;
    mg_engine_frame();   /* select: needs confirm — emulate via start button */
    g_input[0] = 0;
    printf("phase after select: %d\n", g_game.game_phase);
    if (g_game.game_phase != 2) {
        /* try one more confirm cycle */
        g_any = 1; mg_engine_frame(); g_any = 0;
        printf("phase: %d\n", g_game.game_phase);
        return (g_game.game_phase == 2) ? 0 : 1;
    }

    /* unit test: which commands match with EMPTY input? (new pipeline) */
    printf("=== command match unit test (empty history) ===\n");
    for (int i = 0; i < g_entities[0].ch->ncommands; i++) {
        MgCommand *c = &g_entities[0].ch->commands[i];
        int m = mg_check_command(&g_entities[0], c->name);
        if (m) printf("  MATCH: name=%s cmd=%s\n", c->name, c->cmd);
    }
    printf("=== end unit test ===\n");
    /* dump state 20 controllers */
    MgState *s20 = mg_find_state(&g_entities[0], 20);
    printf("=== state 20: %d ctls ===\n", s20 ? s20->nctls : -1);
    if (s20) {
        for (int i = 0; i < s20->nctls; i++) {
            MgStateCtl *c = &s20->ctls[i];
            printf("  ctl[%d] type=%s ntrig_all=%d ntrig_idx=%d", i, c->type, c->ntrig_all, c->ntrig_idx);
            for (int j = 0; j < c->nparams; j++)
                printf(" | %s=%s", c->pnames[j], c->pvals[j]);
            printf("\n");
        }
    }
    /* font check */
    for (int i = 0; i < 2; i++) {
        MgFont *f = g_game.fonts[i];
        if (!f) { printf("font[%d] = NULL\n", i); continue; }
        int ngl = 0;
        for (int c = 0; c < 256; c++) if (f->glyphs[c]) ngl++;
        printf("font[%d] '%s': %d glyphs, A=%dx%d sw=%d\n", i, "f", ngl,
               f->gw['A'], f->gh['A'], f->size_w);
    }
    /* glyph A pixel dump */
    {
        MgFont *f = g_game.fonts[1];
        if (f && f->glyphs['A']) {
            printf("glyph A %dx%d:\n", f->gw['A'], f->gh['A']);
            for (int y = 0; y < f->gh['A']; y++) {
                for (int x = 0; x < f->gw['A']; x++)
                    printf("%c", (f->glyphs['A'][y*f->gw['A']+x] & 0xFF) ? '#' : '.');
                printf("\n");
            }
        } else printf("no glyph A\n");
        /* draw text test */
        if (f) mg_render_text(f, "HELLO", 100, 100, 4, 0xFFD040);
    }
    /* expression evaluation unit test */
    printf("=== expr tests ===\n");
    {
        MgEntity *e = &g_entities[0];
        e->vel_x = 2.4; e->anim_no = 0;
        const char *tests[] = {"vel x > 0", "Anim != 20 && Anim != 5", "Anim != 20", "Anim != 5", "vel x", "0"};
        for (int i = 0; i < 6; i++) {
            MgExpr *ex = mg_expr_parse(tests[i]);
            if (!ex) { printf("  [%s] PARSE FAILED\n", tests[i]); continue; }
            MgVal v = mg_expr_eval(ex, e);
            printf("  [%s] -> %lld (f=%d)\n", tests[i], (long long)(v.is_float ? (int64_t)v.f : v.i), v.is_float);
            mg_expr_free(ex);
        }
    }
    /* test with FWD held: run pipeline 10 frames then check */
    for (int i = 0; i < 10; i++) {
        g_entities[0].buttons = BTN_FWD;
        mg_input_update(&g_entities[0]);
        mg_commands_step(&g_entities[0]);
    }
    g_entities[0].buttons = 0;
    printf("=== commands matching with FWD held ===\n");
    for (int i = 0; i < g_entities[0].ch->ncommands; i++) {
        MgCommand *c = &g_entities[0].ch->commands[i];
        int m = mg_check_command(&g_entities[0], c->name);
        if (m) printf("  MATCH: name=%s cmd=%s\n", c->name, c->cmd);
    }
    /* punch test: A held 1 frame after idle */
    for (int i = 0; i < 10; i++) {
        g_entities[0].buttons = 0;
        mg_input_update(&g_entities[0]);
        mg_commands_step(&g_entities[0]);
    }
    g_entities[0].buttons = BTN_A;
    mg_input_update(&g_entities[0]);
    mg_commands_step(&g_entities[0]);
    g_entities[0].buttons = 0;
    printf("=== commands matching with A pressed ===\n");
    for (int i = 0; i < g_entities[0].ch->ncommands; i++) {
        MgCommand *c = &g_entities[0].ch->commands[i];
        int m = mg_check_command(&g_entities[0], c->name);
        if (m) printf("  MATCH: name=%s cmd=%s\n", c->name, c->cmd);
    }
    printf("=== end ===\n");

    g_game.world_scale = 1.0;
    MgStage *st = g_game.stages[g_game.selected_stage];
    g_game.world_w = (int)st->localcoord_w;
    g_game.world_h = (int)st->localcoord_h;

    /* jump through round intro */
    for (int i = 0; i < 130; i++) { mg_headless_feed_entities(); mg_engine_frame(); }
    printf("round_state=%d (should be 2=fighting)\n", g_game.round_state);

    /* TEST A: hold forward 120 frames — expect pos_x to increase ~2.5px/frame */
    g_input[0] = BTN_FWD;
    for (int i = 0; i < 120; i++) {
        mg_headless_feed_entities();
        mg_engine_frame();
        if (i % 30 == 0) {
            MgEntity *e = &g_entities[0];
            printf("A[%3d] st=%4d anim=%4d statetime=%3d pos=%7.1f vel=%5.2f ctrl=%d cmds=[%s]\n",
                   i, e->state_no, e->anim_no, e->state_time, e->pos_x, e->vel_x, e->ctrl,
                   e->ncmd ? e->cmdbuf[0].name : "-");
        }
    }
    g_input[0] = 0;

    /* TEST B: punch (a) — expect state 200 */
    g_input[0] = BTN_A;
    for (int i = 0; i < 10; i++) { mg_headless_feed_entities(); mg_engine_frame(); }
    g_input[0] = 0;
    for (int i = 0; i < 20; i++) { mg_headless_feed_entities(); mg_engine_frame(); }
    MgEntity *e = &g_entities[0];
    printf("B: after punch: st=%d anim=%d attacking=%d movetype=%c\n",
           e->state_no, e->anim_no, e->attacking, e->movetype);

    /* TEST C: walk P1 next to P2 and punch — expect damage */
    g_entities[1].pos_x = g_entities[0].pos_x + 45;
    g_entities[0].facing = 1;
    g_entities[1].facing = -1;
    g_entities[1].is_cpu = 0;   /* freeze P2 input for deterministic test */
    g_entities[1].buttons = 0;
    g_input[0] = BTN_A;
    for (int i = 0; i < 14; i++) {
        mg_headless_feed_entities();
        g_entities[1].buttons = 0;
        mg_engine_frame();
        MgEntity *e0 = &g_entities[0], *e1 = &g_entities[1];
        if (i < 10 || e1->life < 1000)
            printf("C[%2d] p1: st=%d atk=%d nclsn1=%d anim=%d | p2: st=%d life=%d nclsn2=%d\n",
                   i, e0->state_no, e0->attacking, e0->nclsn1, e0->anim_no,
                   e1->state_no, e1->life, e1->nclsn2);
    }
    g_input[0] = 0;
    for (int i = 0; i < 20; i++) {
        mg_headless_feed_entities();
        g_entities[1].buttons = 0;
        mg_engine_frame();
    }
    printf("C: p1life=%d p2life=%d p2state=%d p2movetype=%c\n",
           g_entities[0].life, g_entities[1].life, g_entities[1].state_no, g_entities[1].movetype);
    return 0;
}
