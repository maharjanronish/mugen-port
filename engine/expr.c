/* expr.c — MUGEN trigger expression parser + evaluator
 * Recursive descent with MUGEN operator precedence.
 * Trigger set covers common1.cns + KFM requirements (verified by vocabulary scan).
 */
#include "mugen.h"

/* forward decls */
static MgVal eval_node(MgExpr *e, MgEntity *en);

/* ---------------- lexer ---------------- */
typedef struct {
    const char *s;
    int pos;
    int tok;          /* token type: TOK_EOF, TOK_NUM, TOK_ID, TOK_STR, TOK_OP, TOK_COMMA */
    int64_t num_i; double num_f; int num_isfloat;
    char id[64];
    char str[64];
    int op;           /* operator code (ASCII or OP_*) */
} Lexer;

enum { TOK_EOF=0, TOK_NUM, TOK_ID, TOK_STR, TOK_OP, TOK_COMMA, TOK_LPAREN, TOK_RPAREN };

static void lx_next(Lexer *lx) {
    const char *s = lx->s;
    int p = lx->pos;
    while (s[p] == ' ' || s[p] == '\t') p++;
    lx->tok = TOK_EOF;
    if (!s[p]) { lx->pos = p; return; }
    if (s[p] >= '0' && s[p] <= '9' || (s[p] == '.' && s[p+1] >= '0' && s[p+1] <= '9')) {
        char *end;
        lx->num_f = strtod(s + p, &end);
        lx->num_i = (int64_t)lx->num_f;
        lx->num_isfloat = 0;
        for (const char *q = s + p; q < end; q++) if (*q == '.' || *q == 'e' || *q == 'E') lx->num_isfloat = 1;
        lx->tok = TOK_NUM;
        lx->pos = (int)(end - s);
        return;
    }
    if ((s[p] >= 'a' && s[p] <= 'z') || (s[p] >= 'A' && s[p] <= 'Z') || s[p] == '_') {
        int n = 0;
        while ((s[p] >= 'a' && s[p] <= 'z') || (s[p] >= 'A' && s[p] <= 'Z') || (s[p] >= '0' && s[p] <= '9') || s[p] == '_' || s[p] == '.') {
            if (n < 63) lx->id[n++] = s[p];
            p++;
        }
        lx->id[n] = 0;
        lx->tok = TOK_ID;
        lx->pos = p;
        return;
    }
    if (s[p] == '"') {
        p++;
        int n = 0;
        while (s[p] && s[p] != '"' && n < 63) lx->str[n++] = s[p++];
        lx->str[n] = 0;
        if (s[p] == '"') p++;
        lx->tok = TOK_STR;
        lx->pos = p;
        return;
    }
    if (s[p] == ',') { lx->tok = TOK_COMMA; lx->op = ','; lx->pos = p + 1; return; }
    if (s[p] == '(') { lx->tok = TOK_LPAREN; lx->pos = p + 1; return; }
    if (s[p] == ')') { lx->tok = TOK_RPAREN; lx->pos = p + 1; return; }
    /* operators */
    int op = 0;
    switch (s[p]) {
        case '=': op = '='; break;
        case '<': op = (s[p+1]=='=') ? OP_LE : '<'; break;
        case '>': op = (s[p+1]=='=') ? OP_GE : '>'; break;
        case '!': op = (s[p+1]=='=') ? OP_NOTEQ : OP_LOGNOT; break;
        case '~': op = OP_BITNOT; break;
        case '+': op = '+'; break;
        case '-': op = '-'; break;
        case '*': op = (s[p+1]=='*') ? OP_POW : '*'; break;
        case '/': op = '/'; break;
        case '%': op = '%'; break;
        case '&': op = (s[p+1]=='&') ? OP_LOGAND : '&'; break;
        case '|': op = (s[p+1]=='|') ? OP_LOGOR : '|'; break;
        case '^': op = (s[p+1]=='^') ? OP_XOR : '^'; break;
    }
    if (op) {
        lx->tok = TOK_OP;
        lx->op = op;
        if (op == OP_LE || op == OP_GE || op == OP_NOTEQ || op == OP_LOGAND || op == OP_LOGOR || op == OP_XOR || op == OP_POW) p += 2;
        else p += 1;
        lx->pos = p;
        return;
    }
    /* unknown char: skip */
    lx->pos = p + 1;
    lx_next(lx);
}

/* ---------------- trigger table ---------------- */
typedef struct { const char *name; int id; } TrigName;
static TrigName trig_names[] = {
    {"time", TR_TIME}, {"animelem", TR_ANIMELEM}, {"animtime", TR_ANIMTIME},
    {"animelemtime", TR_ANIMELEMTIME}, {"anim", TR_ANIM}, {"ctrl", TR_CTRL},
    {"stateno", TR_STATENO}, {"statetype", TR_STATETYPE}, {"movecontact", TR_MOVECONTACT},
    {"movehit", TR_MOVEHIT}, {"moveguarded", TR_MOVEGUARDED}, {"alive", TR_ALIVE},
    {"life", TR_LIFE}, {"lifemax", TR_LIFEMAX}, {"power", TR_POWER}, {"powermax", TR_POWERMAX},
    {"pos x", TR_POS_X}, {"pos y", TR_POS_Y}, {"vel x", TR_VEL_X}, {"vel y", TR_VEL_Y},
    {"command", TR_COMMAND}, {"hitdefattr", TR_HITDEFATTR}, {"inguarddist", TR_INGUARDDIST},
    {"p2bodydist x", TR_P2BODYDIST}, {"p2dist x", TR_P2DIST_X}, {"p2dist y", TR_P2DIST_Y},
    {"p2statetype", TR_P2STATETYPE}, {"p2movetype", TR_P2MOVETYPE},
    {"hitover", TR_HITOVER}, {"hitshakeover", TR_HITSHAKEOVER}, {"hitfall", TR_HITFALL},
    {"canrecover", TR_CANRECOVER}, {"hitvel x", TR_HITVEL_X}, {"hitvel y", TR_HITVEL_Y},
    {"var", TR_VAR}, {"fvar", TR_FVAR}, {"sysvar", TR_SYSVAR}, {"sysfvar", TR_SYSFVAR},
    {"selfanimexist", TR_SELFANIMEXIST}, {"animexist", TR_ANIMEXIST},
    {"numtarget", TR_NUMTARGET}, {"frontedgebodydist", TR_FRONTEDGEBODYDIST},
    {"backedgebodydist", TR_BACKEDGEBODYDIST}, {"frontedge", TR_FRONTEDGE},
    {"backedge", TR_BACKEDGE}, {"groundlevel", TR_GROUNDLEVEL},
    {"roundstate", TR_ROUNDSTATE}, {"random", TR_RANDOM}, {"teamside", TR_TEAMSIDE},
    {"uniqhitcount", TR_UNIQHITCOUNT}, {"hitcount", TR_HITCOUNT},
    {"guardcount", TR_GUARDCOUNT}, {"ifelse", TR_IFELSE}, {"const", TR_CONST},
    {"const720p", TR_CONST720P}, {"abs", TR_Abs}, {"ceil", TR_ceil}, {"floor", TR_floor},
    {"min", TR_min}, {"max", TR_max}, {"sign", TR_sign},
    {"gametime", TR_GAMETIME}, {"ishelper", TR_ISHELPER}, {"facing", TR_FACING},
    {"prevstateno", TR_PREVSTATENO}, {"pi", TR_PI},
    {"p2life", TR_P2LIFE}, {"p2state", TR_P2STATE}, {"p2stateno", TR_P2STATENO},
    {"p2ctrl", TR_P2CTRL}, {"p2dist", TR_P2DIST},
    {"movevictim", TR_MOVEVICTIM}, {"movetype", TR_MOVETYPE_},
    {"combo", TR_COMBO}, {"airjuggle", TR_AIRJUGGLE},
    {NULL, 0}
};

static int trig_lookup(const char *name) {
    char low[64];
    strncpy(low, name, 63); low[63] = 0; mg_str_lower(low);
    /* try multiword: "pos x" style: id may be "pos" and next token "x" handled by parser */
    for (TrigName *t = trig_names; t->name; t++)
        if (mg_str_ieq(t->name, low)) return t->id;
    return 0;
}

/* ---------------- parser ---------------- */
static MgExpr *ex_new(int type) {
    MgExpr *e = (MgExpr*)calloc(1, sizeof(MgExpr));
    e->type = type;
    return e;
}

void mg_expr_free(MgExpr *e) {
    if (!e) return;
    mg_expr_free(e->a);
    mg_expr_free(e->b);
    free(e);
}

static MgExpr *parse_expr(Lexer *lx);

/* parse a "pos x" style two-word trigger */
static MgExpr *parse_primary(Lexer *lx) {
    if (lx->tok == TOK_NUM) {
        MgExpr *e = ex_new(lx->num_isfloat ? ET_FLOAT : ET_INT);
        e->ival = lx->num_i; e->fval = lx->num_f;
        lx_next(lx);
        return e;
    }
    if (lx->tok == TOK_STR) {
        MgExpr *e = ex_new(ET_STR);
        strncpy(e->sval, lx->str, 47);
        lx_next(lx);
        return e;
    }
    if (lx->tok == TOK_OP && (lx->op == '-' || lx->op == OP_LOGNOT || lx->op == OP_BITNOT)) {
        int op = lx->op;
        lx_next(lx);
        MgExpr *e = ex_new(ET_NODE);
        e->op = (op == '-') ? OP_NEG : op;
        e->a = parse_primary(lx);
        return e;
    }
    if (lx->tok == TOK_LPAREN) {
        lx_next(lx);
        MgExpr *e = parse_expr(lx);
        if (lx->tok == TOK_RPAREN) lx_next(lx);
        return e;
    }
    if (lx->tok == TOK_ID) {
        char id[64];
        strncpy(id, lx->id, 63); id[63] = 0;
        lx_next(lx);
        char low[64]; strncpy(low, id, 63); low[63] = 0; mg_str_lower(low);

        /* two-word triggers: pos x / vel y / p2dist x etc. */
        if (lx->tok == TOK_ID) {
            char two[128];
            snprintf(two, sizeof(two), "%s %s", low, lx->id);
            mg_str_lower(two);
            int t2 = trig_lookup(two);
            if (t2) {
                lx_next(lx);
                MgExpr *e = ex_new(ET_TRIG);
                e->trig = t2;
                return e;
            }
        }
        int t = trig_lookup(low);
        if (t) {
            MgExpr *e = ex_new(ET_TRIG);
            e->trig = t;
            /* argument: (expr[, expr[, expr]]) or bare int */
            if (lx->tok == TOK_LPAREN) {
                lx_next(lx);
                e->a = parse_expr(lx);
                if (lx->tok == TOK_COMMA) { lx_next(lx); e->b = parse_expr(lx); }
                if (lx->tok == TOK_COMMA) { lx_next(lx); e->c = parse_expr(lx); }
                if (lx->tok == TOK_RPAREN) lx_next(lx);
            } else if (lx->tok == TOK_NUM) {
                e->a = ex_new(lx->num_isfloat ? ET_FLOAT : ET_INT);
                e->a->ival = lx->num_i; e->a->fval = lx->num_f;
                lx_next(lx);
            } else if (lx->tok == TOK_ID) {
                MgExpr *arg = ex_new(ET_TRIG);
                int t2 = trig_lookup(lx->id);
                if (t2) { arg->trig = t2; lx_next(lx); e->a = arg; }
            } else if (lx->tok == TOK_STR) {
                e->a = ex_new(ET_STR);
                strncpy(e->a->sval, lx->str, 47);
                lx_next(lx);
            }
            return e;
        }
        /* unknown identifier: keep as string (statetype = S, movetype = A, const names) */
        MgExpr *e = ex_new(ET_STR);
        strncpy(e->sval, id, 47);
        return e;
    }
    /* error / EOF: 0 */
    MgExpr *e = ex_new(ET_INT);
    e->ival = 0;
    if (lx->tok != TOK_EOF) lx_next(lx);
    return e;
}

static int prec_of(int op) {
    switch (op) {
        case OP_LOGOR: return 1;
        case OP_XOR: return 2;
        case OP_LOGAND: return 3;
        case '|': return 4;
        case '^': return 5;
        case '&': return 6;
        case '=': case OP_NOTEQ: return 7;
        case '<': case '>': case OP_LE: case OP_GE: return 8;
        case OP_POW: return 10;
        case '+': case '-': return 9;
        case '*': case '/': case '%': return 10;
    }
    return 0;
}

/* precedence climbing */
static MgExpr *parse_prec(Lexer *lx, int minprec) {
    MgExpr *lhs = parse_primary(lx);
    for (;;) {
        if (lx->tok != TOK_OP) break;
        int op = lx->op;
        int p = prec_of(op);
        if (p < minprec || p == 0) break;
        lx_next(lx);
        MgExpr *rhs = parse_prec(lx, p + 1);
        MgExpr *n = ex_new(ET_NODE);
        n->op = op;
        n->a = lhs; n->b = rhs;
        lhs = n;
    }
    return lhs;
}

static MgExpr *parse_expr(Lexer *lx) {
    return parse_prec(lx, 1);
}

MgExpr *mg_expr_parse(const char *src) {
    if (!src || !*src) return NULL;
    Lexer lx;
    memset(&lx, 0, sizeof(lx));
    lx.s = src;
    lx_next(&lx);
    return parse_expr(&lx);
}

/* ---------------- evaluation ---------------- */
static double val_d(MgVal v) { return v.is_float ? v.f : (double)v.i; }
static int64_t val_i(MgVal v) { return v.is_float ? (int64_t)v.f : v.i; }

static MgEntity *p2_of(MgEntity *en) {
    /* the other player entity */
    for (int i = 0; i < g_nentities; i++) {
        if (&g_entities[i] != en && !g_entities[i].is_helper && g_entities[i].alive >= 0)
            return &g_entities[i];
    }
    return NULL;
}

/* statetype as trigger: returns 1 for S, 2 for C, 3 for A, 4 for L compared vs string */
static int statetype_code(char t) {
    switch (t) { case 'S': return 1; case 'C': return 2; case 'A': return 3; case 'L': return 4; }
    return 0;
}

static MgVal eval_trig(int trig, MgExpr *arg, MgExpr *arg2, MgEntity *en) {
    MgVal v; v.is_float = 0; v.i = 0; v.f = 0;
    if (!en) return v;
    MgEntity *p2 = p2_of(en);
    switch (trig) {
        case TR_TIME: v.i = en->state_time; break;
        case TR_ANIMELEM: {
            int n = (int)val_i(eval_node(arg, en));
            MgAnim *a = en->cur_anim;
            if (!a) { v.i = 0; break; }
            /* animelem(n): true when on nth element (and within its duration) */
            int t = en->anim_time, idx = 0;
            for (int i = 0; i < a->nframes; i++) {
                int tk = a->frames[i].ticks <= 0 ? 1 : a->frames[i].ticks;
                if (t < tk) { idx = i + 1; break; }
                t -= tk;
                if (i == a->nframes - 1) { idx = a->loopstart + 1; t = 0; }
            }
            v.i = (idx == n) ? 1 : 0;
            break;
        }
        case TR_ANIMELEMTIME: {
            int n = (int)val_i(eval_node(arg, en));
            MgAnim *a = en->cur_anim;
            if (!a) break;
            int t = en->anim_time, start = 0;
            for (int i = 0; i < a->nframes && i < n - 1; i++)
                start += a->frames[i].ticks <= 0 ? 1 : a->frames[i].ticks;
            v.i = t - start;
            break;
        }
        case TR_ANIMTIME: {
            MgAnim *a = en->cur_anim;
            if (!a) break;
            /* total ticks (negative = looping) */
            int total = 0;
            for (int i = 0; i < a->nframes; i++) total += a->frames[i].ticks <= 0 ? 1 : a->frames[i].ticks;
            v.i = (en->anim_time >= total) ? 0 : (total - en->anim_time);
            break;
        }
        case TR_ANIM: v.i = en->anim_no; break;
        case TR_CTRL: v.i = en->ctrl; break;
        case TR_STATENO: v.i = en->state_no; break;
        case TR_STATETYPE: v.i = statetype_code(en->statetype); break;
        case TR_MOVETYPE_: v.i = en->movetype == 'I' ? 1 : en->movetype == 'A' ? 2 : 3; break;
        case TR_MOVECONTACT: v.i = en->move_contact; break;
        case TR_MOVEHIT: v.i = en->move_hit; break;
        case TR_MOVEGUARDED: v.i = en->move_guarded; break;
        case TR_ALIVE: v.i = en->alive; break;
        case TR_LIFE: v.i = en->life; break;
        case TR_LIFEMAX: v.i = en->lifemax; break;
        case TR_POWER: v.i = en->power; break;
        case TR_POWERMAX: v.i = en->powermax; break;
        case TR_POS_X: v.is_float = 1; v.f = en->pos_x; break;
        case TR_POS_Y: v.is_float = 1; v.f = en->pos_y; break;
        case TR_VEL_X: v.is_float = 1; v.f = en->vel_x; break;
        case TR_VEL_Y: v.is_float = 1; v.f = en->vel_y; break;
        case TR_COMMAND: {
            /* handled specially: compare with string operand at node level */
            v.i = 0;
            break;
        }
        case TR_HITDEFATTR: v.i = en->attacking ? en->hitdef.attr : 0; break;
        case TR_INGUARDDIST: v.i = en->inguarddist; break;
        case TR_P2BODYDIST: {
            if (!p2) break;
            double d = fabs(p2->pos_x - en->pos_x);
            double fw = mg_char_const(en->ch, "size.ground.front", 16) * (en->coord_scale > 0 ? en->coord_scale : 1.0)
                      + mg_char_const(p2->ch, "size.ground.front", 16) * (p2->coord_scale > 0 ? p2->coord_scale : 1.0);
            v.is_float = 1; v.f = d - fw;
            break;
        }
        case TR_P2DIST_X: if (p2) { v.is_float = 1; v.f = (p2->pos_x - en->pos_x) * ((en->facing > 0) ? 1 : -1); } break;
        case TR_P2DIST_Y: if (p2) { v.is_float = 1; v.f = p2->pos_y - en->pos_y; } break;
        case TR_P2STATETYPE: v.i = p2 ? statetype_code(p2->statetype) : 0; break;
        case TR_P2MOVETYPE: v.i = !p2 ? 0 : (p2->movetype == 'I' ? 1 : p2->movetype == 'A' ? 2 : 3); break;
        case TR_P2LIFE: v.i = p2 ? p2->life : 0; break;
        case TR_HITOVER: v.i = en->gethit.hittime_left <= 0 && en->gethit.hitshaketime_left <= 0; break;
        case TR_HITSHAKEOVER: v.i = en->gethit.hitshaketime_left <= 0; break;
        case TR_HITFALL: v.i = en->gethit.fall_fall; break;
        case TR_CANRECOVER: v.i = en->canrecover; break;
        case TR_HITVEL_X: v.is_float = 1; v.f = en->gethit.xvel; break;
        case TR_HITVEL_Y: v.is_float = 1; v.f = en->gethit.yvel; break;
        case TR_VAR: {
            int idx = (int)val_i(eval_node(arg, en));
            v.i = (idx >= 0 && idx < 64) ? en->var[idx] : 0;
            break;
        }
        case TR_FVAR: {
            int idx = (int)val_i(eval_node(arg, en));
            v.is_float = 1; v.f = (idx >= 0 && idx < 64) ? en->fvar[idx] : 0;
            break;
        }
        case TR_SYSVAR: {
            int idx = (int)val_i(eval_node(arg, en));
            v.i = (idx >= 0 && idx < 4) ? en->sysvar[idx] : 0;
            break;
        }
        case TR_SYSFVAR: {
            int idx = (int)val_i(eval_node(arg, en));
            v.is_float = 1; v.f = (idx >= 0 && idx < 4) ? en->sysfvar[idx] : 0;
            break;
        }
        case TR_SELFANIMEXIST: case TR_ANIMEXIST: {
            int n = (int)val_i(eval_node(arg, en));
            v.i = mg_air_get(en->ch->air, n) != NULL;
            break;
        }
        case TR_NUMTARGET: v.i = 0; {
            for (int i = 0; i < g_nentities; i++) {
                if (g_entities[i].gethit.hitcount > 0 && !g_entities[i].is_helper && &g_entities[i] != en) v.i++;
            }
            break;
        }
        case TR_FRONTEDGEBODYDIST: {
            double e = mg_stage_right_edge() - en->pos_x;
            v.is_float = 1; v.f = e - mg_char_const(en->ch, "size.ground.front", 16);
            break;
        }
        case TR_BACKEDGEBODYDIST: {
            double e = en->pos_x - mg_stage_left_edge();
            v.is_float = 1; v.f = e - mg_char_const(en->ch, "size.ground.back", 16);
            break;
        }
        case TR_FRONTEDGE: v.is_float = 1; v.f = mg_stage_right_edge() - en->pos_x; break;
        case TR_BACKEDGE: v.is_float = 1; v.f = en->pos_x - mg_stage_left_edge(); break;
        case TR_GROUNDLEVEL: {
            /* distance to floor: floor is at pos_y = 0 */
            v.is_float = 1; v.f = -en->pos_y;
            break;
        }
        case TR_ROUNDSTATE:
            /* 0 pre-round, 1 round intro anims, 2 fight, 3 match decided, 4 post */
            if (g_game.round_state == 0) v.i = 0;
            else if (g_game.round_state == 1) v.i = 1;
            else if (g_game.round_state == 2) v.i = 2;
            else v.i = 3;
            break;
        case TR_RANDOM: v.i = rand() % 1000; break;
        case TR_TEAMSIDE: v.i = en->teamside; break;
        case TR_UNIQHITCOUNT: v.i = en->uniq_hit_count; break;
        case TR_HITCOUNT: v.i = en->hit_count; break;
        case TR_GUARDCOUNT: v.i = en->guard_count; break;
        case TR_CONST: {
            /* const(param name) — bare string arg or compare node */
            v.is_float = 1; v.f = 0;
            if (arg && arg->type == ET_STR && en->ch)
                v.f = mg_char_const(en->ch, arg->sval, 0);
            break;
        }
        case TR_CONST720P: {
            double d = val_d(eval_node(arg, en));
            v.is_float = 1;
            v.f = d * (en->ch ? (double)en->ch->localcoord_h / 720.0 : 1.0);
            break;
        }
        case TR_Abs: { MgVal x = eval_node(arg, en); v.is_float = x.is_float; if (x.is_float) v.f = fabs(x.f); else v.i = llabs(x.i); break; }
        case TR_ceil: { MgVal x = eval_node(arg, en); v.is_float = 0; v.i = (int64_t)ceil(val_d(x)); break; }
        case TR_floor: { MgVal x = eval_node(arg, en); v.is_float = 0; v.i = (int64_t)floor(val_d(x)); break; }
        case TR_min: {
            MgVal a = eval_node(arg, en), b = eval_node(arg2, en);
            v.is_float = a.is_float || b.is_float;
            if (v.is_float) v.f = fmin(val_d(a), val_d(b)); else v.i = (a.i < b.i) ? a.i : b.i;
            break;
        }
        case TR_max: {
            MgVal a = eval_node(arg, en), b = eval_node(arg2, en);
            v.is_float = a.is_float || b.is_float;
            if (v.is_float) v.f = fmax(val_d(a), val_d(b)); else v.i = (a.i > b.i) ? a.i : b.i;
            break;
        }
        case TR_sign: {
            MgVal x = eval_node(arg, en);
            double d = val_d(x);
            v.i = (d > 0) - (d < 0);
            break;
        }
        case TR_GAMETIME: v.i = g_game.frames_done; break;
        case TR_ISHELPER: v.i = en->is_helper; break;
        case TR_FACING: v.i = en->facing; break;
        case TR_PREVSTATENO: v.i = en->prev_state_no; break;
        case TR_PI: v.is_float = 1; v.f = 3.14159265358979323846; break;
        case TR_COMBO: v.i = en->hit_count; break;
        default: break;
    }
    return v;
}

/* node evaluation with special cases for command/string compares */
static MgVal eval_node(MgExpr *e, MgEntity *en) {
    MgVal v; v.is_float = 0; v.i = 0; v.f = 0;
    if (!e) return v;
    switch (e->type) {
        case ET_INT: v.i = e->ival; break;
        case ET_FLOAT: v.is_float = 1; v.f = e->fval; break;
        case ET_STR: v.i = 1; break; /* strings used only in compare context */
        case ET_TRIG: return eval_trig(e->trig, e->a, e->b, en);
        case ET_NODE: {
            int op = e->op;
            /* special: command = "name" */
            if (op == '=' && e->a && e->a->type == ET_TRIG && e->a->trig == TR_COMMAND && e->b && e->b->type == ET_STR) {
                v.i = mg_check_command(en, e->b->sval);
                return v;
            }
            /* special: ifelse(cond, a, b) — trig node with a,b,c */
            if (e->a && e->a->type == ET_TRIG && e->a->trig == TR_IFELSE) {
                MgExpr *ie = e->a;
                MgVal cond = eval_node(ie->a, en);
                v = eval_node(val_i(cond) ? ie->b : ie->c, en);
                return v;
            }
            /* special: const("name") or const(name) — unknown id became ET_STR */
            if (e->a && e->a->type == ET_TRIG && e->a->trig == TR_CONST && e->b) {
                const char *key = (e->b->type == ET_STR) ? e->b->sval : NULL;
                if (key) {
                    v.is_float = 1;
                    v.f = mg_char_const(en->ch, key, 0);
                    return v;
                }
            }
            /* special: statetype = S / A / C / L (identifier compare) */
            if ((op == '=' || op == OP_NOTEQ) && e->a && e->a->type == ET_TRIG &&
                (e->a->trig == TR_STATETYPE || e->a->trig == TR_P2STATETYPE || e->a->trig == TR_MOVETYPE_ || e->a->trig == TR_P2MOVETYPE) &&
                e->b && e->b->type == ET_STR) {
                int code = 0;
                const char *s = e->b->sval;
                if (e->a->trig == TR_STATETYPE || e->a->trig == TR_P2STATETYPE) {
                    code = (s[0]=='S'||s[0]=='s') ? 1 : (s[0]=='C'||s[0]=='c') ? 2 : (s[0]=='A'||s[0]=='a') ? 3 : 4;
                } else {
                    code = (s[0]=='I'||s[0]=='i') ? 1 : (s[0]=='A'||s[0]=='a') ? 2 : 3;
                }
                MgVal lhs = eval_node(e->a, en);
                v.i = (op == '=') ? (lhs.i == code) : (lhs.i != code);
                return v;
            }
            MgVal a = eval_node(e->a, en);
            MgVal b = (e->b) ? eval_node(e->b, en) : a;
            switch (op) {
                case '+': v.is_float = a.is_float || b.is_float; if (v.is_float) v.f = val_d(a) + val_d(b); else v.i = a.i + b.i; break;
                case '-': v.is_float = a.is_float || b.is_float; if (v.is_float) v.f = val_d(a) - val_d(b); else v.i = a.i - b.i; break;
                case '*': v.is_float = a.is_float || b.is_float; if (v.is_float) v.f = val_d(a) * val_d(b); else v.i = a.i * b.i; break;
                case '/': v.is_float = 1; v.f = (val_d(b) != 0) ? val_d(a) / val_d(b) : 0; break;
                case '%': v.is_float = 0; v.i = (val_i(b) != 0) ? val_i(a) % val_i(b) : 0; break;
                case OP_POW: v.is_float = 1; v.f = pow(val_d(a), val_d(b)); break;
                case '<': v.is_float = 0; v.i = val_d(a) < val_d(b); break;
                case '>': v.is_float = 0; v.i = val_d(a) > val_d(b); break;
                case OP_LE: v.is_float = 0; v.i = val_d(a) <= val_d(b); break;
                case OP_GE: v.is_float = 0; v.i = val_d(a) >= val_d(b); break;
                case '=': v.is_float = 0; v.i = val_d(a) == val_d(b); break;
                case OP_NOTEQ: v.is_float = 0; v.i = val_d(a) != val_d(b); break;
                case '&': v.is_float = 0; v.i = val_i(a) & val_i(b); break;
                case '|': v.is_float = 0; v.i = val_i(a) | val_i(b); break;
                case '^': v.is_float = 0; v.i = val_i(a) ^ val_i(b); break;
                case OP_AND: case OP_LOGAND: v.is_float = 0; v.i = val_i(a) && val_i(b); break;
                case OP_OR: case OP_LOGOR: v.is_float = 0; v.i = val_i(a) || val_i(b); break;
                case OP_XOR: v.is_float = 0; v.i = (!val_i(a) != !val_i(b)); break;
                case OP_LOGNOT: v.is_float = 0; v.i = !val_i(a); break;
                case OP_BITNOT: v.is_float = 0; v.i = ~val_i(a); break;
                case OP_NEG: v.is_float = a.is_float; if (a.is_float) v.f = -a.f; else v.i = -a.i; break;
                /* ifelse(c, a, b) parsed as trig with 3 args: handled in eval_trig? do here via node */
                default: break;
            }
            break;
        }
    }
    /* ifelse special: TR_IFELSE with a=cond b ignored; better: encode as node */
    return v;
}

MgVal mg_expr_eval(MgExpr *e, void *en) {
    return eval_node((MgExpr*)e, (MgEntity*)en);
}
