/* game.c — engine init, select screen, title, victory, game flow */
#include "mugen.h"

/* globals live in state.c: g_game, g_entities, g_nentities */
extern MgGame g_game;

void mg_load_fight_def(const char *gamedir);
void mg_load_fightfx(const char *gamedir);

void mg_engine_init(const char *gamedir) {
    memset(&g_game, 0, sizeof(g_game));
    strncpy(g_game.base_dir, gamedir, 500);
    g_game.win_w = MG_DEFAULT_W;
    g_game.win_h = MG_DEFAULT_H;
    g_game.running = 1;
    g_game.title_bg_rgb = 0x101820;

    /* fight.def HUD defaults (320x240 space, scaled at render) */
    g_game.hud.p1_life_x = 14; g_game.hud.p1_life_y = 12; g_game.hud.p1_life_w = 240; g_game.hud.p1_life_h = 9;
    g_game.hud.p2_life_x = MG_DEFAULT_W - 254; g_game.hud.p2_life_y = 12; g_game.hud.p2_life_w = 240; g_game.hud.p2_life_h = 9;
    g_game.hud.p1_power_x = 14; g_game.hud.p1_power_y = 24; g_game.hud.p1_power_w = 240; g_game.hud.p1_power_h = 7;
    g_game.hud.p2_power_x = MG_DEFAULT_W - 254; g_game.hud.p2_power_y = 24; g_game.hud.p2_power_w = 240; g_game.hud.p2_power_h = 7;
    g_game.hud.p1_face_x = 12; g_game.hud.p1_face_y = 32; g_game.hud.p1_face_w = 48; g_game.hud.p1_face_h = 48;
    g_game.hud.p2_face_x = MG_DEFAULT_W - 60; g_game.hud.p2_face_y = 32; g_game.hud.p2_face_w = 48; g_game.hud.p2_face_h = 48;
    g_game.hud.name_p1_x = 14; g_game.hud.name_p1_y = 82; g_game.hud.name_p2_x = MG_DEFAULT_W - 14;
    g_game.hud.name_p2_y = 82;
    g_game.hud.p1_life_front = 0xFF2020u | 0x00u;   /* set below properly */
    g_game.hud.p1_life_front = (255u << 16) | (64u << 8) | 64u;
    g_game.hud.p2_life_front = (255u << 16) | (64u << 8) | 64u;
    g_game.hud.p1_power_front = (64u << 16) | (128u << 8) | 255u;
    g_game.hud.p2_power_front = (64u << 16) | (128u << 8) | 255u;

    /* load fonts */
    char *fdir = mg_path_join(gamedir, "font");
    const char *fonts[] = {"f-4x6.def", "f-6x9.def"};
    for (int i = 0; i < 2; i++)
        g_game.fonts[i] = mg_font_load(fdir, fonts[i]);
    free(fdir);

    /* fight.def HUD config */
    mg_load_fight_def(gamedir);
    mg_load_fightfx(gamedir);

    /* roster from select.def */
    mg_load_select();
}

void mg_load_fight_def(const char *gamedir) {
    char *p = mg_path_join(gamedir, "data/fight.def");
    char *text = mg_read_file(p, NULL);
    free(p);
    if (!text) { fprintf(stderr, "[game] no fight.def; using defaults\n"); return; }
    char buf[256];
    /* [Lifebar] */
    if (mg_def_get(text, "Lifebar", "p1.pos", buf, sizeof(buf))) {
        int v[2] = {g_game.hud.p1_life_x, g_game.hud.p1_life_y};
        mg_parse_ints(buf, v, 2);
        /* fight.def coords are in 320x240 space; scale to window */
        g_game.hud.p1_life_x = v[0] * 4; g_game.hud.p1_life_y = v[1] * 4;
    }
    if (mg_def_get(text, "Lifebar", "p2.pos", buf, sizeof(buf))) {
        int v[2]; mg_parse_ints(buf, v, 2);
        g_game.hud.p2_life_x = v[0] * 4 - 240; g_game.hud.p2_life_y = v[1] * 4;
    }
    if (mg_def_get(text, "Powerbar", "p1.pos", buf, sizeof(buf))) {
        int v[2]; mg_parse_ints(buf, v, 2);
        g_game.hud.p1_power_x = v[0] * 4; g_game.hud.p1_power_y = v[1] * 4;
    }
    if (mg_def_get(text, "Powerbar", "p2.pos", buf, sizeof(buf))) {
        int v[2]; mg_parse_ints(buf, v, 2);
        g_game.hud.p2_power_x = v[0] * 4 - 240; g_game.hud.p2_power_y = v[1] * 4;
    }
    free(text);
}

void mg_load_select(void) {
    char *p = mg_path_join(g_game.base_dir, "data/select.def");
    char *text = mg_read_file(p, NULL);
    free(p);
    if (!text) { fprintf(stderr, "[game] no select.def\n"); return; }

    MgTextIter it;
    memset(&it, 0, sizeof(it));
    it.text = text; it.len = strlen(text);
    int in_roster = 0;
    while (mg_text_next(&it)) {
        if (mg_str_ieq(it.section, "Characters") || mg_str_ieq(it.section, "Select")) in_roster = 1;
        else if (it.section[0]) in_roster = mg_str_ieq(it.section, "Characters") || mg_str_ieq(it.section, "Select");
        if (in_roster && it.name[0] && it.value[0] == 0) {
            /* char line: path or name (possibly with ", stage" suffix in name) */
            if (strncmp(it.name, ";", 1) == 0) continue;
            char path[512];
            char entry[256];
            strncpy(entry, it.name, 255); entry[255] = 0;
            char *comma = strchr(entry, ',');
            if (comma) *comma = 0;   /* strip ", stage" */
            mg_trim(entry);
            if (!entry[0]) continue;
            snprintf(path, sizeof(path), "%s/chars/%s/%s.def", g_game.base_dir, entry, entry);
            mg_path_fix(path);
            if (!mg_file_exists(path)) {
                /* try direct as given */
                snprintf(path, sizeof(path), "%s/%s", g_game.base_dir, entry);
                mg_path_fix(path);
            }
            if (mg_file_exists(path) && g_game.nchars < 64) {
                MgChar *ch = mg_char_load(path);
                if (ch) {
                    g_game.chars[g_game.nchars] = ch;
                    strncpy(g_game.char_names[g_game.nchars], ch->displayname[0] ? ch->displayname : ch->name, 127);
                    g_game.nchars++;
                }
            } else {
                fprintf(stderr, "[select] char not found: %s\n", entry);
            }
        }
    }
    free(text);

    /* stages: scan stages/*.def */
    char stagesdir[600];
    snprintf(stagesdir, sizeof(stagesdir), "%s/stages", g_game.base_dir);
    const char *stagefiles[] = {"stage0.def"};
    for (int i = 0; i < 1; i++) {
        char sp[700];
        snprintf(sp, sizeof(sp), "%s/%s", stagesdir, stagefiles[i]);
        mg_path_fix(sp);
        if (mg_file_exists(sp) && g_game.nstages < 16) {
            MgStage *st = mg_stage_load(sp);
            if (st) {
                g_game.stages[g_game.nstages] = st;
                strncpy(g_game.stage_names[g_game.nstages], st->name, 127);
                g_game.nstages++;
            }
        }
    }
    fprintf(stderr, "[game] roster: %d chars, %d stages\n", g_game.nchars, g_game.nstages);
}

/* ---------------- title ---------------- */
void mg_title_frame(void) {
    /* any button → select */
    for (int i = 0; i < g_nentities; i++) (void)i;
    extern int mg_platform_any_button(void);
    if (mg_platform_any_button()) g_game.game_phase = 1;
}
void mg_title_render(void) {
    mg_fill_rect(0, 0, g_game.win_w, g_game.win_h,
                 (g_game.title_bg_rgb >> 16) & 0xFF, (g_game.title_bg_rgb >> 8) & 0xFF, g_game.title_bg_rgb & 0xFF, 255);
    if (g_game.fonts[1]) {
        const char *t = "M.U.G.E.N";
        int tw = strlen(t) * (g_game.fonts[1]->size_w + 2) * 6;
        mg_render_text(g_game.fonts[1], t, g_game.win_w/2 - tw/2, g_game.win_h/4, 6, 0xFFD040);
        const char *t2 = "ANDROID PORT";
        int tw2 = strlen(t2) * (g_game.fonts[1]->size_w + 2) * 3;
        mg_render_text(g_game.fonts[1], t2, g_game.win_w/2 - tw2/2, g_game.win_h/4 + 80, 3, 0x8090A0);
        const char *t3 = "PRESS ANY BUTTON";
        if ((g_game.frames_done / 30) % 2 == 0) {
            int tw3 = strlen(t3) * (g_game.fonts[1]->size_w + 2) * 2;
            mg_render_text(g_game.fonts[1], t3, g_game.win_w/2 - tw3/2, g_game.win_h*3/4, 2, 0xFFFFFF);
        }
    }
}

/* ---------------- select ---------------- */
void mg_select_frame(void) {
    extern int mg_platform_menu_action(int player, int *delta, int *confirm, int *back);
    int delta = 0, confirm = 0, back = 0;
    static int cur[2] = {0, 0};
    g_game.menu_index = cur[0];

    if (mg_platform_menu_action(0, &delta, &confirm, &back)) {
        cur[0] = (cur[0] + delta + g_game.nchars) % g_game.nchars;
        if (confirm) {
            g_game.selected[0] = cur[0];
            /* P2: cycle CPU by holding? simple: P2 = next char, CPU */
            g_game.selected[1] = (cur[0] + 1) % g_game.nchars;
            g_game.p2_cpu = 1;
            g_game.selected_stage = 0;
            g_game.game_phase = 2;
            mg_fight_start();
        }
        if (back) g_game.game_phase = 0;
    }
    g_game.menu_index = cur[0];
    g_game.frames_done++;
}
void mg_select_render(void) {
    mg_fill_rect(0, 0, g_game.win_w, g_game.win_h, 16, 16, 32, 255);
    if (g_game.fonts[1]) {
        const char *t = "SELECT YOUR FIGHTER";
        int tw = strlen(t) * (g_game.fonts[1]->size_w + 2) * 3;
        mg_render_text(g_game.fonts[1], t, g_game.win_w/2 - tw/2, 40, 3, 0xFFD040);
    }
    /* grid of portraits (9000,1 big or 9000,0) */
    int cols = 6;
    int cw = 120, chh = 130;
    for (int i = 0; i < g_game.nchars; i++) {
        int cx = 80 + (i % cols) * (cw + 20);
        int cy = 100 + (i / cols) * (chh + 20);
        MgSprite *spr = mg_sff_get(g_game.chars[i]->sff, 9000, 0);
        if (!spr) spr = mg_sff_get(g_game.chars[i]->sff, 9000, 1);
        if (spr) {
            double sc = 100.0 / spr->w;
            mg_render_sprite(spr, cx, cy, (int)(spr->w * sc), (int)(spr->h * sc), 0, 0, -1, -1);
        }
        if (g_game.fonts[0])
            mg_render_text(g_game.fonts[0], g_game.char_names[i], cx, cy + 102, 1, 0xC0C0C0);
        if (i == g_game.menu_index)
            mg_fill_rect(cx - 4, cy - 4, 108, 108, 255, 255, 255, 40);
    }
}

/* ---------------- victory ---------------- */
void mg_victory_frame(void) {
    extern int mg_platform_menu_action(int player, int *delta, int *confirm, int *back);
    int delta = 0, confirm = 0, back = 0;
    if (mg_platform_menu_action(0, &delta, &confirm, &back)) {
        if (confirm) {
            g_game.game_phase = 1;   /* back to select */
            g_game.announce_timer = 0;
        }
    }
    g_game.frames_done++;
}
void mg_victory_render(void) {
    mg_fight_render();
    if (g_game.fonts[1] && g_game.announce_text[0]) {
        int tw = strlen(g_game.announce_text) * (g_game.fonts[1]->size_w + 2) * 4;
        mg_render_text(g_game.fonts[1], g_game.announce_text, g_game.win_w/2 - tw/2, g_game.win_h/3, 4, 0xFFD040);
        const char *t = "PRESS START TO CONTINUE";
        int tw2 = strlen(t) * (g_game.fonts[1]->size_w + 2) * 2;
        mg_render_text(g_game.fonts[1], t, g_game.win_w/2 - tw2/2, g_game.win_h*2/3, 2, 0xFFFFFF);
    }
}

/* ---------------- main loop tick (platform calls this at 60Hz) ---------------- */
void mg_engine_frame(void) {
    switch (g_game.game_phase) {
        case 0: mg_title_frame(); break;
        case 1: mg_select_frame(); break;
        case 2: mg_fight_frame(); break;
        case 3: mg_victory_frame(); break;
    }
}

void mg_engine_render(void) {
    switch (g_game.game_phase) {
        case 0: mg_title_render(); break;
        case 1: mg_select_render(); break;
        case 2: mg_fight_render(); break;
        case 3: mg_victory_render(); break;
    }
}

int mg_engine_running(void) { return g_game.running; }
