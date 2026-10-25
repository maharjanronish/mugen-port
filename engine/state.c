/* state.c — state machine execution, state controllers, command detection */
#include "mugen.h"

MgGame g_game;
MgEntity g_entities[MG_MAX_ENTITIES];
int g_nentities = 0;

MgEntity *mg_p2_of(MgEntity *en) {
    for (int i = 0; i < g_nentities; i++)
        if (&g_entities[i] != en && !g_entities[i].is_helper)
            return &g_entities[i];
    return NULL;
}

MgState *mg_find_state(MgEntity *en, int state_no) {
    if (!en->ch) return NULL;
    for (int i = 0; i < en->ch->nstates; i++)
        if (en->ch->states[i].statedef == state_no)
            return &en->ch->states[i];
    return NULL;
}

/* ---------------- animation ---------------- */
void mg_update_anim(MgEntity *en) {
    if (!en->cur_anim || en->cur_anim->nframes == 0) return;
    if (en->hitpause_time > 0) return;   /* animation frozen during hitpause */
    en->anim_time++;
    MgAnim *a = en->cur_anim;
    int total = 0;
    for (int i = 0; i < a->nframes; i++) total += a->frames[i].ticks <= 0 ? 1 : a->frames[i].ticks;
    if (en->anim_time >= total) {
        if (a->loopstart >= 0 && a->loopstart < a->nframes) {
            /* MUGEN semantics: the tick where the anim reaches its total is
             * AnimTime = 0 (loop end / anim end) — triggers like
             * "trigger1 = AnimTime = 0" must observe it. So we let anim_time
             * REST at `total` for exactly one tick (AnimTime reports 0) and
             * wrap to the loopstart on the NEXT tick. The old code wrapped
             * immediately, so AnimTime never reached 0 and characters got
             * stuck forever inside their attack states (v0.2.0 device bug:
             * "punches continuously / move never ends"). */
            if (en->anim_time == total) { en->cur_frame_index = a->nframes - 1; return; }
            int pre = 0;
            for (int i = 0; i < a->loopstart; i++) pre += a->frames[i].ticks <= 0 ? 1 : a->frames[i].ticks;
            en->anim_time = pre + (en->anim_time - total - 1);
            if (en->anim_time >= total) en->anim_time = pre;
        } else {
            /* non-looping: clamp at the end — AnimTime stays 0 (MUGEN's
             * AnimTime goes negative after the end; 0 is the signal CNS
             * states test for). The old `total - 1` clamp froze it at 1. */
            en->anim_time = total;
            en->cur_frame_index = a->nframes - 1;
            return;
        }
    }
    /* find current frame */
    int t = en->anim_time, idx = 0;
    for (int i = 0; i < a->nframes; i++) {
        int tk = a->frames[i].ticks <= 0 ? 1 : a->frames[i].ticks;
        if (t < tk) { idx = i; break; }
        t -= tk;
        if (i == a->nframes - 1) idx = i;
    }
    en->cur_frame_index = idx;
}

void mg_update_clsn(MgEntity *en) {
    en->nclsn1 = en->nclsn2 = 0;
    if (!en->cur_anim || en->cur_anim->nframes == 0) return;
    MgAnimFrame *f = &en->cur_anim->frames[en->cur_frame_index];
    /* AIR boxes are authored in the CHAR's localcoord: scale to world units
     * (identity for 320x240 chars on a 320x240 stage). */
    double cs = en->coord_scale > 0 ? en->coord_scale : 1.0;
    if (f->nclsn1) {
        for (int i = 0; i < f->nclsn1; i++) {
            en->clsn1[i].x1 = (int32_t)(f->clsn1[i].x1 * cs);
            en->clsn1[i].x2 = (int32_t)(f->clsn1[i].x2 * cs);
            en->clsn1[i].y1 = (int32_t)(f->clsn1[i].y1 * cs);
            en->clsn1[i].y2 = (int32_t)(f->clsn1[i].y2 * cs);
        }
        en->nclsn1 = f->nclsn1;
    }
    if (f->nclsn2) {
        for (int i = 0; i < f->nclsn2; i++) {
            en->clsn2[i].x1 = (int32_t)(f->clsn2[i].x1 * cs);
            en->clsn2[i].x2 = (int32_t)(f->clsn2[i].x2 * cs);
            en->clsn2[i].y1 = (int32_t)(f->clsn2[i].y1 * cs);
            en->clsn2[i].y2 = (int32_t)(f->clsn2[i].y2 * cs);
        }
        en->nclsn2 = f->nclsn2;
    }
}

/* ---------------- state change ---------------- */
void mg_change_state(MgEntity *en, int state_no, int anim, int ctrl) {
    en->prev_state_no = en->state_no;
    en->state_no = state_no;
    en->state_time = 0;
    /* PosFreeze is a per-state flag in MUGEN: controllers assert it with
     * trigger1 = 1 (e.g. common1.cns state 5100 lie-down bounce) and it
     * must lapse when the state changes. v0.2.0 bug: it was latched
     * forever, so the first knockdown PERMANENTLY froze that character
     * (velocity zeroed every frame — the CPU looked "stuck" at its x
     * position for the rest of the fight). */
    en->pos_freeze = 0;
    MgState *st = mg_find_state(en, state_no);
    if (st) {
        if (st->type) en->statetype = st->type;
        if (st->movetype) en->movetype = st->movetype;
        en->physics = st->physics;
        if (st->anim >= 0) {
            en->anim_no = st->anim;
            MgAnim *an = mg_air_get(en->ch->air, st->anim);
            if (an) { en->cur_anim = an; en->anim_time = 0; }
        } else if (anim >= 0) {
            en->anim_no = anim;
            MgAnim *an = mg_air_get(en->ch->air, anim);
            if (an) { en->cur_anim = an; en->anim_time = 0; }
        }
        /* else: keep current anim */
        /* Statedef velocities are in char-local units */
        double cs = en->coord_scale > 0 ? en->coord_scale : 1.0;
        en->vel_x = st->vel_x * cs; en->vel_y = st->vel_y * cs;
        if (st->ctrl >= 0) en->ctrl = st->ctrl;
        else if (ctrl >= 0) en->ctrl = ctrl;
        /* reset hitdef */
        en->attacking = 0;
        /* reset move counters on attack states */
        if (en->movetype == 'A' && st->poweradd != 0) {}
    }
    if (ctrl >= 0) en->ctrl = ctrl;
    en->hitpause_time = 0;
}

/* ---------------- param helpers ---------------- */
static const char *ctl_param(MgStateCtl *c, const char *name) {
    for (int i = 0; i < c->nparams; i++)
        if (mg_str_ieq(c->pnames[i], name)) return c->pvals[i];
    return NULL;
}

static double ctl_param_f(MgStateCtl *c, const char *name, double defval) {
    const char *s = ctl_param(c, name);
    /* expression-valued params: evaluate against entity via AST */
    return s ? mg_getf(s, defval) : defval;
}

/* eval param as expression if parseable */
static double ctl_param_e(MgStateCtl *c, MgEntity *en, const char *name, double defval) {
    const char *s = ctl_param(c, name);
    if (!s) return defval;
    MgExpr *ex = mg_expr_parse(s);
    if (!ex) return defval;
    MgVal v = mg_expr_eval(ex, en);
    mg_expr_free(ex);
    return v.is_float ? v.f : (double)v.i;
}

static int ctl_param_i(MgStateCtl *c, MgEntity *en, const char *name, int defval) {
    return (int)ctl_param_e(c, en, name, defval);
}

/* ---------------- trigger evaluation for a controller ---------------- */
static int ctl_triggers_met(MgStateCtl *c, MgEntity *en) {
    /* all triggerall lines must hold (AND) */
    for (int i = 0; i < c->ntrig_all; i++) {
        if (!c->trig_all[i]) continue;
        MgVal v = mg_expr_eval(c->trig_all[i], en);
        if (!(v.is_float ? v.f : v.i)) return 0;
    }
    /* triggerN: lines with same N are AND-ed; different N are OR-ed */
    int any = 0, any_defined = 0;
    for (int idx = 0; idx < c->ntrig_idx && idx < 8; idx++) {
        if (c->ntrig_per[idx] == 0) continue;
        any_defined = 1;
        int ok = 1;
        for (int j = 0; j < c->ntrig_per[idx]; j++) {
            if (!c->trig[idx][j]) continue;
            MgVal v = mg_expr_eval(c->trig[idx][j], en);
            if (!(v.is_float ? v.f : v.i)) { ok = 0; break; }
        }
        if (ok) { any = 1; break; }
    }
    return any_defined ? any : 1;   /* no triggers = always true */
}

/* ---------------- state controller execution ---------------- */
static void exec_sctrl(MgEntity *en, MgStateCtl *c) {
    const char *t = c->type;
    MgChar *ch = en->ch;

    if (mg_str_ieq(t, "changestate")) {
        int v = ctl_param_i(c, en, "value", -1);
        int a = ctl_param_i(c, en, "anim", -1);
        int ctrl = ctl_param_i(c, en, "ctrl", -1);
        if (v >= 0) mg_change_state(en, v, a, ctrl);
    }
    else if (mg_str_ieq(t, "selfstate")) {
        int v = ctl_param_i(c, en, "value", -1);
        if (v >= 0) mg_change_state(en, v, ctl_param_i(c, en, "anim", -1), ctl_param_i(c, en, "ctrl", -1));
    }
    else if (mg_str_ieq(t, "changeanim")) {
        int v = ctl_param_i(c, en, "value", -1);
        if (v >= 0) {
            MgAnim *an = mg_air_get(ch->air, v);
            if (an) { en->cur_anim = an; en->anim_time = 0; en->anim_no = v; }
        }
    }
    else if (mg_str_ieq(t, "changeanim2")) {
        int v = ctl_param_i(c, en, "value", -1);
        if (v >= 0) {
            MgAnim *an = mg_air_get(ch->air, v);
            if (an) { en->cur_anim = an; en->anim_time = 0; en->anim_no = v; }
        }
    }
    else if (mg_str_ieq(t, "velset")) {
        const char *x = ctl_param(c, "x"), *y = ctl_param(c, "y");
        double cs = en->coord_scale > 0 ? en->coord_scale : 1.0;
        /* MUGEN: X velocities in CNS are FACING-RELATIVE (positive = toward
         * the direction the character faces — walk.fwd/jump.fwd are positive,
         * *.back are negative in every CNS). Storing them absolute made every
         * left-facing character moonwalk (walk away while animating toward). */
        double fx = en->facing < 0 ? -1.0 : 1.0;
        if (x) en->vel_x = ctl_param_e(c, en, "x", en->vel_x) * cs * fx;
        if (y) en->vel_y = ctl_param_e(c, en, "y", en->vel_y) * cs;
    }
    else if (mg_str_ieq(t, "veladd")) {
        const char *x = ctl_param(c, "x"), *y = ctl_param(c, "y");
        double cs = en->coord_scale > 0 ? en->coord_scale : 1.0;
        double fx = en->facing < 0 ? -1.0 : 1.0;
        if (x) en->vel_x += ctl_param_e(c, en, "x", 0) * cs * fx;
        if (y) en->vel_y += ctl_param_e(c, en, "y", 0) * cs;
    }
    else if (mg_str_ieq(t, "velmul")) {
        const char *x = ctl_param(c, "x"), *y = ctl_param(c, "y");
        if (x) en->vel_x *= ctl_param_e(c, en, "x", 1);
        if (y) en->vel_y *= ctl_param_e(c, en, "y", 1);
    }
    else if (mg_str_ieq(t, "posset")) {
        const char *x = ctl_param(c, "x"), *y = ctl_param(c, "y");
        double cs = en->coord_scale > 0 ? en->coord_scale : 1.0;
        if (x) en->pos_x = ctl_param_e(c, en, "x", en->pos_x) * cs;
        if (y) en->pos_y = ctl_param_e(c, en, "y", en->pos_y) * cs;
    }
    else if (mg_str_ieq(t, "posadd")) {
        const char *x = ctl_param(c, "x"), *y = ctl_param(c, "y");
        double cs = en->coord_scale > 0 ? en->coord_scale : 1.0;
        double fx = en->facing < 0 ? -1.0 : 1.0;   /* MUGEN: x offset facing-relative */
        if (x) en->pos_x += ctl_param_e(c, en, "x", 0) * cs * fx;
        if (y) en->pos_y += ctl_param_e(c, en, "y", 0) * cs;
    }
    else if (mg_str_ieq(t, "ctrlset")) {
        en->ctrl = ctl_param_i(c, en, "value", en->ctrl);
        en->ctrl_overridden = 1;
    }
    else if (mg_str_ieq(t, "statetypeset")) {
        const char *v;
        if ((v = ctl_param(c, "statetype")) && v[0]) en->statetype = (v[0]|32) - 32;
        if ((v = ctl_param(c, "movetype")) && v[0]) en->movetype = (v[0]|32) - 32;
        if ((v = ctl_param(c, "physics")) && v[0]) en->physics = (v[0]|32) - 32;
    }
    else if (mg_str_ieq(t, "playsnd")) {
        int g = ctl_param_i(c, en, "value", -1000);
        int grp = g >> 8, num = g & 0xFF;
        /* value = "group, number" — parse raw */
        const char *vs = ctl_param(c, "value");
        if (vs) {
            int vv[2] = {-1, 0};
            mg_parse_ints(vs, vv, 2);
            grp = vv[0]; num = vv[1];
        }
        if (grp >= 0 && ch->snd) {
            MgSndEntry *e = mg_snd_get(ch->snd, grp, num);
            if (e) mg_play_wav(e->data, e->len, ctl_param_i(c, en, "channel", -1),
                               ctl_param_i(c, en, "loop", 0));
        }
    }
    else if (mg_str_ieq(t, "varset") || mg_str_ieq(t, "varadd")) {
        int add = mg_str_ieq(t, "varadd");
        int idx = ctl_param_i(c, en, "v", -1);
        const char *vs = ctl_param(c, "value");
        if (idx < 0) {
            /* legacy Elecbyte form: "var(3) = <expr>" / "fvar(2) = <expr>".
             * EXTREMELY common in state -1 (KFM gates every special on
             * var(1) "combo condition" written this way) — v0.2.2 fix: not
             * handling it silently blocked ALL special moves. */
            for (int p = 0; p < c->nparams; p++) {
                const char *nm = c->pnames[p];
                const char *pv = c->pvals[p];
                if (!nm || !pv) continue;
                int isf = 0, nstart = -1;
                if (strncasecmp(nm, "var(", 4) == 0) nstart = 4;
                else if (strncasecmp(nm, "fvar(", 5) == 0) { nstart = 5; isf = 1; }
                if (nstart > 0) {
                    int n = atoi(nm + nstart);
                    if (n >= 0 && (isf ? n < 64 : n < 64)) {
                        MgExpr *ex = mg_expr_parse(pv);
                        if (ex) {
                            MgVal v = mg_expr_eval(ex, en);
                            if (isf) {
                                double d = v.is_float ? v.f : (double)v.i;
                                en->fvar[n] = add ? en->fvar[n] + d : d;
                            } else {
                                int64_t iv = v.is_float ? (int64_t)v.f : v.i;
                                en->var[n] = add ? en->var[n] + iv : iv;
                            }
                            mg_expr_free(ex);
                        }
                    }
                    idx = -2;   /* handled via legacy path */
                    break;
                }
            }
        }
        if (idx >= 0 && idx < 64 && vs) {
            MgExpr *ex = mg_expr_parse(vs);
            if (ex) {
                MgVal v = mg_expr_eval(ex, en);
                int64_t val = v.is_float ? (int64_t)v.f : v.i;
                en->var[idx] = add ? en->var[idx] + val : val;
                mg_expr_free(ex);
            }
        }
    }
    else if (mg_str_ieq(t, "varrangeset") || mg_str_ieq(t, "varrangeadd")) {
        int first = ctl_param_i(c, en, "first", -1);
        int last = ctl_param_i(c, en, "last", first);
        int64_t val = (int64_t)ctl_param_e(c, en, "value", 0);
        for (int i = first; i >= 0 && i <= last && i < 64; i++)
            en->var[i] = (c && mg_str_ieq(t, "varrangeadd")) ? en->var[i] + val : val;
    }
    else if (mg_str_ieq(t, "nothitby") || mg_str_ieq(t, "hitby")) {
        en->invuln_time = ctl_param_i(c, en, "value", 0);
    }
    else if (mg_str_ieq(t, "assertspecial")) {
        const char *fl[6]; int used = 0;
        for (int i = 0; i < c->nparams && used < 6; i++) {
            if (mg_str_ieq(c->pnames[i], "flag") || mg_str_ieq(c->pnames[i], "flag2") || mg_str_ieq(c->pnames[i], "flag3"))
                fl[used++] = c->pvals[i];
        }
        for (int i = 0; i < used; i++) {
            if (mg_str_ieq(fl[i], "nowalk")) en->special_nowalk = 1;
            else if (mg_str_ieq(fl[i], "nobrake")) en->special_nobrake = 1;
            else if (mg_str_ieq(fl[i], "invincible")) en->special_invincible = 1;
            else if (mg_str_ieq(fl[i], "noshadow")) en->special_noshadow = 1;
            else if (mg_str_ieq(fl[i], "nostandguard")) en->special_nostandguard = 1;
            else if (mg_str_ieq(fl[i], "nocrouchguard")) en->special_nocrouchguard = 1;
            else if (mg_str_ieq(fl[i], "noairguard")) en->special_noairguard = 1;
            else if (mg_str_ieq(fl[i], "nojugglecheck")) en->special_nojugglecheck = 1;
            else if (mg_str_ieq(fl[i], "auto_turn_special")) en->special_auto_turn_special = 1;
        }
    }
    else if (mg_str_ieq(t, "turn")) {
        en->facing = -en->facing;
    }
    else if (mg_str_ieq(t, "width")) {
        en->width_edge_add = ctl_param_i(c, en, "edge", 0);
        en->width_player_add = ctl_param_i(c, en, "player", 0);
    }
    else if (mg_str_ieq(t, "screenbound")) {
        en->screenbound_x = ctl_param_i(c, en, "value", 1);
    }
    else if (mg_str_ieq(t, "posfreeze")) {
        en->pos_freeze = ctl_param_i(c, en, "value", 1);
    }
    else if (mg_str_ieq(t, "sprpriority")) {
        en->sprpriority = ctl_param_i(c, en, "value", 0);
    }
    else if (mg_str_ieq(t, "hitdef") || mg_str_ieq(t, "reversaldef")) {
        /* parse all hitdef params */
        en->attacking = 1;
        en->reversal_flag = mg_str_ieq(t, "reversaldef");
        const char *v;
        if ((v = ctl_param(c, "attr"))) strncpy(en->hitdef.attr_str, v, 31);
        if ((v = ctl_param(c, "hitflag"))) strncpy(en->hitdef.hitflag_str, v, 15);
        if ((v = ctl_param(c, "guardflag"))) strncpy(en->hitdef.guardflag_str, v, 15);
        if ((v = ctl_param(c, "animtype"))) strncpy(en->hitdef.animtype_str, v, 15);
        en->hitdef.damage = ctl_param_e(c, en, "damage", 0);
        en->hitdef.hitdamage = ctl_param_e(c, en, "hitdamage", en->hitdef.damage);
        en->hitdef.guarddamage = ctl_param_e(c, en, "guarddamage", en->hitdef.damage);
        en->hitdef.pausetime = ctl_param_e(c, en, "pausetime", 0);
        en->hitdef.guard_pausetime = ctl_param_e(c, en, "guard.pausetime", en->hitdef.pausetime);
        const char *vs = ctl_param(c, "sparkno");
        (void)vs;
        en->hitdef.sparkxy_x = ctl_param_e(c, en, "sparkxy", 0);
        /* sparkxy = "x, y" */
        vs = ctl_param(c, "sparkxy");
        if (vs) { double sv[2] = {0,0}; mg_parse_floats(vs, sv, 2); en->hitdef.sparkxy_x = sv[0]; en->hitdef.sparkxy_y = sv[1]; }
        en->hitdef.hit_slidetime = (int)ctl_param_e(c, en, "slidetime", 0);
        vs = ctl_param(c, "slidetime");
        if (vs) { int sv[2] = {0,0}; mg_parse_ints(vs, sv, 2); en->hitdef.hit_slidetime = sv[0]; en->hitdef.guard_slidetime = sv[1]; }
        vs = ctl_param(c, "ground.velocity");
        if (vs) { double sv[3] = {0,0,0}; int nn = mg_parse_floats(vs, sv, 3);
            en->hitdef.ground_velocity_x = sv[0]; en->hitdef.ground_velocity_y = sv[1];
            if (nn > 2) en->hitdef.ground_cornerpush = sv[2]; else en->hitdef.ground_cornerpush = sv[0] * 1.0; }
        vs = ctl_param(c, "air.velocity");
        if (vs) { double sv[3] = {0,0,0}; int nn = mg_parse_floats(vs, sv, 3);
            en->hitdef.air_velocity_x = sv[0]; en->hitdef.air_velocity_y = sv[1];
            if (nn > 2) en->hitdef.air_cornerpush = sv[2]; }
        vs = ctl_param(c, "air.fall.velocity");
        if (vs) { double sv[2] = {0,0}; mg_parse_floats(vs, sv, 2); en->hitdef.air_fall_xvel = sv[0]; en->hitdef.air_fall_yvel = sv[1]; }
        vs = ctl_param(c, "down.velocity");
        if (vs) { double sv[3] = {0,0,0}; mg_parse_floats(vs, sv, 3); en->hitdef.down_velocity_x = sv[0]; en->hitdef.down_velocity_y = sv[1]; }
        vs = ctl_param(c, "guard.velocity");
        if (vs) { double sv[3] = {0,0,0}; mg_parse_floats(vs, sv, 3);
            en->hitdef.guard_velocity_x = sv[0];
            if (sv[1]) en->hitdef.guard_velocity_y = sv[1]; }
        vs = ctl_param(c, "airguard.velocity");
        if (vs) { double sv[3] = {0,0,0}; mg_parse_floats(vs, sv, 3);
            en->hitdef.airguard_velocity_x = sv[0]; en->hitdef.airguard_velocity_y = sv[1]; }
        en->hitdef.power_add = (int)ctl_param_e(c, en, "poweradd", 0);
        vs = ctl_param(c, "p2stateno");
        if (vs) en->hitdef.p2stateno = mg_geti(vs, -1); else en->hitdef.p2stateno = -1;
        vs = ctl_param(c, "p1stateno");
        if (vs) en->hitdef.p1stateno = mg_geti(vs, -1); else en->hitdef.p1stateno = -1;
        vs = ctl_param(c, "numhits");
        if (vs) en->hitdef.numhits = mg_geti(vs, 1);
        /* fall params */
        vs = ctl_param(c, "fall");
        if (vs) en->hitdef.fall_fall = mg_geti(vs, 0); else en->hitdef.fall_fall = 0;
        en->hitdef.fall_recover = ctl_param_i(c, en, "fall.recover", 1);
        en->hitdef.juggle = ctl_param_i(c, en, "juggle", 0);
        en->hitdef.hitsperframe = 1;
    }
    else if (mg_str_ieq(t, "hitoverride")) {
        /* simplified: no-op for MVP */
    }
    else if (mg_str_ieq(t, "targetbind") || mg_str_ieq(t, "targetfacing") ||
             mg_str_ieq(t, "targetlifeadd") || mg_str_ieq(t, "targetstate") ||
             mg_str_ieq(t, "targetvelset") || mg_str_ieq(t, "targetdrop")) {
        mg_apply_target_sctrl(en, t, c);
    }
    else if (mg_str_ieq(t, "explod")) {
        mg_spawn_explod(en, c);
    }
    else if (mg_str_ieq(t, "removeexplod")) {
        mg_remove_explods(en);
    }
    else if (mg_str_ieq(t, "afterimage") || mg_str_ieq(t, "afterimagetime")) {
        /* MVP stub: accepted, no visual */
    }
    else if (mg_str_ieq(t, "palfx")) {
        en->palfx_time = ctl_param_i(c, en, "time", 0);
        en->palfx_add_r = ctl_param_i(c, en, "add", 0);  /* simplified */
    }
    else if (mg_str_ieq(t, "envshake")) {
        g_game.shake_time = ctl_param_i(c, en, "time", 0);
        g_game.shake_ampl = ctl_param_i(c, en, "ampl", 4);
    }
    else if (mg_str_ieq(t, "pause") || mg_str_ieq(t, "superpause")) {
        g_game.pause_timer = ctl_param_i(c, en, "time", 30);
        g_game.paused = 1;
    }
    else if (mg_str_ieq(t, "null")) {
        /* nothing */
    }
    else if (mg_str_ieq(t, "forcefeedback")) {
        /* rumble — skip on PC, vibrate on Android */
        mg_platform_vibrate(ctl_param_i(c, en, "time", 10));
    }
    else if (mg_str_ieq(t, "defencemulset")) {
        en->DefenceMul = ctl_param_e(c, en, "value", 1.0);
    }
    else if (mg_str_ieq(t, "hitvelset")) {
        /* set victim vel to gethit velocities (used inside gethit states) */
        if (ctl_param(c, "x")) en->vel_x = en->gethit.xvel * en->facing;
        if (ctl_param(c, "y")) en->vel_y = en->gethit.yvel;
    }
    else if (mg_str_ieq(t, "hitfalldamage") || mg_str_ieq(t, "hitfallvel") ||
             mg_str_ieq(t, "hitfallset") || mg_str_ieq(t, "fallenvshake")) {
        /* handled within gethit common states; simplified support */
        if (mg_str_ieq(t, "hitfallset")) {
            int val = ctl_param_i(c, en, "value", -1);
            if (val >= 0) en->gethit.fall_fall = val;
        }
    }
    else if (mg_str_ieq(t, "remappal")) {
        en->remap_pal = ctl_param_i(c, en, "destination", 0);
    }
    else if (mg_str_ieq(t, "makedust")) {
        mg_spawn_dust(en);
    }
    else if (mg_str_ieq(t, "projectile")) {
        mg_spawn_projectile(en, c);
    }
    else if (mg_str_ieq(t, "helper") || mg_str_ieq(t, "destroyself")) {
        /* helper system: MVP stub */
    }
    else if (mg_str_ieq(t, "angleDraw") || mg_str_ieq(t, "angleadd") ||
             mg_str_ieq(t, "angleset") || mg_str_ieq(t, "anglemul")) {
        /* rotation — partial support */
        if (mg_str_ieq(t, "angleset")) en->angle = ctl_param_i(c, en, "value", 0);
        if (mg_str_ieq(t, "angleadd")) en->angle += ctl_param_i(c, en, "value", 0);
    }
    else if (mg_str_ieq(t, "trans")) {
        /* transparency sctrl — stored for render */
        en->trans_mode = ctl_param_i(c, en, "trans", 0);
    }
    /* unknown sctrls: silently skipped (MUGEN behavior with warnings) */
}

/* ---------------- entity state execution ---------------- */
void mg_run_entity(MgEntity *en) {
    if (!en->ch || !en->alive) return;

    /* hitpause freezes state execution but not hitshake visuals */
    if (en->hitpause_time > 0) {
        en->hitpause_time--;
        return;
    }

    /* per-frame special flags reset */
    en->special_nowalk = 0; en->special_nobrake = 0; en->special_invincible = 0;
    en->special_noshadow = 0; en->special_nostandguard = 0; en->special_nocrouchguard = 0;
    en->special_noairguard = 0; en->special_nojugglecheck = 0;

    /* command processing (state -1).
     * Ikemen GO pipeline: key timers from held buttons (+facing), then the
     * per-tick command matcher fills the trigger buffers that state -1 and
     * CNS `Command("name")` expressions test (see input.c). */
    if (!en->is_helper) {
        mg_input_update(en);
        mg_commands_step(en);

        /* engine-side default movement (MUGEN hardcodes stand/walk/crouch/jump
         * transitions when ctrl is on — chars get these without any CNS code) */
        if (en->ctrl && g_game.round_state == 2) {
            uint32_t fwd = (en->facing > 0) ? BTN_FWD : BTN_BACK;
            uint32_t back = (en->facing > 0) ? BTN_BACK : BTN_FWD;
            int holdingF = (en->buttons & fwd) != 0;
            int holdingB = (en->buttons & back) != 0;
            int holdingD = (en->buttons & BTN_D) != 0;
            int holdingU = (en->buttons & BTN_U) != 0;
            if (en->state_no == 0) {
                if (holdingD) mg_change_state(en, 11, -1, -1);         /* crouch */
                else if (holdingU) mg_change_state(en, 40, -1, -1);    /* jump start */
                else if (holdingF || holdingB) mg_change_state(en, 20, -1, -1);  /* walk */
            } else if (en->state_no == 20) {
                if (holdingD) mg_change_state(en, 11, -1, -1);
                else if (holdingU) mg_change_state(en, 40, -1, -1);
                else if (!holdingF && !holdingB) mg_change_state(en, 0, -1, -1);
            } else if (en->state_no == 11 || en->state_no == 12) {
                if (!holdingD) mg_change_state(en, 12, -1, -1);        /* stand up */
            }
        }
        /* engine-side get-up from lying down (5110): Elecbyte docs —
         * "Hit_liedown (5110): player changes to get-up state (5120) when
         * ready". The CNS has no such ChangeState (common1.cns 5110 ends
         * at ForceFeedback) — MUGEN hardcodes it. v0.2.2: without this, a
         * knocked-down character lay in 5110 forever and the fight stalled
         * (fight-flow "entity stuck >600 frames" regression). Gate: alive +
         * lie-down time (data.liedown.time, default 60) elapsed. 5120's own
         * ChangeState (AnimTime=0 -> state 0 ctrl=1) finishes the recovery. */
        if (en->state_no == 5110 && en->alive &&
            en->state_time >= (int)mg_char_const(en->ch, "data.liedown.time", 60))
            mg_change_state(en, 5120, -1, -1);
        /* run state -1 (input commands) */
        MgState *neg = mg_find_state(en, -1);
        if (neg && en->ctrl) {
            for (int i = 0; i < neg->nctls; i++) {
                if (ctl_triggers_met(&neg->ctls[i], en))
                    exec_sctrl(en, &neg->ctls[i]);
            }
        }
        /* state -3 (always-active states) */
        MgState *neg3 = mg_find_state(en, -3);
        if (neg3) {
            for (int i = 0; i < neg3->nctls; i++)
                if (ctl_triggers_met(&neg3->ctls[i], en))
                    exec_sctrl(en, &neg3->ctls[i]);
        }
    }

    /* current state controllers */
    MgState *st = mg_find_state(en, en->state_no);
    if (st) {
        for (int i = 0; i < st->nctls; i++) {
            if (ctl_triggers_met(&st->ctls[i], en))
                exec_sctrl(en, &st->ctls[i]);
        }
    }

    en->state_time++;
    mg_update_anim(en);
    mg_update_clsn(en);
}
