/* fight.c — match flow, physics, hit detection, camera, HUD, explods */
#include "mugen.h"

/* globals defined in state.c: g_game, g_entities, g_nentities */

double mg_stage_left_edge(void) { return g_game.camera_x + 0; }
double mg_stage_right_edge(void) { return g_game.camera_x + g_game.world_w; }
double mg_stage_floor(void) { return 0; }

/* ---------------- explods ---------------- */
#define MG_MAX_EXPLODS 32
typedef struct {
    int active;
    MgSprite *spr;
    MgAnim *anim;   /* fightfx anim */
    int anim_time, pos_x, pos_y;
    int removeongethit;
    int entity_idx;   /* owner index */
    int sprpriority;
    int removetime;
    int scale_x, scale_y;
} MgExplod;
static MgExplod g_explods[MG_MAX_EXPLODS];

static MgSff *g_fightfx_sff = NULL;
static MgAir *g_fightfx_air = NULL;

void mg_load_fightfx(const char *gamedir) {
    char *p = mg_path_join(gamedir, "data/fightfx.sff");
    g_fightfx_sff = mg_sff_load(p); free(p);
    p = mg_path_join(gamedir, "data/fightfx.air");
    g_fightfx_air = mg_air_load(p); free(p);
}

void mg_spawn_explod(MgEntity *en, MgStateCtl *c) {
    /* params: anim, spr, pos, removetime, sprpriority, own (ignored) */
    int anim = -1;
    const char *v;
    int gid = 0, num = 0;
    if ((v = c->pvals[0])) {} /* unused */
    for (int i = 0; i < c->nparams; i++) {
        if (mg_str_ieq(c->pnames[i], "anim")) anim = mg_geti(c->pvals[i], -1);
        if (mg_str_ieq(c->pnames[i], "spr")) {
            int vv[2] = {0,0}; mg_parse_ints(c->pvals[i], vv, 2); gid = vv[0]; num = vv[1];
        }
    }
    int px = 0, py = 0;
    for (int i = 0; i < c->nparams; i++) {
        if (mg_str_ieq(c->pnames[i], "pos")) {
            int vv[2] = {0,0}; mg_parse_ints(c->pvals[i], vv, 2); px = vv[0]; py = vv[1];
        }
    }
    for (int i = 0; i < MG_MAX_EXPLODS; i++) {
        if (!g_explods[i].active) {
            MgExplod *ex = &g_explods[i];
            memset(ex, 0, sizeof(*ex));
            ex->active = 1;
            ex->anim = g_fightfx_air ? mg_air_get(g_fightfx_air, anim) : NULL;
            ex->spr = g_fightfx_sff ? mg_sff_get(g_fightfx_sff, gid, num) : NULL;
            ex->pos_x = (int)(en->pos_x + (en->facing > 0 ? px : -px));
            ex->pos_y = (int)(en->pos_y - py);
            ex->anim_time = 0;
            for (int q = 0; q < c->nparams; q++)
                if (mg_str_ieq(c->pnames[q], "removetime")) ex->removetime = mg_geti(c->pvals[q], -2);
            for (int q = 0; q < c->nparams; q++)
                if (mg_str_ieq(c->pnames[q], "sprpriority")) ex->sprpriority = mg_geti(c->pvals[q], 0);
            ex->entity_idx = (int)(en - g_entities);
            break;
        }
    }
}

void mg_remove_explods(MgEntity *en) {
    int idx = (int)(en - g_entities);
    for (int i = 0; i < MG_MAX_EXPLODS; i++)
        if (g_explods[i].active && g_explods[i].entity_idx == idx)
            g_explods[i].active = 0;
}

void mg_spawn_dust(MgEntity *en) {
    /* common dust: fightfx anim 120 (classic) */
    for (int i = 0; i < MG_MAX_EXPLODS; i++) {
        if (!g_explods[i].active) {
            MgExplod *ex = &g_explods[i];
            memset(ex, 0, sizeof(*ex));
            ex->active = 1;
            ex->anim = g_fightfx_air ? mg_air_get(g_fightfx_air, 120) : NULL;
            ex->pos_x = (int)en->pos_x;
            ex->pos_y = (int)en->pos_y;
            ex->removetime = -2;
            break;
        }
    }
}

void mg_spawn_hitspark(int x, int y, int guard) {
    /* fightfx: hit spark anim 0 (guard 10's typically); use sprite 0,0 fallback */
    for (int i = 0; i < MG_MAX_EXPLODS; i++) {
        if (!g_explods[i].active) {
            MgExplod *ex = &g_explods[i];
            memset(ex, 0, sizeof(*ex));
            ex->active = 1;
            ex->anim = g_fightfx_air ? mg_air_get(g_fightfx_air, guard ? 10 : 0) : NULL;
            if (!ex->anim && g_fightfx_sff)
                ex->spr = mg_sff_get(g_fightfx_sff, guard ? 1 : 0, 0);
            ex->pos_x = x; ex->pos_y = y;
            ex->removetime = -2;
            break;
        }
    }
}

/* ---------------- projectiles (MVP: used by chars that need them) ---------------- */
#define MG_MAX_PROJ 8
typedef struct {
    int active;
    double pos_x, pos_y, vel_x, vel_y;
    MgSprite *spr; MgAnim *anim; int anim_time;
    int facing, removetime, damage;
    int owner_idx;
    int hit_once[MG_MAX_ENTITIES];
} MgProj;
static MgProj g_projs[MG_MAX_PROJ];

void mg_spawn_projectile(MgEntity *en, MgStateCtl *c) {
    for (int i = 0; i < MG_MAX_PROJ; i++) {
        if (!g_projs[i].active) {
            MgProj *p = &g_projs[i];
            memset(p, 0, sizeof(*p));
            p->active = 1;
            p->owner_idx = (int)(en - g_entities);
            p->pos_x = en->pos_x + en->facing * 20;
            p->pos_y = en->pos_y - 40;
            p->vel_x = en->facing * 4.0;
            p->damage = 50;
            int gid = 0, num = 0, anim = -1;
            for (int q = 0; q < c->nparams; q++) {
                if (mg_str_ieq(c->pnames[q], "projid")) {}
                if (mg_str_ieq(c->pnames[q], "projremovetime")) p->removetime = mg_geti(c->pvals[q], 300);
                if (mg_str_ieq(c->pnames[q], "velocity")) {
                    double vv[2] = {4,0}; mg_parse_floats(c->pvals[q], vv, 2);
                    p->vel_x = en->facing * vv[0]; p->vel_y = vv[1];
                }
                if (mg_str_ieq(c->pnames[q], "projanim")) anim = mg_geti(c->pvals[q], -1);
                if (mg_str_ieq(c->pnames[q], "projspr")) {
                    int vv[2] = {0,0}; mg_parse_ints(c->pvals[q], vv, 2); gid = vv[0]; num = vv[1];
                }
                if (mg_str_ieq(c->pnames[q], "damage")) p->damage = mg_geti(c->pvals[q], 50);
                if (mg_str_ieq(c->pnames[q], "offset")) {
                    double vv[2] = {0,0}; mg_parse_floats(c->pvals[q], vv, 2);
                    p->pos_x = en->pos_x + en->facing * vv[0];
                    p->pos_y = en->pos_y - vv[1];
                }
            }
            p->anim = en->ch->air ? mg_air_get(en->ch->air, anim) : NULL;
            p->spr = en->ch->sff ? mg_sff_get(en->ch->sff, gid, num) : NULL;
            if (!p->anim && !p->spr && g_fightfx_sff)
                p->spr = mg_sff_get(g_fightfx_sff, gid, num);
            p->facing = en->facing;
            break;
        }
    }
}

/* ---------------- target sctrls (applied to hit victims) ---------------- */
void mg_apply_target_sctrl(MgEntity *en, const char *type, MgStateCtl *c) {
    MgEntity *p2 = mg_p2_of(en);
    if (!p2) return;
    /* only affects entities currently being hit by en */
    if (mg_str_ieq(type, "targetlifeadd")) {
        int val = 0;
        for (int i = 0; i < c->nparams; i++)
            if (mg_str_ieq(c->pnames[i], "value")) val = mg_geti(c->pvals[i], 0);
        p2->life -= val;
        if (p2->life < 0) p2->life = 0;
    } else if (mg_str_ieq(type, "targetstate")) {
        int val = -1;
        for (int i = 0; i < c->nparams; i++)
            if (mg_str_ieq(c->pnames[i], "value")) val = mg_geti(c->pvals[i], -1);
        if (val >= 0) mg_change_state(p2, val, -1, -1);
    } else if (mg_str_ieq(type, "targetfacing")) {
        p2->facing = en->facing;
    } else if (mg_str_ieq(type, "targetvelset")) {
        for (int i = 0; i < c->nparams; i++) {
            if (mg_str_ieq(c->pnames[i], "x")) p2->vel_x = mg_getf(c->pvals[i], p2->vel_x);
            if (mg_str_ieq(c->pnames[i], "y")) p2->vel_y = mg_getf(c->pvals[i], p2->vel_y);
        }
    } else if (mg_str_ieq(type, "targetbind")) {
        /* simplified: keep victim at attacker side */
        int x = 0, y = 0;
        for (int i = 0; i < c->nparams; i++) {
            if (mg_str_ieq(c->pnames[i], "pos")) {
                int vv[2] = {0,0}; mg_parse_ints(c->pvals[i], vv, 2); x = vv[0]; y = vv[1];
            }
        }
        p2->pos_x = en->pos_x + en->facing * x;
        p2->pos_y = en->pos_y - y;
    }
}

/* ---------------- physics ---------------- */
static void apply_physics(MgEntity *en) {
    MgChar *ch = en->ch;
    if (en->pos_freeze) { en->vel_x = 0; en->vel_y = 0; return; }

    if (en->physics == 'A') {
        en->vel_y += mg_char_const(ch, "movement.yaccel", 0.44);
        if (en->vel_y > 8.0) en->vel_y = 8.0;
    } else if (en->physics == 'S' || en->physics == 'C') {
        if (en->vel_y < 0) {
            en->vel_y += mg_char_const(ch, "data.yaccel", 0.44);
        } else {
            /* ground friction */
            double fric = (en->physics == 'C') ?
                mg_char_const(ch, "movement.crouch.friction", 0.85) :
                mg_char_const(ch, "movement.stand.friction", 0.85);
            if (en->movetype != 'H' || 1) {
                en->vel_x *= fric;
                double th = (en->physics == 'C') ?
                    mg_char_const(ch, "movement.crouch.friction.threshold", 0.2) :
                    mg_char_const(ch, "movement.stand.friction.threshold", 0.2);
                if (fabs(en->vel_x) < th) en->vel_x = 0;
            }
            en->vel_y = 0;
        }
    }
    /* apply velocity */
    en->pos_x += en->vel_x;
    en->pos_y += en->vel_y;

    /* ground: y >= 0 means below floor */
    if (en->pos_y >= 0 && en->vel_y >= 0 && en->statetype != 'A') {
        en->pos_y = 0;
    }
    if (en->pos_y > 0) {
        /* fell to ground while in air */
        if (en->statetype == 'A' && en->movetype != 'H') {
            /* landing handled by common states via statetype set; engine floors */
        }
        if (en->pos_y >= 0 && en->gethit.fall_fall) {
            en->pos_y = 0;
        }
        if (en->pos_y > 0) en->pos_y = en->pos_y; /* let states manage */
    }
}

/* ---------------- hit detection ---------------- */
static int rects_overlap(double ax1, double ay1, double ax2, double ay2,
                         double bx1, double by1, double bx2, double by2) {
    return ax1 < bx2 && bx1 < ax2 && ay1 < by2 && by1 < ay2;
}

static double ent_front(MgEntity *en) {
    return en->pos_x + en->facing * mg_char_const(en->ch, "size.ground.front", 16);
}
static double ent_back(MgEntity *en) {
    return en->pos_x - en->facing * mg_char_const(en->ch, "size.ground.back", 12);
}

static void do_hit(MgEntity *att, MgEntity *vic, int guarded) {
    (void)att;
    (void)vic;
    (void)guarded;
}

/* parse attr "AAN" style: SCA / NA / SA / HA etc + attack type */
static int attr_is_attack(const char *attr) {
    /* attr like "A, AT" — hitdefattr usage: "SCA, NA, AT" */
    (void)attr;
    return 1;
}

static int guardflag_matches(MgEntity *vic, const char *guardflag) {
    if (!guardflag || !guardflag[0]) return 0;
    char want = 0;
    switch (vic->statetype) {
        case 'S': want = 'S'; break;
        case 'C': want = 'C'; break;
        case 'A': want = 'A'; break;
        case 'L': want = 'L'; break;
    }
    for (const char *p = guardflag; *p; p++) {
        char c = *p | 32;
        if (c == want) return 1;
    }
    return 0;
}

static int hitflag_matches(MgEntity *vic, const char *hitflag) {
    if (!hitflag || !hitflag[0]) return 1;   /* default: HAF */
    char want = 0;
    if (vic->movetype == 'H') want = 'H';
    else if (vic->statetype == 'L') want = 'L';
    else if (vic->statetype == 'A') want = 'A';
    else want = 'F';   /* stand/crouch grounded normal */
    for (const char *p = hitflag; *p; p++) {
        char c = *p | 32;
        if (c == want) return 1;
        if (c == 'd' && vic->statetype == 'L') return 1;   /* downed */
    }
    return 0;
}

static void apply_hit(MgEntity *att, MgEntity *vic) {
    if (!att->attacking || !vic->alive) return;
    /* hitdef active only during movetype A */
    if (att->movetype != 'A') return;

    /* attacker clsn1 vs victim clsn2 */
    double a1 = 1e9, a2 = -1e9, b1 = 1e9, b2 = -1e9;
    for (int i = 0; i < att->nclsn1; i++) {
        MgClsn *cl = &att->clsn1[i];
        double x1 = att->pos_x + (att->facing > 0 ? cl->x1 : -cl->x2);
        double x2 = att->pos_x + (att->facing > 0 ? cl->x2 : -cl->x1);
        double y1 = att->pos_y - cl->y2, y2 = att->pos_y - cl->y1;
        if (x1 < a1) a1 = x1; if (x2 > a2) a2 = x2;
        if (y1 < b1) b1 = y1; if (y2 > b2) b2 = y2;
    }
    double c1 = 1e9, c2 = -1e9, d1 = 1e9, d2 = -1e9;
    for (int i = 0; i < vic->nclsn2; i++) {
        MgClsn *cl = &vic->clsn2[i];
        double x1 = vic->pos_x + (vic->facing > 0 ? cl->x1 : -cl->x2);
        double x2 = vic->pos_x + (vic->facing > 0 ? cl->x2 : -cl->x1);
        double y1 = vic->pos_y - cl->y2, y2 = vic->pos_y - cl->y1;
        if (x1 < c1) c1 = x1; if (x2 > c2) c2 = x2;
        if (y1 < d1) d1 = y1; if (y2 > d2) d2 = y2;
    }
    if (a1 > a2 || c1 > c2) return;   /* no boxes */
    if (!rects_overlap(a1, b1, a2, b2, c1, d1, c2, d2)) return;

    /* once per hitdef per victim */
    if (att->hitdef.already_hit_vic[vic - g_entities]) return;
    att->hitdef.already_hit_vic[vic - g_entities] = 1;
    att->hitdef_used = 1;

    if (!hitflag_matches(vic, att->hitdef.hitflag_str)) return;

    /* guard check: victim holding back + in guard dist + guardable statetype */
    int can_guard = 0;
    uint32_t back = (vic->facing > 0) ? BTN_BACK : BTN_FWD;
    if ((vic->buttons & back) && vic->ctrl && guardflag_matches(vic, att->hitdef.guardflag_str))
        can_guard = 1;
    if (vic->special_nostandguard && vic->statetype == 'S') can_guard = 0;
    if (vic->special_nocrouchguard && vic->statetype == 'C') can_guard = 0;
    if (vic->special_noairguard && vic->statetype == 'A') can_guard = 0;

    double spark_x = (ent_front(att) + ent_back(vic)) / 2;
    double spark_y = vic->pos_y - mg_char_const(vic->ch, "size.mid.pos.y", -60) * 0 - 60;

    if (can_guard) {
        /* GUARD */
        double dmg = att->hitdef.guarddamage > 0 ? att->hitdef.guarddamage : att->hitdef.damage;
        double defence = mg_char_const(vic->ch, "data.defence", 100);
        dmg = dmg * 100.0 / defence * (vic->DefenceMul > 0 ? vic->DefenceMul : 1);
        if (dmg < 0) dmg = 0;
        vic->life -= (int)dmg;
        if (vic->life < 0) vic->life = 0;
        vic->guard_count++;
        att->move_guarded = 1;
        att->guard_count++;
        vic->power += 5;   /* guard power gain */
        att->power += att->hitdef.power_add > 0 ? 0 : 0;
        /* guard pushback */
        double gv = att->hitdef.guard_velocity_x;
        if (gv == 0) gv = att->hitdef.ground_velocity_x * 0.5;
        vic->vel_x = -vic->facing * fabs(gv);
        vic->vel_y = 0;
        /* guard spark + sound */
        mg_spawn_hitspark((int)spark_x, (int)spark_y, 1);
        /* attacker pushback */
        att->vel_x = -att->facing * fabs(att->hitdef.ground_velocity_x) * 0.3;
        /* victim enters guard states via common (state 150 family) if ctrl */
        if (vic->ctrl) {
            int gst = vic->statetype == 'A' ? 154 : (vic->statetype == 'C' ? 153 : 152);
            mg_change_state(vic, 150, -1, -1);
            (void)gst;
        }
        vic->gethit.hittime_left = att->hitdef.guard_pausetime > 0 ? (int)att->hitdef.guard_pausetime : 12;
        vic->hitpause_time = vic->gethit.hittime_left;
        att->hitpause_time = (int)(att->hitdef.guard_pausetime > 0 ? att->hitdef.guard_pausetime : 12) / 2;
    } else {
        /* CLEAN HIT */
        double dmg = att->hitdef.hitdamage > 0 ? att->hitdef.hitdamage : att->hitdef.damage;
        double defence = mg_char_const(vic->ch, "data.defence", 100);
        dmg = dmg * 100.0 / defence * (vic->DefenceMul > 0 ? vic->DefenceMul : 1);
        if (dmg < 1) dmg = 1;
        vic->life -= (int)dmg;
        if (vic->life < 0) vic->life = 0;
        vic->hit_count++;
        att->move_hit = 1;
        att->move_contact = 1;
        att->hit_count++;
        att->uniq_hit_count++;
        att->power += abs(att->hitdef.power_add);
        if (att->power > att->powermax) att->power = att->powermax;
        vic->power += 8;

        /* gethitvars */
        vic->gethit.xvel = -vic->facing * fabs(att->hitdef.ground_velocity_x);
        if (vic->statetype == 'A') {
            vic->gethit.xvel = -vic->facing * fabs(att->hitdef.air_velocity_x);
            vic->gethit.yvel = att->hitdef.air_velocity_y;
        } else if (vic->statetype == 'L') {
            vic->gethit.xvel = -vic->facing * fabs(att->hitdef.down_velocity_x);
            vic->gethit.yvel = att->hitdef.down_velocity_y;
        } else {
            vic->gethit.xvel = -vic->facing * fabs(att->hitdef.ground_velocity_x);
            vic->gethit.yvel = att->hitdef.ground_velocity_y;
        }
        vic->gethit.fall_fall = att->hitdef.fall_fall;
        vic->gethit.hitshaketime_left = att->hitdef.pausetime > 0 ? (int)att->hitdef.pausetime : 10;
        vic->gethit.hittime_left = 14;
        vic->gethit.hitcount = att->hit_count;

        /* pause both */
        vic->hitpause_time = vic->gethit.hitshaketime_left;
        att->hitpause_time = att->hitdef.pausetime > 0 ? (int)att->hitdef.pausetime / 2 + 3 : 10;

        /* spark + sound */
        mg_spawn_hitspark((int)spark_x, (int)spark_y, 0);
        if (att->ch->snd) {
            MgSndEntry *s = mg_snd_get(att->ch->snd, att->hitdef.hit_sound_group, att->hitdef.hit_sound_number);
            if (s) mg_play_wav(s->data, s->len, -1, 0);
        }

        /* victim enters gethit state */
        int hit_state = 5000;
        if (vic->statetype == 'A') hit_state = (att->hitdef.fall_fall ? 5030 : 5020);
        else if (vic->statetype == 'C') hit_state = 5010;
        else hit_state = 5000;
        if (vic->statetype == 'L') hit_state = 5100;
        mg_change_state(vic, hit_state, -1, 0);
        vic->movetype = 'H';

        /* attacker hitpush */
        att->vel_x = -att->facing * fabs(att->hitdef.ground_velocity_x) * 0.25;
    }
}

/* ---------------- AI (simple) ---------------- */
static void simple_ai(MgEntity *en) {
    if (!en->is_cpu || !en->ctrl) { en->buttons = 0; return; }
    MgEntity *p2 = mg_p2_of(en);
    if (!p2) { en->buttons = 0; return; }
    double dist = fabs(p2->pos_x - en->pos_x);
    en->ai_timer--;
    if (en->ai_timer <= 0) {
        en->ai_timer = 20 + rand() % 40;
        int r = rand() % 10;
        if (dist > 90) en->ai_action = (r < 6) ? 1 : 2;    /* walk fwd or jump */
        else if (dist > 40) en->ai_action = (r < 3) ? 3 : (r < 6 ? 4 : 1);  /* attack or approach */
        else en->ai_action = (r < 5) ? 3 : (r < 7 ? 4 : 5); /* attack or guard */
    }
    uint32_t fwd = (en->facing > 0) ? BTN_FWD : BTN_BACK;
    uint32_t back = (en->facing > 0) ? BTN_BACK : BTN_FWD;
    switch (en->ai_action) {
        case 1: en->buttons = fwd; break;
        case 2: en->buttons = fwd | BTN_U; break;
        case 3: {
            /* punch/kick: press a then b briefly */
            en->buttons = ((g_game.frames_done / 6) % 2) ? BTN_A : 0;
            if (dist < 50 && (rand() % 3 == 0)) en->buttons |= BTN_B;
            break;
        }
        case 4: en->buttons = fwd | ((g_game.frames_done / 8) % 2 ? BTN_A : 0); break;
        case 5: en->buttons = back; break;
        default: en->buttons = 0;
    }
}

/* ---------------- round flow ---------------- */
void mg_fight_start(void) {
    g_nentities = 0;
    MgStage *st = g_game.stages[g_game.selected_stage];
    g_game.world_w = (int)st->localcoord_w;
    g_game.world_h = (int)st->localcoord_h;
    g_game.round_no = 1;
    g_game.rounds_won[0] = g_game.rounds_won[1] = 0;
    g_game.announce_timer = 0;

    for (int i = 0; i < MG_MAX_PLAYERS; i++) {
        MgEntity *en = &g_entities[i];
        memset(en, 0, sizeof(*en));
        en->ch = g_game.chars[g_game.selected[i]];
        en->alive = 1;
        en->player_no = i;
        en->teamside = i + 1;
        en->is_cpu = (i == 1) && g_game.p2_cpu;
        en->lifemax = (int)mg_char_const(en->ch, "data.life", 1000);
        en->life = en->lifemax;
        en->powermax = 3000;
        en->power = 0;
        en->facing = (i == 0) ? 1 : -1;
        en->pos_x = (i == 0) ? st->p1startx : st->p2startx;
        en->pos_y = 0;
        en->ctrl = 1;
        en->statetype = 'S'; en->movetype = 'I'; en->physics = 'S';
        en->DefenceMul = 1.0;
        en->state_no = 0;
        MgState *s0 = mg_find_state(en, 0);
        if (s0) { if (s0->anim >= 0) { en->anim_no = s0->anim; en->cur_anim = mg_air_get(en->ch->air, s0->anim); } }
        if (!en->cur_anim) { en->anim_no = 0; en->cur_anim = mg_air_get(en->ch->air, 0); }
        en->juggle_points = (int)mg_char_const(en->ch, "data.airjuggle", 15);
        g_nentities++;
    }
    /* reset explods/projectiles */
    memset(g_explods, 0, sizeof(g_explods));
    memset(g_projs, 0, sizeof(g_projs));
    g_game.camera_x = (st->startx + st->boundleft);
    g_game.round_state = 0;
    g_game.round_timer = 0;
    g_game.time_left = 99 * 60;
    g_game.fight_timer = 0;
}

static void next_round(void) {
    MgStage *st = g_game.stages[g_game.selected_stage];
    for (int i = 0; i < g_nentities; i++) {
        MgEntity *en = &g_entities[i];
        en->alive = 1;
        en->life = en->lifemax;
        en->pos_x = (i == 0) ? st->p1startx : st->p2startx;
        en->pos_y = 0;
        en->vel_x = en->vel_y = 0;
        en->ctrl = 1;
        en->statetype = 'S'; en->movetype = 'I'; en->physics = 'S';
        en->hitpause_time = 0;
        mg_change_state(en, 0, 0, 1);
        en->gethit.fall_fall = 0;
        memset(en->var, 0, sizeof(en->var));
        memset(en->fvar, 0, sizeof(en->fvar));
    }
    memset(g_explods, 0, sizeof(g_explods));
    memset(g_projs, 0, sizeof(g_projs));
    g_game.round_state = 0;
    g_game.round_timer = 0;
    g_game.time_left = 99 * 60;
}

void mg_fight_frame(void) {
    MgStage *st = g_game.stages[g_game.selected_stage];
    if (!st) return;

    /* global pause (superpause) */
    if (g_game.pause_timer > 0) {
        g_game.pause_timer--;
        if (g_game.pause_timer == 0) g_game.paused = 0;
        /* during pause only render (and decrement explods stay frozen) */
        return;
    }

    /* round state machine */
    g_game.round_timer++;
    switch (g_game.round_state) {
        case 0:   /* pre-intro */
            if (g_game.round_timer > 30) {
                g_game.round_state = 1;
                g_game.round_timer = 0;
                snprintf(g_game.announce_text, sizeof(g_game.announce_text), "ROUND %d", g_game.round_no);
                g_game.announce_timer = 60;
            }
            break;
        case 1:   /* intro / round text */
            if (g_game.round_timer > 70) {
                g_game.round_state = 2;
                g_game.round_timer = 0;
                snprintf(g_game.announce_text, sizeof(g_game.announce_text), "FIGHT!");
                g_game.announce_timer = 40;
            }
            break;
        case 2: { /* fighting */
            g_game.time_left--;
            int alive0 = g_entities[0].life <= 0 ? 0 : 1;
            int alive1 = g_entities[1].life <= 0 ? 0 : 1;
            if (!alive0 || !alive1 || g_game.time_left <= 0) {
                g_game.round_state = 3;
                g_game.round_timer = 0;
                snprintf(g_game.announce_text, sizeof(g_game.announce_text),
                         !alive0 && !alive1 ? "DOUBLE K.O." :
                         !alive1 ? "K.O." : !alive0 ? "K.O." : "TIME OVER");
                g_game.announce_timer = 90;
                int w = 0;
                if (!alive0 && !alive1) w = -1;
                else if (!alive1) w = 0;
                else if (!alive0) w = 1;
                else {
                    /* time over: more life wins */
                    double l0 = (double)g_entities[0].life / g_entities[0].lifemax;
                    double l1 = (double)g_entities[1].life / g_entities[1].lifemax;
                    w = (l0 > l1) ? 0 : (l1 > l0) ? 1 : -1;
                }
                if (w >= 0) g_game.rounds_won[w]++;
            }
            break;
        }
        case 3:   /* KO pause */
            if (g_game.round_timer > 100) {
                if (g_game.rounds_won[0] >= 2 || g_game.rounds_won[1] >= 2 ||
                    g_game.round_no >= 3) {
                    g_game.round_state = 4;
                    g_game.round_timer = 0;
                    int w = g_game.rounds_won[0] > g_game.rounds_won[1] ? 0 : 1;
                    snprintf(g_game.announce_text, sizeof(g_game.announce_text),
                             "%s WINS", g_entities[w].ch->displayname);
                    g_game.announce_timer = 9999;
                } else {
                    g_game.round_no++;
                    next_round();
                }
            }
            break;
        case 4:   /* victory screen — wait for input */
            break;
    }

    /* entities */
    for (int i = 0; i < g_nentities; i++) {
        MgEntity *en = &g_entities[i];
        /* AI fills buttons */
        if (en->is_cpu) simple_ai(en);
        /* record input history */
        mg_input_record(i, en->buttons);
        /* auto-face opponent when standing and ctrl */
        MgEntity *p2 = mg_p2_of(en);
        if (p2 && en->ctrl && en->statetype == 'S' && en->movetype == 'I' && !en->is_helper) {
            if ((p2->pos_x > en->pos_x && en->facing < 0) || (p2->pos_x < en->pos_x && en->facing > 0))
                en->facing = (p2->pos_x > en->pos_x) ? 1 : -1;
        }
        /* guard distance calc */
        en->inguarddist = 0;
        if (p2 && p2->attacking && p2->movetype == 'A') {
            double d = fabs(p2->pos_x - en->pos_x);
            double gd = mg_char_const(p2->ch, "size.attack.dist", 160);
            if (d < gd) en->inguarddist = 1;
        }
        /* gethit timers decay */
        if (en->gethit.hitshaketime_left > 0) en->gethit.hitshaketime_left--;
        else if (en->gethit.hittime_left > 0) en->gethit.hittime_left--;
        mg_run_entity(en);
        apply_physics(en);
        /* stage bounds */
        double lb = st->boundleft + 20, rb = st->boundright - 20;
        if (en->pos_x < lb) { en->pos_x = lb; if (en->vel_x < 0) en->vel_x = 0; }
        if (en->pos_x > rb) { en->pos_x = rb; if (en->vel_x > 0) en->vel_x = 0; }
        /* death → liedown state 5150 handled via common states (life<=0) */
        if (en->life <= 0 && en->alive && en->state_no != 5150 && g_game.round_state == 2) {
            /* let common states handle KO; force fall */
            if (en->statetype != 'L') {
                en->gethit.fall_fall = 1;
            }
            en->alive = 1;  /* stays as entity; KO via round flow */
        }
        en->buttons_prev = en->buttons;
        if (en->invuln_time > 0) en->invuln_time--;
    }

    /* hits */
    if (g_game.round_state == 2) {
        for (int a = 0; a < g_nentities; a++)
            for (int v = 0; v < g_nentities; v++)
                if (a != v) apply_hit(&g_entities[a], &g_entities[v]);
    }

    /* reset hitdef flags when leaving attack states */
    for (int i = 0; i < g_nentities; i++) {
        MgEntity *en = &g_entities[i];
        if (en->movetype != 'A') {
            en->attacking = 0;
            memset(en->hitdef.already_hit_vic, 0, sizeof(en->hitdef.already_hit_vic));
            en->hitdef_used = 0;
        }
        if (en->state_time == 0 && en->movetype == 'A') {
            memset(en->hitdef.already_hit_vic, 0, sizeof(en->hitdef.already_hit_vic));
        }
    }

    /* projectiles */
    for (int i = 0; i < MG_MAX_PROJ; i++) {
        MgProj *p = &g_projs[i];
        if (!p->active) continue;
        p->pos_x += p->vel_x; p->pos_y += p->vel_y;
        p->anim_time++;
        if (p->removetime > 0 && p->anim_time > p->removetime) p->active = 0;
        if (p->pos_x < st->boundleft - 100 || p->pos_x > st->boundright + 100) p->active = 0;
        /* hit check vs opponent of owner */
        if (g_game.round_state == 2) {
            for (int v = 0; v < g_nentities; v++) {
                MgEntity *vic = &g_entities[v];
                if (v == p->owner_idx || !vic->alive || p->hit_once[v]) continue;
                double px1 = p->pos_x - 8, px2 = p->pos_x + 8;
                double py1 = p->pos_y - 8, py2 = p->pos_y + 8;
                double c1 = 1e9, c2 = -1e9, d1 = 1e9, d2 = -1e9;
                for (int q = 0; q < vic->nclsn2; q++) {
                    MgClsn *cl = &vic->clsn2[q];
                    double x1 = vic->pos_x + (vic->facing > 0 ? cl->x1 : -cl->x2);
                    double x2 = vic->pos_x + (vic->facing > 0 ? cl->x2 : -cl->x1);
                    double y1 = vic->pos_y - cl->y2, y2 = vic->pos_y - cl->y1;
                    if (x1 < c1) c1 = x1; if (x2 > c2) c2 = x2;
                    if (y1 < d1) d1 = y1; if (y2 > d2) d2 = y2;
                }
                if (rects_overlap(px1, py1, px2, py2, c1, d1, c2, d2)) {
                    p->hit_once[v] = 1;
                    p->active = 0;
                    double dmg = p->damage * 100.0 / mg_char_const(vic->ch, "data.defence", 100);
                    vic->life -= (int)dmg;
                    if (vic->life < 0) vic->life = 0;
                    vic->gethit.xvel = -vic->facing * 3;
                    vic->gethit.yvel = -4;
                    vic->gethit.fall_fall = 1;
                    mg_change_state(vic, 5030, -1, 0);
                    vic->movetype = 'H';
                    mg_spawn_hitspark((int)p->pos_x, (int)p->pos_y, 0);
                    break;
                }
            }
        }
    }

    /* explods */
    for (int i = 0; i < MG_MAX_EXPLODS; i++) {
        MgExplod *ex = &g_explods[i];
        if (!ex->active) continue;
        ex->anim_time++;
        if (ex->anim) {
            int total = 0;
            for (int q = 0; q < ex->anim->nframes; q++) total += ex->anim->frames[q].ticks <= 0 ? 1 : ex->anim->frames[q].ticks;
            if (ex->anim_time >= total) {
                ex->active = 0;
            }
        } else if (ex->removetime > 0 && ex->anim_time > ex->removetime) {
            ex->active = 0;
        }
    }

    /* camera */
    mg_update_camera();

    /* shake */
    if (g_game.shake_time > 0) g_game.shake_time--;

    g_game.frames_done++;
}

void mg_update_camera(void) {
    MgStage *st = g_game.stages[g_game.selected_stage];
    if (!st) return;
    double mid = 0;
    int n = 0;
    for (int i = 0; i < g_nentities; i++) {
        if (g_entities[i].is_helper) continue;
        mid += g_entities[i].pos_x; n++;
    }
    if (n) mid /= n;
    double half = g_game.world_w / 2.0;
    double target = mid - half;
    /* tension: keep players on screen */
    double minp = 1e9, maxp = -1e9;
    for (int i = 0; i < g_nentities; i++) {
        if (g_entities[i].is_helper) continue;
        if (g_entities[i].pos_x < minp) minp = g_entities[i].pos_x;
        if (g_entities[i].pos_x > maxp) maxp = g_entities[i].pos_x;
    }
    if (n) {
        if (maxp - minp > g_game.world_w - 40) {
            /* players wider than screen: center */
        } else {
            if (minp - g_game.camera_x < 40) target = g_game.camera_x - (40 - (minp - g_game.camera_x));
            if ((g_game.camera_x + g_game.world_w) - maxp < 40) target = g_game.camera_x + (40 - ((g_game.camera_x + g_game.world_w) - maxp));
        }
    }
    if (target < st->boundleft) target = st->boundleft;
    if (target > st->boundright - g_game.world_w) target = st->boundright - g_game.world_w;
    if (st->boundright - st->boundleft < g_game.world_w) target = st->boundleft;
    g_game.camera_x += (target - g_game.camera_x) * 0.18;
}

/* ---------------- rendering ---------------- */
static double stage_zoffset(void) {
    MgStage *st = g_game.stages[g_game.selected_stage];
    return st ? st->zoffset : g_game.world_h;
}
static void world_to_screen(double wx, double wy, int *sx, int *sy) {
    *sx = (int)((wx - g_game.camera_x) * g_game.world_scale);
    *sy = (int)((stage_zoffset() + wy - g_game.camera_y) * g_game.world_scale);
}

void mg_render_entity(MgEntity *en) {
    if (!en->cur_anim || en->cur_anim->nframes == 0) return;
    MgAnimFrame *f = &en->cur_anim->frames[en->cur_frame_index];
    MgChar *ch = en->ch;
    MgSprite *spr = mg_sff_get(ch->sff, f->group, f->number);
    if (!spr) return;
    double scale = g_game.world_scale * ch->pal_scale;
    int flip = (en->facing < 0) ? !f->flip_h : f->flip_h;
    int sx, sy;
    world_to_screen(en->pos_x, en->pos_y, &sx, &sy);
    int dx = (int)(sx - (f->xoff + spr->axis_x) * scale * (en->facing < 0 ? -1 : 1) - (en->facing < 0 ? spr->w * scale : 0));
    int dy = (int)(sy - (f->yoff + spr->axis_y) * scale - spr->h * scale);
    /* simpler: draw aligned to axis */
    dx = (int)(sx - ((en->facing < 0 ? spr->w - (f->xoff + spr->axis_x) : (f->xoff + spr->axis_x)) * scale));
    dy = (int)(sy - (f->yoff + spr->axis_y) * scale - spr->h * scale);
    mg_render_sprite(spr, dx, dy, (int)(spr->w * scale), (int)(spr->h * scale), flip, f->flip_v, f->alpha_src, f->alpha_dst);
}

void mg_render_stage(int layer) {
    MgStage *st = g_game.stages[g_game.selected_stage];
    if (!st) return;
    for (int i = 0; i < st->nbgs; i++) {
        struct MgStage_local { int x; }; /* placeholder */
        MgStageBg *bg = &st->bgs[i];
        if (bg->layerno != layer) continue;
        MgSprite *spr = mg_sff_get(st->sff, bg->spr_group, bg->spr_number);
        if (!spr) continue;
        double scale = g_game.world_scale;
        int x = (int)((bg->x - g_game.camera_x * bg->delta_x) * scale);
        int y = (int)((bg->y - g_game.camera_y * bg->delta_y) * scale);
        int w = (int)(spr->w * scale * bg->xscale);
        int h = (int)(spr->h * scale * bg->yscale);
        if (bg->tile_x) {
            int span = (int)(g_game.world_w * scale);
            int tw = (w > 0) ? w : 1;
            for (int tx = x % tw - tw; tx < span + w; tx += tw + (int)(bg->tilespacing_x * scale)) {
                mg_render_sprite(spr, tx, y, w, h, 0, 0, -1, -1);
            }
        } else {
            mg_render_sprite(spr, x, y, w, h, 0, 0, -1, -1);
        }
    }
}

void mg_render_hud(void) {
    MgGame *g = &g_game;
    /* life bars */
    MgEntity *p1 = &g_entities[0], *p2 = &g_entities[1];
    int life_w1 = (int)((double)p1->life / p1->lifemax * g->hud.p1_life_w);
    int life_w2 = (int)((double)p2->life / p2->lifemax * g->hud.p2_life_w);
    /* bg then front */
    mg_fill_rect(g->hud.p1_life_x, g->hud.p1_life_y, g->hud.p1_life_w, g->hud.p1_life_h, 0x40, 0x00, 0x00, 0xFF);
    mg_fill_rect(g->hud.p1_life_x, g->hud.p1_life_y, life_w1, g->hud.p1_life_h,
                 (g->hud.p1_life_front >> 16) & 0xFF, (g->hud.p1_life_front >> 8) & 0xFF, g->hud.p1_life_front & 0xFF, 0xFF);
    mg_fill_rect(g->hud.p2_life_x, g->hud.p2_life_y, g->hud.p2_life_w, g->hud.p2_life_h, 0x40, 0x00, 0x00, 0xFF);
    mg_fill_rect(g->hud.p2_life_x + (g->hud.p2_life_w - life_w2), g->hud.p2_life_y, life_w2, g->hud.p2_life_h,
                 (g->hud.p2_life_front >> 16) & 0xFF, (g->hud.p2_life_front >> 8) & 0xFF, g->hud.p2_life_front & 0xFF, 0xFF);
    /* power bars */
    int pow_w1 = (int)((double)p1->power / p1->powermax * g->hud.p1_power_w);
    int pow_w2 = (int)((double)p2->power / p2->powermax * g->hud.p2_power_w);
    mg_fill_rect(g->hud.p1_power_x, g->hud.p1_power_y, g->hud.p1_power_w, g->hud.p1_power_h, 0x00, 0x00, 0x40, 0xFF);
    mg_fill_rect(g->hud.p1_power_x, g->hud.p1_power_y, pow_w1, g->hud.p1_power_h,
                 (g->hud.p1_power_front >> 16) & 0xFF, (g->hud.p1_power_front >> 8) & 0xFF, g->hud.p1_power_front & 0xFF, 0xFF);
    mg_fill_rect(g->hud.p2_power_x, g->hud.p2_power_y, g->hud.p2_power_w, g->hud.p2_power_h, 0x00, 0x00, 0x40, 0xFF);
    mg_fill_rect(g->hud.p2_power_x + (g->hud.p2_power_w - pow_w2), g->hud.p2_power_y, pow_w2, g->hud.p2_power_h,
                 (g->hud.p2_power_front >> 16) & 0xFF, (g->hud.p2_power_front >> 8) & 0xFF, g->hud.p2_power_front & 0xFF, 0xFF);
    /* timer */
    if (g->fonts[0]) {
        char t[8];
        int secs = g->time_left / 60;
        if (secs < 0) secs = 0;
        snprintf(t, sizeof(t), "%d", secs);
        int tw = strlen(t) * (g->fonts[0]->size_w + 2) * 2;
        mg_render_text(g->fonts[0], t, g->win_w/2 - tw/2, 8, 2, 0xFFFF00);
    }
    /* names */
    if (g->fonts[0]) {
        mg_render_text(g->fonts[0], p1->ch->displayname, g->hud.name_p1_x, g->hud.name_p1_y, 1, 0xFFFFFF);
        int nw = strlen(p2->ch->displayname) * (g->fonts[0]->size_w + 2);
        mg_render_text(g->fonts[0], p2->ch->displayname, g->hud.name_p2_x - nw, g->hud.name_p2_y, 1, 0xFFFFFF);
    }
    /* faces (9000,0 portraits) */
    MgSprite *face1 = mg_sff_get(p1->ch->sff, 9000, 0);
    if (face1) mg_render_sprite(face1, g->hud.p1_face_x, g->hud.p1_face_y, g->hud.p1_face_w, g->hud.p1_face_h, 0, 0, -1, -1);
    MgSprite *face2 = mg_sff_get(p2->ch->sff, 9000, 0);
    if (face2) mg_render_sprite(face2, g->hud.p2_face_x, g->hud.p2_face_y, g->hud.p2_face_w, g->hud.p2_face_h, 1, 0, -1, -1);
    /* announce text */
    if (g->announce_timer > 0 && g->announce_text[0] && g->fonts[0]) {
        g->announce_timer--;
        int tw = strlen(g->announce_text) * (g->fonts[0]->size_w + 2) * 3;
        mg_render_text(g->fonts[0], g->announce_text, g->win_w/2 - tw/2, g->win_h/3, 3, 0xFFFF00);
    }
}

void mg_fight_render(void) {
    /* shake offset applied by renderer via g_game.shake */
    mg_render_stage(0);
    /* entities sorted by sprpriority */
    for (int pr = -5; pr <= 5; pr++)
        for (int i = 0; i < g_nentities; i++)
            if (g_entities[i].sprpriority == pr)
                mg_render_entity(&g_entities[i]);
    /* projectiles + explods above */
    for (int i = 0; i < MG_MAX_PROJ; i++) {
        MgProj *p = &g_projs[i];
        if (!p->active) continue;
        MgSprite *spr = p->spr;
        if (p->anim && p->anim->nframes) {
            int total = 0, idx = 0, t = p->anim_time;
            for (int q = 0; q < p->anim->nframes; q++) total += p->anim->frames[q].ticks <= 0 ? 1 : p->anim->frames[q].ticks;
            t %= total;
            for (int q = 0; q < p->anim->nframes; q++) {
                int tk = p->anim->frames[q].ticks <= 0 ? 1 : p->anim->frames[q].ticks;
                if (t < tk) { idx = q; break; }
                t -= tk;
            }
            MgAnimFrame *f = &p->anim->frames[idx];
            spr = mg_sff_get(g_fightfx_sff, f->group, f->number);
            if (!spr && g_entities[p->owner_idx].ch->sff)
                spr = mg_sff_get(g_entities[p->owner_idx].ch->sff, f->group, f->number);
        }
        if (spr) {
            int sx, sy;
            world_to_screen(p->pos_x, p->pos_y, &sx, &sy);
            mg_render_sprite(spr, sx - spr->w/2, sy - spr->h/2, spr->w, spr->h, p->facing < 0, 0, -1, -1);
        }
    }
    for (int i = 0; i < MG_MAX_EXPLODS; i++) {
        MgExplod *ex = &g_explods[i];
        if (!ex->active) continue;
        MgSprite *spr = ex->spr;
        if (ex->anim && ex->anim->nframes) {
            int total = 0, idx = 0, t = ex->anim_time;
            for (int q = 0; q < ex->anim->nframes; q++) total += ex->anim->frames[q].ticks <= 0 ? 1 : ex->anim->frames[q].ticks;
            for (int q = 0; q < ex->anim->nframes; q++) {
                int tk = ex->anim->frames[q].ticks <= 0 ? 1 : ex->anim->frames[q].ticks;
                if (t < tk) { idx = q; break; }
                t -= tk;
            }
            MgAnimFrame *f = &ex->anim->frames[idx];
            spr = g_fightfx_sff ? mg_sff_get(g_fightfx_sff, f->group, f->number) : NULL;
        }
        if (spr) {
            int sx, sy;
            world_to_screen(ex->pos_x, ex->pos_y, &sx, &sy);
            mg_render_sprite(spr, sx, sy, spr->w, spr->h, 0, 0, -1, -1);
        }
    }
    mg_render_stage(1);
    mg_render_hud();
}

