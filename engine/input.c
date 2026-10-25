/* input.c — Ikemen GO-style input & command system (faithful C port).
 *
 * Ported from Ikemen GO (github.com/ikemen-engine/Ikemen-GO), src/input.go:
 *   - InputBuffer: per-key hold/release tick timers. Timer > 0: key held for
 *     N ticks (1 = press edge this tick). Timer < 0: released N ticks ago
 *     (-1 = release edge this tick). This is what makes "one tap = one move":
 *     bare command elements match ONLY on the t==1 edge, never while held.
 *   - InputBuffer.State / StateCharge: per-symbol key queries with the
 *     / $ * ~ prefixes, diagonal strictness (perpendicular conflicts), $N etc.
 *   - Command compiler: comma steps, '+' AND / '|' OR, '>' greater steps,
 *     ~N and /N charge times, diagonals, AutoGreaterExpand ("F, F" becomes
 *     "F, >~F, >F"), MUGEN backward-compat (slash propagation, hold-only
 *     buffer=1), and the reverse loopOrder with DirToButton forward islands.
 *   - Command.Step: the per-tick matcher with per-step completion, overall
 *     time window, GreaterCheckFail, and curbuftime buffering (the value the
 *     CNS trigger Command("name") tests > 0, like Ikemen's Char.command()).
 *   - CommandList.InputUpdate equivalent: en->buttons (BTN_* bitmask, held
 *     state this tick) + en->facing -> U/D/L/R bools; B/F are derived
 *     facing-relative (fbFlip = facing<0), exactly like Ikemen.
 *
 * Why the rewrite: the old matcher (state.c v0.2.1) required command elements
 * on CONSECUTIVE history slots — multi-element motions (QCF, double-tap run)
 * could never match with human-speed d-pad slides, which read on-device as
 * "buttons not working properly". This port reproduces Ikemen GO semantics.
 */
#include "mugen.h"

/* ------------------------------------------------------------------ */
/* key identifiers (order: directions, diagonals, neutral, buttons)    */
/* MGK_L/MGK_R are PHYSICAL left/right; MGK_B/MGK_F are facing-relative */
/* ------------------------------------------------------------------ */
typedef enum {
    MGK_U, MGK_D, MGK_B, MGK_F, MGK_L, MGK_R,
    MGK_UB, MGK_UF, MGK_DB, MGK_DF,
    MGK_UL, MGK_UR, MGK_DL, MGK_DR,
    MGK_N,
    MGK_a, MGK_b, MGK_c, MGK_x, MGK_y, MGK_z, MGK_s,
    MGK__COUNT
} MgKey;

static int mgk_is_dir(MgKey k) { return k >= MGK_U && k <= MGK_N; }
static int mgk_is_btn(MgKey k) { return k >= MGK_a && k <= MGK_s; }

/* ---------------- input buffers (one per player) ---------------- */
typedef struct {
    int32_t b[MGK__COUNT];   /* current tick timer */
    int32_t p[MGK__COUNT];   /* previous tick timer */
} MgInputBuffer;

static MgInputBuffer g_ibuf[MG_MAX_PLAYERS];

/* Ikemen: update(held, buffer) */
static void upd_timer(int held, int32_t *buf) {
    if (held != (*buf > 0)) { *buf = held ? 1 : -1; return; }
    *buf += held ? 1 : -1;
}

/* Ikemen: InputBuffer.updateInputTime — one tick of key timers.
 * fbFlip: facing<0 -> B=R, F=L (facing left). else B=L, F=R. */
void mg_input_update(MgEntity *en) {
    if (!en) return;
    MgInputBuffer *ib = &g_ibuf[en->player_no & 1];
    for (int k = 0; k < MGK__COUNT; k++) ib->p[k] = ib->b[k];

    int U  = (en->buttons & BTN_U) != 0;
    int D  = (en->buttons & BTN_D) != 0;
    int L  = (en->buttons & BTN_BACK) != 0;   /* physical left  */
    int R  = (en->buttons & BTN_FWD) != 0;    /* physical right */
    int fbflip = en->facing < 0;
    int B  = fbflip ? R : L;
    int F  = fbflip ? L : R;
    int a  = (en->buttons & BTN_A) != 0;
    int b  = (en->buttons & BTN_B) != 0;
    int c  = (en->buttons & BTN_C) != 0;
    int x  = (en->buttons & BTN_X) != 0;
    int y  = (en->buttons & BTN_Y) != 0;
    int z  = (en->buttons & BTN_Z) != 0;
    int s  = (en->buttons & BTN_S) != 0;

    upd_timer(U, &ib->b[MGK_U]);
    upd_timer(D, &ib->b[MGK_D]);
    upd_timer(L, &ib->b[MGK_L]);
    upd_timer(R, &ib->b[MGK_R]);
    upd_timer(B, &ib->b[MGK_B]);
    upd_timer(F, &ib->b[MGK_F]);
    upd_timer(!(U || D || L || R || B || F), &ib->b[MGK_N]);
    upd_timer(a, &ib->b[MGK_a]);
    upd_timer(b, &ib->b[MGK_b]);
    upd_timer(c, &ib->b[MGK_c]);
    upd_timer(x, &ib->b[MGK_x]);
    upd_timer(y, &ib->b[MGK_y]);
    upd_timer(z, &ib->b[MGK_z]);
    upd_timer(s, &ib->b[MGK_s]);
}

/* ------------------------------------------------------------------ */
/* State queries (Ikemen InputBuffer.State)                            */
/* ------------------------------------------------------------------ */
static int32_t mgx_max(int32_t a, int32_t b) { return a > b ? a : b; }
static int32_t mgx_min(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t mgx_abs(int32_t v) { return v < 0 ? -v : v; }

#define TB(k)  (ib->b[MGK_##k])
#define TP(k)  (ib->p[MGK_##k])

static int32_t ibuf_state(const MgInputBuffer *ib, MgKey key, int tilde,
                           int dollar, int star) {
    int simple = !dollar && !star;

    /* ---- hold simple directions (strict: perpendicular dirs conflict) ---- */
    if (!tilde && simple) {
        switch (key) {
            case MGK_U: return mgx_min(-mgx_max(TB(B), mgx_max(TB(D), TB(F))), TB(U));
            case MGK_D: return mgx_min(-mgx_max(TB(B), mgx_max(TB(U), TB(F))), TB(D));
            case MGK_B: return mgx_min(-mgx_max(TB(D), mgx_max(TB(U), TB(F))), TB(B));
            case MGK_F: return mgx_min(-mgx_max(TB(D), mgx_max(TB(U), TB(B))), TB(F));
            case MGK_L: return mgx_min(-mgx_max(TB(D), mgx_max(TB(U), TB(R))), TB(L));
            case MGK_R: return mgx_min(-mgx_max(TB(D), mgx_max(TB(U), TB(L))), TB(R));
            case MGK_UF: return mgx_min(-mgx_max(TB(D), TB(B)), mgx_min(TB(U), TB(F)));
            case MGK_UB: return mgx_min(-mgx_max(TB(D), TB(F)), mgx_min(TB(U), TB(B)));
            case MGK_DF: return mgx_min(-mgx_max(TB(U), TB(B)), mgx_min(TB(D), TB(F)));
            case MGK_DB: return mgx_min(-mgx_max(TB(U), TB(F)), mgx_min(TB(D), TB(B)));
            case MGK_UL: return mgx_min(-mgx_max(TB(D), TB(R)), mgx_min(TB(U), TB(L)));
            case MGK_UR: return mgx_min(-mgx_max(TB(D), TB(L)), mgx_min(TB(U), TB(R)));
            case MGK_DL: return mgx_min(-mgx_max(TB(U), TB(R)), mgx_min(TB(D), TB(L)));
            case MGK_DR: return mgx_min(-mgx_max(TB(U), TB(L)), mgx_min(TB(D), TB(R)));
            case MGK_N:  return TB(N);
            default: break;
        }
    }
    /* ---- hold star directions (ignore conflicts) ---- */
    if (!tilde && star) {
        switch (key) {
            case MGK_U: return TB(U); case MGK_D: return TB(D);
            case MGK_B: return TB(B); case MGK_F: return TB(F);
            case MGK_L: return TB(L); case MGK_R: return TB(R);
            case MGK_UB: return mgx_max(TB(U), TB(B));
            case MGK_UF: return mgx_max(TB(U), TB(F));
            case MGK_DB: return mgx_max(TB(D), TB(B));
            case MGK_DF: return mgx_max(TB(D), TB(F));
            case MGK_UL: return mgx_max(TB(U), TB(L));
            case MGK_UR: return mgx_max(TB(U), TB(R));
            case MGK_DL: return mgx_max(TB(D), TB(L));
            case MGK_DR: return mgx_max(TB(D), TB(R));
            default: break;
        }
    }
    /* ---- hold dollar directions (ignore conflicting dirs, Min of Abs) ---- */
    if (!tilde && dollar) {
        switch (key) {
            case MGK_U: if (TB(U) > 0) return mgx_min(mgx_min(mgx_abs(TB(U)), mgx_abs(TB(D))),
                                                      mgx_min(mgx_abs(TB(B)), mgx_abs(TB(F))));
                break;
            case MGK_D: if (TB(D) > 0) return mgx_min(mgx_min(mgx_abs(TB(U)), mgx_abs(TB(D))),
                                                      mgx_min(mgx_abs(TB(B)), mgx_abs(TB(F))));
                break;
            case MGK_B: if (TB(B) > 0) return mgx_min(mgx_min(mgx_abs(TB(U)), mgx_abs(TB(D))),
                                                      mgx_min(mgx_abs(TB(B)), mgx_abs(TB(F))));
                break;
            case MGK_F: if (TB(F) > 0) return mgx_min(mgx_min(mgx_abs(TB(U)), mgx_abs(TB(D))),
                                                      mgx_min(mgx_abs(TB(B)), mgx_abs(TB(F))));
                break;
            case MGK_L: if (TB(L) > 0) return mgx_min(mgx_min(mgx_abs(TB(U)), mgx_abs(TB(D))),
                                                     mgx_min(mgx_abs(TB(L)), mgx_abs(TB(R))));
                break;
            case MGK_R: if (TB(R) > 0) return mgx_min(mgx_min(mgx_abs(TB(U)), mgx_abs(TB(D))),
                                                     mgx_min(mgx_abs(TB(L)), mgx_abs(TB(R))));
                break;
            /* $ accepts diagonals even with other dirs held */
            case MGK_UB: if (TB(U) > 0 && TB(B) > 0)
                    return mgx_min(mgx_min(mgx_abs(TB(U)), mgx_abs(TB(D))), mgx_min(mgx_abs(TB(B)), mgx_abs(TB(F))));
                break;
            case MGK_UF: if (TB(U) > 0 && TB(F) > 0)
                    return mgx_min(mgx_min(mgx_abs(TB(U)), mgx_abs(TB(D))), mgx_min(mgx_abs(TB(B)), mgx_abs(TB(F))));
                break;
            case MGK_DB: if (TB(D) > 0 && TB(B) > 0)
                    return mgx_min(mgx_min(mgx_abs(TB(U)), mgx_abs(TB(D))), mgx_min(mgx_abs(TB(B)), mgx_abs(TB(F))));
                break;
            case MGK_DF: if (TB(D) > 0 && TB(F) > 0)
                    return mgx_min(mgx_min(mgx_abs(TB(U)), mgx_abs(TB(D))), mgx_min(mgx_abs(TB(B)), mgx_abs(TB(F))));
                break;
            default: break;
        }
    }
    /* ---- release simple directions ---- */
    if (tilde && simple) {
        switch (key) {
            case MGK_U: if (TB(U) < 0 || TP(U) > 0)
                    return -mgx_min(-mgx_max(TB(B), mgx_max(TB(D), TB(F))), TB(U));
                break;
            case MGK_D: if (TB(D) < 0 || TP(D) > 0)
                    return -mgx_min(-mgx_max(TB(B), mgx_max(TB(U), TB(F))), TB(D));
                break;
            case MGK_B: if (TB(B) < 0 || TP(B) > 0)
                    return -mgx_min(-mgx_max(TB(D), mgx_max(TB(U), TB(F))), TB(B));
                break;
            case MGK_F: if (TB(F) < 0 || TP(F) > 0)
                    return -mgx_min(-mgx_max(TB(D), mgx_max(TB(U), TB(B))), TB(F));
                break;
            case MGK_L: if (TB(L) < 0 || TP(L) > 0)
                    return -mgx_min(-mgx_max(TB(D), mgx_max(TB(U), TB(R))), TB(L));
                break;
            case MGK_R: if (TB(R) < 0 || TP(R) > 0)
                    return -mgx_min(-mgx_max(TB(D), mgx_max(TB(U), TB(L))), TB(R));
                break;
            case MGK_UF:
                if ((TB(U) < 0 || TP(U) > 0) && (TB(F) < 0 || TP(F) > 0))
                    return -mgx_min(-mgx_max(TB(D), TB(B)), mgx_min(TB(U), TB(F)));
                break;
            case MGK_UB:
                if ((TB(U) < 0 || TP(U) > 0) && (TB(B) < 0 || TP(B) > 0))
                    return -mgx_min(-mgx_max(TB(D), TB(F)), mgx_min(TB(U), TB(B)));
                break;
            case MGK_DF:
                if ((TB(D) < 0 || TP(D) > 0) && (TB(F) < 0 || TP(F) > 0))
                    return -mgx_min(-mgx_max(TB(U), TB(B)), mgx_min(TB(D), TB(F)));
                break;
            case MGK_DB:
                if ((TB(D) < 0 || TP(D) > 0) && (TB(B) < 0 || TP(B) > 0))
                    return -mgx_min(-mgx_max(TB(U), TB(F)), mgx_min(TB(D), TB(B)));
                break;
            case MGK_UL:
                if ((TB(U) < 0 || TP(U) > 0) && (TB(L) < 0 || TP(L) > 0))
                    return -mgx_min(-mgx_max(TB(D), TB(R)), mgx_min(TB(U), TB(L)));
                break;
            case MGK_UR:
                if ((TB(U) < 0 || TP(U) > 0) && (TB(R) < 0 || TP(R) > 0))
                    return -mgx_min(-mgx_max(TB(D), TB(L)), mgx_min(TB(U), TB(R)));
                break;
            case MGK_DL:
                if ((TB(D) < 0 || TP(D) > 0) && (TB(L) < 0 || TP(L) > 0))
                    return -mgx_min(-mgx_max(TB(U), TB(R)), mgx_min(TB(D), TB(L)));
                break;
            case MGK_DR:
                if ((TB(D) < 0 || TP(D) > 0) && (TB(R) < 0 || TP(R) > 0))
                    return -mgx_min(-mgx_max(TB(U), TB(L)), mgx_min(TB(D), TB(R)));
                break;
            case MGK_N: return -TB(N);
            default: break;
        }
    }
    /* ---- release star directions ---- */
    if (tilde && star) {
        switch (key) {
            case MGK_U: if (TB(U) < 0 || TP(U) > 0) return -TB(U); break;
            case MGK_D: if (TB(D) < 0 || TP(D) > 0) return -TB(D); break;
            case MGK_B: if (TB(B) < 0 || TP(B) > 0) return -TB(B); break;
            case MGK_F: if (TB(F) < 0 || TP(F) > 0) return -TB(F); break;
            case MGK_L: if (TB(L) < 0 || TP(L) > 0) return -TB(L); break;
            case MGK_R: if (TB(R) < 0 || TP(R) > 0) return -TB(R); break;
            /* * diagonals trigger if either component is released */
            case MGK_UB:
                if ((TB(U) < 0 || TP(U) > 0) || (TB(B) < 0 || TP(B) > 0))
                    return -mgx_max(TB(U), TB(B));
                break;
            case MGK_UF:
                if ((TB(U) < 0 || TP(U) > 0) || (TB(F) < 0 || TP(F) > 0))
                    return -mgx_max(TB(U), TB(F));
                break;
            case MGK_DB:
                if ((TB(D) < 0 || TP(D) > 0) || (TB(B) < 0 || TP(B) > 0))
                    return -mgx_max(TB(D), TB(B));
                break;
            case MGK_DF:
                if ((TB(D) < 0 || TP(D) > 0) || (TB(F) < 0 || TP(F) > 0))
                    return -mgx_max(TB(D), TB(F));
                break;
            default: break;
        }
    }
    /* ---- release dollar directions (backward compatible way) ---- */
    if (tilde && dollar) {
        switch (key) {
            case MGK_U:
                if (TB(U) < 0 || TP(U) > 0) {
                    if (TB(U) < 0) return -TB(U);
                    return mgx_min(mgx_min(mgx_abs(TB(D)), mgx_abs(TB(B))), mgx_abs(TB(F)));
                } break;
            case MGK_D:
                if (TB(D) < 0 || TP(D) > 0) {
                    if (TB(D) < 0) return -TB(D);
                    return mgx_min(mgx_min(mgx_abs(TB(U)), mgx_abs(TB(B))), mgx_abs(TB(F)));
                } break;
            case MGK_B:
                if (TB(B) < 0 || TP(B) > 0) {
                    if (TB(B) < 0) return -TB(B);
                    return mgx_min(mgx_min(mgx_abs(TB(U)), mgx_abs(TB(D))), mgx_abs(TB(F)));
                } break;
            case MGK_F:
                if (TB(F) < 0 || TP(F) > 0) {
                    if (TB(F) < 0) return -TB(F);
                    return mgx_min(mgx_min(mgx_abs(TB(U)), mgx_abs(TB(D))), mgx_abs(TB(B)));
                } break;
            case MGK_DF:
                if ((TB(D) < 0 || TP(D) > 0) && (TB(F) < 0 || TP(F) > 0)) {
                    if (TB(D) < 0 || TB(F) < 0) return -mgx_min(TB(D), TB(F));
                    return mgx_min(mgx_abs(TB(U)), mgx_abs(TB(B)));
                } break;
            case MGK_DB:
                if ((TB(D) < 0 || TP(D) > 0) && (TB(B) < 0 || TP(B) > 0)) {
                    if (TB(D) < 0 || TB(B) < 0) return -mgx_min(TB(D), TB(B));
                    return mgx_min(mgx_abs(TB(U)), mgx_abs(TB(F)));
                } break;
            default: break;
        }
    }
    /* ---- hold buttons: raw timer ---- */
    if (!tilde) {
        switch (key) {
            case MGK_a: return TB(a); case MGK_b: return TB(b); case MGK_c: return TB(c);
            case MGK_x: return TB(x); case MGK_y: return TB(y); case MGK_z: return TB(z);
            case MGK_s: return TB(s);
            default: break;
        }
    }
    /* ---- release buttons: -timer while released-ish ---- */
    if (tilde) {
        switch (key) {
            case MGK_a: if (TB(a) < 0 || TP(a) > 0) return -TB(a); break;
            case MGK_b: if (TB(b) < 0 || TP(b) > 0) return -TB(b); break;
            case MGK_c: if (TB(c) < 0 || TP(c) > 0) return -TB(c); break;
            case MGK_x: if (TB(x) < 0 || TP(x) > 0) return -TB(x); break;
            case MGK_y: if (TB(y) < 0 || TP(y) > 0) return -TB(y); break;
            case MGK_z: if (TB(z) < 0 || TP(z) > 0) return -TB(z); break;
            case MGK_s: if (TB(s) < 0 || TP(s) > 0) return -TB(s); break;
            default: break;
        }
    }
    /* special $N: "any change" */
    if (dollar && key == MGK_N) {
        int32_t dirs = mgx_min(mgx_min(mgx_abs(TB(U)), mgx_abs(TB(D))),
                               mgx_min(mgx_abs(TB(B)), mgx_abs(TB(F))));
        int32_t btns = mgx_min(mgx_min(mgx_abs(TB(a)), mgx_abs(TB(b))),
                               mgx_min(mgx_abs(TB(c)), mgx_min(mgx_abs(TB(x)),
                               mgx_min(mgx_abs(TB(y)), mgx_abs(TB(z))))));
        return mgx_min(dirs, btns);
    }
    return 0;
}

/* Ikemen: StateCharge — how long the key was held (for ~N charge moves) */
static int32_t ignore_recent(int32_t buf) {
    return buf == 1 ? INT32_MIN : buf;
}
static int32_t ibuf_statecharge(const MgInputBuffer *ib, MgKey key, int tilde,
                                int dollar, int star) {
    int simple = !dollar && !star;
    /* hold dollar/star directions: raw */
    if (!tilde && (dollar || star)) {
        switch (key) {
            case MGK_U: return TB(U); case MGK_D: return TB(D);
            case MGK_B: return TB(B); case MGK_F: return TB(F);
            case MGK_L: return TB(L); case MGK_R: return TB(R);
            default: break;
        }
    }
    /* release dollar/star directions: previous tick value */
    if (tilde && (dollar || star)) {
        switch (key) {
            case MGK_U: return TP(U); case MGK_D: return TP(D);
            case MGK_B: return TP(B); case MGK_F: return TP(F);
            case MGK_L: return TP(L); case MGK_R: return TP(R);
            default: break;
        }
    }
    /* hold simple directions */
    if (!tilde && simple) {
        switch (key) {
            case MGK_U: return mgx_max(0, mgx_min(-mgx_max(TB(D), mgx_max(TB(B), TB(F))), TB(U)));
            case MGK_D: return mgx_max(0, mgx_min(-mgx_max(TB(U), mgx_max(TB(B), TB(F))), TB(D)));
            case MGK_B: return mgx_max(0, mgx_min(-mgx_max(TB(U), mgx_max(TB(D), TB(F))), TB(B)));
            case MGK_F: return mgx_max(0, mgx_min(-mgx_max(TB(U), mgx_max(TB(D), TB(B))), TB(F)));
            case MGK_L: return mgx_max(0, mgx_min(-mgx_max(TB(U), mgx_max(TB(D), TB(R))), TB(L)));
            case MGK_R: return mgx_max(0, mgx_min(-mgx_max(TB(U), mgx_max(TB(D), TB(L))), TB(R)));
            case MGK_UF: return mgx_max(0, mgx_min(-mgx_max(TB(D), TB(B)), mgx_min(TB(U), TB(F))));
            case MGK_UB: return mgx_max(0, mgx_min(-mgx_max(TB(D), TB(F)), mgx_min(TB(U), TB(B))));
            case MGK_DF: return mgx_max(0, mgx_min(-mgx_max(TB(U), TB(B)), mgx_min(TB(D), TB(F))));
            case MGK_DB: return mgx_max(0, mgx_min(-mgx_max(TB(U), TB(F)), mgx_min(TB(D), TB(B))));
            case MGK_N: return TB(N);
            default: break;
        }
    }
    /* release simple directions (previous values, ignore recently-pressed peers) */
    if (tilde && simple) {
        switch (key) {
            case MGK_U: return mgx_max(0, mgx_min(-mgx_max(ignore_recent(TB(B)),
                        mgx_max(ignore_recent(TB(D)), ignore_recent(TB(F)))), TP(U)));
            case MGK_D: return mgx_max(0, mgx_min(-mgx_max(ignore_recent(TB(U)),
                        mgx_max(ignore_recent(TB(B)), ignore_recent(TB(F)))), TP(D)));
            case MGK_B: return mgx_max(0, mgx_min(-mgx_max(ignore_recent(TB(U)),
                        mgx_max(ignore_recent(TB(D)), ignore_recent(TB(F)))), TP(B)));
            case MGK_F: return mgx_max(0, mgx_min(-mgx_max(ignore_recent(TB(U)),
                        mgx_max(ignore_recent(TB(D)), ignore_recent(TB(B)))), TP(F)));
            case MGK_DF: return mgx_max(0, mgx_min(-mgx_max(ignore_recent(TB(U)),
                        ignore_recent(TB(B))), mgx_min(TP(D), TP(F))));
            case MGK_DB: return mgx_max(0, mgx_min(-mgx_max(ignore_recent(TB(U)),
                        ignore_recent(TB(F))), mgx_min(TP(D), TP(B))));
            case MGK_N: return TP(N);
            default: break;
        }
    }
    /* hold buttons: raw */
    if (!tilde) {
        switch (key) {
            case MGK_a: return TB(a); case MGK_b: return TB(b); case MGK_c: return TB(c);
            case MGK_x: return TB(x); case MGK_y: return TB(y); case MGK_z: return TB(z);
            default: break;
        }
    }
    /* release buttons: previous tick value */
    if (tilde) {
        switch (key) {
            case MGK_a: return TP(a); case MGK_b: return TP(b); case MGK_c: return TP(c);
            case MGK_x: return TP(x); case MGK_y: return TP(y); case MGK_z: return TP(z);
            default: break;
        }
    }
    return 0;
}

/* ================================================================== */
/* Command compilation (Ikemen Command.ReadCommandSymbols)            */
/* ================================================================== */
#define MG_CS_MAXKEYS  8
#define MG_CC_MAXSTEPS 40

typedef struct {
    MgKey key;
    uint8_t slash, tilde, dollar, star;
    int32_t chargetime;
} MgCmdKey;

typedef struct {
    MgCmdKey keys[MG_CS_MAXKEYS];
    int nkeys;
    uint8_t greater, orlogic;
} MgCmdStep;

typedef struct MgCompiledCmd {
    MgCmdStep steps[MG_CC_MAXSTEPS];
    int nsteps;
    int32_t maxtime;      /* overall window (command.time)  */
    int32_t maxbuftime;   /* trigger buffer (buffer.time)   */
    int32_t maxsteptime;  /* per-step expiry; <0 = unlimited */
    int looporder[MG_CC_MAXSTEPS];
    int nloop;
} MgCompiledCmd;

/* runtime state per command, per entity */
typedef struct {
    uint8_t completed[MG_CC_MAXSTEPS];
    int32_t stepTimers[MG_CC_MAXSTEPS];
    int32_t curtime, curbuftime;
    uint8_t completeframe;
} MgCmdRun;

static MgCmdRun *g_rt[MG_MAX_ENTITIES];
static int        g_rt_n[MG_MAX_ENTITIES];
static MgChar    *g_rt_ch[MG_MAX_ENTITIES];

/* --- key char parsing --- */
static int keychar_to_mgkey(char c0, char c1, MgKey *out) {
    /* c1 != 0 means "look for a diagonal" */
    switch (c0) {
        case 'U': *out = MGK_U; break;
        case 'D': *out = MGK_D; break;
        case 'B': *out = MGK_B; break;
        case 'F': *out = MGK_F; break;
        case 'L': *out = MGK_L; break;
        case 'R': *out = MGK_R; break;
        case 'N': *out = MGK_N; break;
        case 'a': *out = MGK_a; return 1;
        case 'b': *out = MGK_b; return 1;
        case 'c': *out = MGK_c; return 1;
        case 'x': *out = MGK_x; return 1;
        case 'y': *out = MGK_y; return 1;
        case 'z': *out = MGK_z; return 1;
        case 's': *out = MGK_s; return 1;
        default: return 0;
    }
    if (c1 && (c0 == 'U' || c0 == 'D')) {
        switch (c1) {
            case 'B': *out = (c0 == 'U') ? MGK_UB : MGK_DB; return 2;
            case 'F': *out = (c0 == 'U') ? MGK_UF : MGK_DF; return 2;
            case 'L': *out = (c0 == 'U') ? MGK_UL : MGK_DL; return 2;
            case 'R': *out = (c0 == 'U') ? MGK_UR : MGK_DR; return 2;
        }
    }
    return 1;   /* plain direction */
}

/* Ikemen: IsDirectionPress / IsDirectionRelease / button variants */
static int ck_is_dir_press(const MgCmdKey *k)  { return !k->tilde && mgk_is_dir(k->key); }
static int ck_is_dir_rel(const MgCmdKey *k)    { return k->tilde && mgk_is_dir(k->key); }
static int ck_is_btn_press(const MgCmdKey *k)  { return !k->tilde && mgk_is_btn(k->key); }
static int ck_is_btn_rel(const MgCmdKey *k)    { return k->tilde && mgk_is_btn(k->key); }

static int step_is_single_direction(const MgCmdStep *cs) {
    return cs->nkeys == 1 && (ck_is_dir_press(&cs->keys[0]) || ck_is_dir_rel(&cs->keys[0]));
}

/* Ikemen: IsDirToButton — can the two steps complete on the same frame? */
static int step_is_dir_to_button(const MgCmdStep *cs, const MgCmdStep *next) {
    for (int i = 0; i < next->nkeys; i++)
        if (next->keys[i].slash) return 0;
    for (int i = 0; i < cs->nkeys; i++)
        if (ck_is_btn_press(&cs->keys[i]) || ck_is_btn_rel(&cs->keys[i])) return 0;
    for (int i = 0; i < cs->nkeys; i++)
        for (int j = 0; j < next->nkeys; j++)
            if (cs->keys[i].key == next->keys[j].key) return 0;
    for (int j = 0; j < next->nkeys; j++)
        if (ck_is_btn_press(&next->keys[j])) return 1;
    for (int i = 0; i < cs->nkeys; i++)
        if (ck_is_dir_rel(&cs->keys[i])) {
            for (int j = 0; j < next->nkeys; j++)
                if (!ck_is_dir_rel(&next->keys[j])) return 1;
        }
    return 0;
}

static int steps_equal(const MgCmdStep *a, const MgCmdStep *b) {
    if (a->greater != b->greater || a->orlogic != b->orlogic) return 0;
    if (a->nkeys != b->nkeys) return 0;
    for (int i = 0; i < a->nkeys; i++) {
        if (a->keys[i].key != b->keys[i].key ||
            a->keys[i].slash != b->keys[i].slash ||
            a->keys[i].tilde != b->keys[i].tilde ||
            a->keys[i].dollar != b->keys[i].dollar ||
            a->keys[i].star != b->keys[i].star ||
            a->keys[i].chargetime != b->keys[i].chargetime) return 0;
    }
    return 1;
}

/* Ikemen: AutoGreaterExpand — "F, F" -> "F, >~F, >F" (MUGEN default on) */
static void auto_greater_expand(MgCompiledCmd *c) {
    if (c->nsteps < 2) return;
    int need = 0;
    for (int i = 1; i < c->nsteps; i++)
        if (step_is_single_direction(&c->steps[i-1]) &&
            step_is_single_direction(&c->steps[i]) &&
            steps_equal(&c->steps[i-1], &c->steps[i])) { need = 1; break; }
    if (!need) return;

    MgCmdStep ns[MG_CC_MAXSTEPS];
    int nn = 0;
    ns[nn++] = c->steps[0];
    for (int i = 1; i < c->nsteps; i++) {
        MgCmdStep *prev = &c->steps[i-1], *curr = &c->steps[i];
        if (step_is_single_direction(prev) && step_is_single_direction(curr) &&
            steps_equal(prev, curr) && nn + 2 <= MG_CC_MAXSTEPS) {
            /* >~X (tilde inverted) then >X */
            MgCmdStep a; memset(&a, 0, sizeof(a));
            a.greater = 1;
            a.nkeys = 1;
            a.keys[0].key = curr->keys[0].key;
            a.keys[0].tilde = !curr->keys[0].tilde;
            a.keys[0].dollar = curr->keys[0].dollar;
            a.keys[0].star = curr->keys[0].star;
            ns[nn++] = a;
            MgCmdStep b; memset(&b, 0, sizeof(b));
            b.greater = 1;
            b.nkeys = curr->nkeys;
            for (int k = 0; k < curr->nkeys; k++) b.keys[k] = curr->keys[k];
            ns[nn++] = b;
        } else if (nn < MG_CC_MAXSTEPS) {
            ns[nn++] = *curr;
        }
    }
    memcpy(c->steps, ns, sizeof(MgCmdStep) * nn);
    c->nsteps = nn;
}

/* Ikemen: ApplyBackwardCompatibility (slash propagation; hold-only -> buf 1) */
static void backward_compat(MgCompiledCmd *c) {
    for (int i = 0; i < c->nsteps; i++) {
        MgCmdStep *st = &c->steps[i];
        if (st->orlogic) continue;
        int has_slash = 0;
        for (int k = 0; k < st->nkeys; k++)
            if (st->keys[k].slash) { has_slash = 1; break; }
        if (has_slash)
            for (int k = 0; k < st->nkeys; k++) st->keys[k].slash = 1;
    }
    if (c->nsteps > 0) {
        int hold_only = 1;
        for (int i = 0; i < c->nsteps && hold_only; i++)
            for (int k = 0; k < c->steps[i].nkeys; k++)
                if (!c->steps[i].keys[k].slash) { hold_only = 0; break; }
        if (hold_only && c->maxbuftime != 1) c->maxbuftime = 1;
    }
}

/* build loopOrder: reverse order with DirToButton forward islands */
static void build_looporder(MgCompiledCmd *c) {
    c->nloop = 0;
    int i = c->nsteps - 1;
    while (i >= 0) {
        if (i > 0 && step_is_dir_to_button(&c->steps[i-1], &c->steps[i])) {
            int start = i - 1, end = i;
            while (start > 0 && step_is_dir_to_button(&c->steps[start-1], &c->steps[start]))
                start--;
            for (int j = start; j <= end && c->nloop < MG_CC_MAXSTEPS; j++)
                c->looporder[c->nloop++] = j;
            i = start - 1;
        } else {
            if (c->nloop < MG_CC_MAXSTEPS) c->looporder[c->nloop++] = i;
            i--;
        }
    }
}

/* compile one MgCommand (raw string) into a MgCompiledCmd */
static int compile_command(const MgCommand *src, MgCompiledCmd *out) {
    memset(out, 0, sizeof(*out));
    out->maxtime = src->time > 0 ? src->time : 15;
    out->maxbuftime = src->buffer > 0 ? src->buffer : 1;
    out->maxsteptime = -1;   /* MUGEN default: unlimited per-step time */

    const char *s = src->cmd;
    if (!s) return 0;
    /* split on ',' */
    while (*s && out->nsteps < MG_CC_MAXSTEPS) {
        char part[64]; int pn = 0;
        while (*s && *s != ',' && pn < 60) part[pn++] = *s++;
        part[pn] = 0;
        if (*s == ',') s++;
        /* trim */
        char *p = part; while (*p == ' ' || *p == '\t') p++;
        char *e = p + strlen(p) - 1;
        while (e >= p && (*e == ' ' || *e == '\t')) *e-- = 0;
        if (!*p) continue;   /* empty token: skip (blank first step = blank cmd) */
        MgCmdStep *cs = &out->steps[out->nsteps];
        memset(cs, 0, sizeof(*cs));
        /* OR logic? */
        int has_or = strchr(p, '|') != NULL;
        if (has_or) cs->orlogic = 1;
        /* iterate parts separated by '|' or '+' */
        char *cursor = p;
        while (*cursor && cs->nkeys < MG_CS_MAXKEYS) {
            char *sep = cs->orlogic ? strchr(cursor, '|') : strchr(cursor, '+');
            char seg[64]; int sn = 0;
            while (*cursor && cursor != sep && sn < 60) seg[sn++] = *cursor++;
            seg[sn] = 0;
            if (sep) cursor++;   /* skip separator */
            char *q = seg; while (*q == ' ' || *q == '\t') q++;
            char *qe = q + strlen(q) - 1;
            while (qe >= q && (*qe == ' ' || *qe == '\t')) *qe-- = 0;
            if (!*q) continue;

            /* prefix symbols */
            MgCmdKey ck; memset(&ck, 0, sizeof(ck));
            int greater = 0;
            while (*q) {
                if (*q == '>') { greater = 1; q++; continue; }
                if (*q == '~' || *q == '/') {
                    int is_tilde = (*q == '~');
                    if (is_tilde) ck.tilde = 1; else ck.slash = 1;
                    q++;
                    int n = 0;
                    while (*q >= '0' && *q <= '9') { n = n*10 + (*q - '0'); q++; }
                    if (n > 0) ck.chargetime = n;
                    continue;
                }
                if (*q == '$') { ck.dollar = 1; q++; continue; }
                if (*q == '*') { ck.star = 1; q++; continue; }
                break;
            }
            if (greater) cs->greater = 1;
            /* key */
            char c1 = q[1] ? q[1] : 0;
            MgKey key;
            if (!keychar_to_mgkey(q[0] ? q[0] : ' ', c1, &key)) continue;
            ck.key = key;
            cs->keys[cs->nkeys++] = ck;
        }
        if (cs->nkeys > 0) out->nsteps++;
    }
    if (out->nsteps == 0) return 0;
    auto_greater_expand(out);
    backward_compat(out);
    build_looporder(out);
    return 1;
}

/* compile every command of a char (called after CMD parse) */
void mg_input_compile_char(MgChar *ch) {
    if (!ch || ch->ncommands <= 0) return;
    if (ch->ccmds) free(ch->ccmds);
    ch->ccmds = (MgCompiledCmd*)calloc(ch->ncommands, sizeof(MgCompiledCmd));
    ch->nccmds = 0;
    for (int i = 0; i < ch->ncommands; i++) {
        MgCompiledCmd cc;
        if (compile_command(&ch->commands[i], &cc))
            ch->ccmds[ch->nccmds++] = cc;
    }
    mg_logf("[input] %s: compiled %d/%d commands",
            ch->name, ch->nccmds, ch->ncommands);
}

/* ================================================================== */
/* Per-tick matching (Ikemen Command.Step + CommandList.Step)         */
/* ================================================================== */
static void cmd_clear(MgCmdRun *r, int bufreset) {
    r->curtime = 0;
    if (bufreset) r->curbuftime = 0;
    for (int i = 0; i < MG_CC_MAXSTEPS; i++) { r->completed[i] = 0; r->stepTimers[i] = 0; }
}

static int32_t q_state(const MgInputBuffer *ib, MgKey k, int tilde) {
    return ibuf_state(ib, k, tilde, 0, 0);
}

/* Ikemen: GreaterCheckFail — any other key pressed/released since the
 * previous step completed invalidates the ">" chain. */
static int greater_check_fail(const MgCompiledCmd *c, int i, const MgInputBuffer *ib) {
    int use_lr = 0;
    for (int k = 0; k < c->steps[i].nkeys; k++) {
        switch (c->steps[i].keys[k].key) {
            case MGK_L: case MGK_R:
            case MGK_UL: case MGK_UR: case MGK_DL: case MGK_DR:
                use_lr = 1; break;
            default: break;
        }
    }
    /* returns 1 when key k had an edge that step i does not contain */
#define GCF_KEY(k_) do { \
        if (q_state(ib, (k_), 0) == 1) { \
            int allowed = 0; \
            for (int z_ = 0; z_ < c->steps[i].nkeys; z_++) \
                if (c->steps[i].keys[z_].key == (k_) && !c->steps[i].keys[z_].tilde) { allowed = 1; break; } \
            if (!allowed) return 1; \
        } \
        if (q_state(ib, (k_), 1) == 1) { \
            int allowed = 0; \
            for (int z_ = 0; z_ < c->steps[i].nkeys; z_++) \
                if (c->steps[i].keys[z_].key == (k_) && c->steps[i].keys[z_].tilde) { allowed = 1; break; } \
            if (!allowed) return 1; \
        } \
    } while (0)

    GCF_KEY(MGK_U);
    GCF_KEY(MGK_D);
    if (use_lr) {
        GCF_KEY(MGK_L); GCF_KEY(MGK_R);
        GCF_KEY(MGK_UL); GCF_KEY(MGK_UR); GCF_KEY(MGK_DL); GCF_KEY(MGK_DR);
    } else {
        GCF_KEY(MGK_B); GCF_KEY(MGK_F);
        GCF_KEY(MGK_UB); GCF_KEY(MGK_UF); GCF_KEY(MGK_DB); GCF_KEY(MGK_DF);
    }
    GCF_KEY(MGK_a); GCF_KEY(MGK_b); GCF_KEY(MGK_c);
    GCF_KEY(MGK_x); GCF_KEY(MGK_y); GCF_KEY(MGK_z); GCF_KEY(MGK_s);
#undef GCF_KEY
    return 0;
}

/* Ikemen: Command.Step (hpbuf/pausebuf always off in this engine) */
static void cmd_step(const MgCompiledCmd *c, MgCmdRun *r, const MgInputBuffer *ib) {
    if (r->curbuftime > 0) r->curbuftime--;
    if (c->nsteps == 0) return;

    int anydone = 0;
    for (int i = 0; i < c->nsteps; i++) {
        if (r->completed[i]) {
            r->stepTimers[i]++;
            if (c->maxsteptime > 0 && r->stepTimers[i] > c->maxsteptime) {
                r->completed[i] = 0;
                r->stepTimers[i] = 0;
                continue;   /* don't flag anydone */
            }
            anydone = 1;
        }
    }
    if (anydone) r->curtime++;
    else if (r->curtime > 0) cmd_clear(r, 0);

    for (int li = 0; li < c->nloop; li++) {
        int i = c->looporder[li];
        if (i > 0 && !r->completed[i-1]) continue;

        int matched;
        const MgCmdStep *st = &c->steps[i];
        if (st->orlogic) {
            matched = 0;
            for (int k = 0; k < st->nkeys; k++) {
                const MgCmdKey *ck = &st->keys[k];
                int32_t t = ibuf_state(ib, ck->key, ck->tilde, ck->dollar, ck->star);
                int ok = ck->slash ? (t > 0) : (t == 1);
                if (ok && ck->chargetime > 1 &&
                    ibuf_statecharge(ib, ck->key, ck->tilde, ck->dollar, ck->star) < ck->chargetime)
                    ok = 0;
                if (ok) { matched = 1; break; }
            }
        } else {
            matched = 1;
            for (int k = 0; k < st->nkeys; k++) {
                const MgCmdKey *ck = &st->keys[k];
                int32_t t = ibuf_state(ib, ck->key, ck->tilde, ck->dollar, ck->star);
                int ok = ck->slash ? (t > 0) : (t == 1);
                if (ok && ck->chargetime > 1 &&
                    ibuf_statecharge(ib, ck->key, ck->tilde, ck->dollar, ck->star) < ck->chargetime)
                    ok = 0;
                if (!ok) { matched = 0; break; }
            }
        }

        if (st->greater && i > 0 && c->nsteps >= 2 &&
            r->completed[i-1] && !r->completed[i]) {
            if (greater_check_fail(c, i, ib)) {
                matched = 0;
                r->completed[i-1] = 0;
                r->stepTimers[i-1] = 0;
            }
        }
        if (matched) {
            r->completed[i] = 1;
            r->stepTimers[i] = 0;
            if (i > 0) { r->completed[i-1] = 0; r->stepTimers[i-1] = 0; }
            if (i == 0) r->curtime = 0;
        }
    }

    r->completeframe = (c->nsteps > 0) && r->completed[c->nsteps - 1];
    if (!r->completeframe) {
        if (r->curtime < c->maxtime) return;
    }
    cmd_clear(r, 0);
    if (r->completeframe) {
        if (c->maxbuftime > r->curbuftime) r->curbuftime = c->maxbuftime;
    }
}

/* runtime array for one entity (rebuilt when its char changes) */
static MgCmdRun *rt_of(MgEntity *en) {
    if (!en || !en->ch || en->ch->nccmds <= 0) return NULL;
    int idx = (int)(en - g_entities);
    if (idx < 0 || idx >= MG_MAX_ENTITIES) return NULL;
    if (g_rt[idx] && g_rt_ch[idx] == en->ch && g_rt_n[idx] == en->ch->nccmds)
        return g_rt[idx];
    free(g_rt[idx]);
    g_rt_n[idx] = en->ch->nccmds;
    g_rt[idx] = (MgCmdRun*)calloc(g_rt_n[idx], sizeof(MgCmdRun));
    g_rt_ch[idx] = en->ch;
    return g_rt[idx];
}

/* Ikemen: CommandList.Step — advance every command of this entity */
void mg_commands_step(MgEntity *en) {
    if (!en || !en->ch || en->ch->nccmds <= 0) return;
    MgCmdRun *rt = rt_of(en);
    if (!rt) return;
    const MgInputBuffer *ib = &g_ibuf[en->player_no & 1];

    int ncompleted = 0;
    char completed_names[8][32];
    for (int i = 0; i < en->ch->nccmds; i++) {
        cmd_step(&en->ch->ccmds[i], &rt[i], ib);
        if (rt[i].completeframe) {
            rt[i].completeframe = 0;
            if (ncompleted < 8)
                strncpy(completed_names[ncompleted++], en->ch->commands[i].name, 31);
        }
    }
    /* ClearName: reset in-progress duplicates of completed names
     * (prevents "piano inputs" firing the same move repeatedly) */
    for (int n = 0; n < ncompleted; n++)
        for (int i = 0; i < en->ch->nccmds; i++)
            if (!rt[i].completeframe &&
                rt[i].curtime > 0 &&
                mg_str_ieq(en->ch->commands[i].name, completed_names[n]))
                cmd_clear(&rt[i], 0);
}

/* CNS trigger Command("name"): any same-named command buffered? */
int mg_check_command(MgEntity *en, const char *name) {
    if (!en || !en->ch || en->ch->nccmds <= 0 || !name) return 0;
    MgCmdRun *rt = rt_of(en);
    if (!rt) return 0;
    for (int i = 0; i < en->ch->nccmds; i++)
        if (mg_str_ieq(en->ch->commands[i].name, name) && rt[i].curbuftime > 0)
            return 1;
    return 0;
}

/* reset input state (fight start / re-init): Ikemen BufReset */
void mg_input_reset_all(void) {
    memset(g_ibuf, 0, sizeof(g_ibuf));
    for (int i = 0; i < MG_MAX_ENTITIES; i++) {
        if (g_rt[i]) memset(g_rt[i], 0, sizeof(MgCmdRun) * (g_rt_n[i] > 0 ? g_rt_n[i] : 1));
        /* keep the arrays; char may reload — rt_of() detects the change */
    }
}
