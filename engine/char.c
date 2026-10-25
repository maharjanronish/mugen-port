/* char.c — character definition loader: .def + .cns/.st/.cmd + constants */
#include "mugen.h"

void mg_char_setconst(MgChar *c, const char *key, double val) {
    for (MgConstEntry *e = c->consts; e; e = e->next) {
        if (mg_str_ieq(e->key, key)) { e->val = val; return; }
    }
    MgConstEntry *e = (MgConstEntry*)calloc(1, sizeof(MgConstEntry));
    strncpy(e->key, key, 47);
    e->val = val;
    e->next = c->consts;
    c->consts = e;
}

double mg_char_const(MgChar *c, const char *key, double defval) {
    if (!c) return defval;
    for (MgConstEntry *e = c->consts; e; e = e->next)
        if (mg_str_ieq(e->key, key)) return e->val;
    /* canonical defaults */
    if (mg_str_ieq(key, "size.ground.front")) return 16;
    if (mg_str_ieq(key, "size.ground.back")) return 12;
    if (mg_str_ieq(key, "data.yaccel")) return 0.44;
    return defval;
}

/* ---------------- CNS parsing (states) ---------------- */
static void parse_states(MgChar *ch, const char *path, MgState **out_states, int *out_n) {
    MgTextIter it;
    mg_text_open(&it, path);
    if (!it.text) { fprintf(stderr, "[cns] cannot open %s\n", path); return; }

    MgState *st = NULL;
    MgStateCtl *ctl = NULL;
    int cap = 128, n = 0;
    MgState *states = (MgState*)malloc(cap * sizeof(MgState));

    while (mg_text_next(&it)) {
        if (it.name[0] == 0) {
            /* new section */
            char sec[128]; strncpy(sec, it.section, 127); sec[127] = 0;
            mg_str_lower(sec);
            if (strncmp(sec, "statedef ", 9) == 0) {
                if (n >= cap) { cap *= 2; states = (MgState*)realloc(states, cap * sizeof(MgState)); }
                st = &states[n++];
                memset(st, 0, sizeof(*st));
                st->statedef = atoi(sec + 9);
                st->anim = -1; st->ctrl = -1; st->poweradd = 0; st->juggle = 0;
                st->vel_x = 0; st->vel_y = 0;
                st->type = 'S'; st->movetype = 'I'; st->physics = 'N';
                ctl = NULL;
            } else if (strncmp(sec, "state ", 6) == 0) {
                if (!st) continue;
                if (st->nctls > 0 && (st->nctls & 7) == 0)
                    st->ctls = (MgStateCtl*)realloc(st->ctls, (st->nctls + 8) * sizeof(MgStateCtl));
                else if (st->nctls == 0)
                    st->ctls = (MgStateCtl*)malloc(8 * sizeof(MgStateCtl));
                ctl = &st->ctls[st->nctls++];
                memset(ctl, 0, sizeof(*ctl));
                ctl->persistent = -1;
                /* [State N, name] — the label after comma is informational */
            } else if (strncmp(sec, "data", 4) == 0 || strncmp(sec, "size", 4) == 0 ||
                       strncmp(sec, "velocity", 8) == 0 || strncmp(sec, "movement", 8) == 0) {
                st = NULL; ctl = NULL;   /* constants section — handled by caller loop below */
            }
            continue;
        }
        if (st && !ctl && it.section[0]) {
            /* Statedef params */
            char nm[64]; strncpy(nm, it.name, 63); nm[63] = 0; mg_str_lower(nm);
            if (mg_str_ieq(nm, "type")) {
                char t = it.value[0] | 32;
                st->type = (t=='s')?'S':(t=='c')?'C':(t=='a')?'A':(t=='l')?'L':'S';
            } else if (mg_str_ieq(nm, "movetype")) {
                char t = it.value[0] | 32;
                st->movetype = (t=='i')?'I':(t=='a')?'A':'H';
            } else if (mg_str_ieq(nm, "physics")) {
                char t = it.value[0] | 32;
                st->physics = (t=='n')?'N':(t=='c')?'C':(t=='a')?'A':(t=='s')?'S':'N';
            } else if (mg_str_ieq(nm, "anim")) st->anim = mg_geti(it.value, -1);
            else if (mg_str_ieq(nm, "velset")) {
                double v[2] = {0,0}; mg_parse_floats(it.value, v, 2);
                st->vel_x = v[0]; st->vel_y = v[1];
            } else if (mg_str_ieq(nm, "ctrl")) st->ctrl = mg_geti(it.value, -1);
            else if (mg_str_ieq(nm, "poweradd")) st->poweradd = mg_geti(it.value, 0);
            else if (mg_str_ieq(nm, "juggle")) st->juggle = mg_geti(it.value, 0);
            else if (mg_str_ieq(nm, "faceclose")) st->faceclose = mg_geti(it.value, 0);
            continue;
        }
        if (st && ctl) {
            /* controller params */
            char nm[64]; strncpy(nm, it.name, 63); nm[63] = 0; mg_str_lower(nm);
            if (mg_str_ieq(nm, "type")) {
                strncpy(ctl->type, it.value, 31);
                mg_str_lower(ctl->type);
            } else if (strncmp(nm, "triggerall", 10) == 0) {
                if (ctl->ntrig_all < 16)
                    ctl->trig_all[ctl->ntrig_all++] = mg_expr_parse(it.value);
            } else if (strncmp(nm, "trigger", 7) == 0) {
                int idx = atoi(nm + 7);
                if (idx >= 1 && idx <= 8) {
                    if (ctl->ntrig_per[idx-1] < 8)
                        ctl->trig[idx-1][ctl->ntrig_per[idx-1]++] = mg_expr_parse(it.value);
                    if (idx > ctl->ntrig_idx) ctl->ntrig_idx = idx;
                }
            } else if (mg_str_ieq(nm, "persistent")) ctl->persistent = mg_geti(it.value, 1);
            else if (mg_str_ieq(nm, "ignorehitpause")) ctl->ignorehitpause = mg_geti(it.value, 0);
            else {
                if (ctl->nparams < MG_MAX_STATE_PARAMS) {
                    ctl->pnames[ctl->nparams] = mg_strdup(nm);
                    ctl->pvals[ctl->nparams] = mg_strdup(it.value);
                    ctl->nparams++;
                }
            }
            continue;
        }
        /* constants: [Data]/[Size]/[Velocity]/[Movement] — captured when section known */
        if (!st) {
            char sec[64]; strncpy(sec, it.section, 63); sec[63] = 0; mg_str_lower(sec);
            if (strncmp(sec, "data", 4) == 0 || strncmp(sec, "size", 4) == 0 ||
                strncmp(sec, "velocity", 8) == 0 || strncmp(sec, "movement", 8) == 0) {
                char key[80];
                snprintf(key, sizeof(key), "%s.%s", sec, it.name);
                mg_str_lower(key);
                double v[3] = {0,0,0};
                int nv = mg_parse_floats(it.value, v, 3);
                mg_char_setconst(ch, key, v[0]);
                /* axis aliases: velocity.foo = x,y → velocity.foo.x / .y */
                if (nv >= 2) {
                    char k2[96];
                    snprintf(k2, sizeof(k2), "%s.y", key);
                    mg_char_setconst(ch, k2, v[1]);
                }
                char kx[96];
                snprintf(kx, sizeof(kx), "%s.x", key);
                mg_char_setconst(ch, kx, v[0]);
                /* jump.neu also serves as canonical velocity.jump.x/.y */
                if (mg_str_ieq(key, "velocity.jump.neu")) {
                    mg_char_setconst(ch, "velocity.jump.x", v[0]);
                    mg_char_setconst(ch, "velocity.jump.y", v[1]);
                }
                if (mg_str_ieq(key, "velocity.airjump.neu")) {
                    mg_char_setconst(ch, "velocity.airjump.x", v[0]);
                    mg_char_setconst(ch, "velocity.airjump.y", v[1]);
                }
            }
        }
    }
    free(it.text);
    /* append to char states */
    if (n > 0) {
        int old = *out_n;
        *out_states = (MgState*)realloc(*out_states, (old + n) * sizeof(MgState));
        memcpy(*out_states + old, states, n * sizeof(MgState));
        *out_n = old + n;
    }
    free(states);
}

/* ---------------- CMD parsing (commands + negative states) ---------------- */
static void parse_cmd(MgChar *ch, const char *path) {
    MgTextIter it;
    mg_text_open(&it, path);
    if (!it.text) { fprintf(stderr, "[cmd] cannot open %s\n", path); return; }

    MgCommand *curcmd = NULL;
    int ccap = 32;
    ch->commands = (MgCommand*)malloc(ccap * sizeof(MgCommand));

    while (mg_text_next(&it)) {
        if (it.name[0] == 0) {
            char sec[128]; strncpy(sec, it.section, 127); sec[127] = 0;
            mg_str_lower(sec);
            if (strncmp(sec, "command", 7) == 0) {
                if (ch->ncommands >= ccap) {
                    ccap *= 2;
                    ch->commands = (MgCommand*)realloc(ch->commands, ccap * sizeof(MgCommand));
                }
                curcmd = &ch->commands[ch->ncommands++];
                memset(curcmd, 0, sizeof(*curcmd));
                curcmd->time = 15; curcmd->buffer = 1;
            } else if (strncmp(sec, "statedef ", 9) == 0 || strncmp(sec, "state ", 6) == 0) {
                curcmd = NULL;
                /* states -1/-2/-3 parsed via same state machinery below */
            }
            continue;
        }
        char secLow[64]; strncpy(secLow, it.section, 63); secLow[63] = 0; mg_str_lower(secLow);
        if (curcmd && strncmp(secLow, "command", 7) == 0) {
            char nm[64]; strncpy(nm, it.name, 63); nm[63] = 0; mg_str_lower(nm);
            if (mg_str_ieq(nm, "name")) strncpy(curcmd->name, it.value, 31);
            else if (mg_str_ieq(nm, "command")) strncpy(curcmd->cmd, it.value, 127);
            else if (mg_str_ieq(nm, "time")) curcmd->time = mg_geti(it.value, 15);
            else if (mg_str_ieq(nm, "buffer.time")) curcmd->buffer = mg_geti(it.value, 1);
        }
    }
    free(it.text);

    /* states in .cmd (negative states) — parse with the generic state parser */
    parse_states(ch, path, &ch->states, &ch->nstates);
}

/* ---------------- char def loading ---------------- */
MgChar *mg_char_load(const char *defpath) {
    MgTextIter it;
    mg_text_open(&it, defpath);
    if (!it.text) { fprintf(stderr, "[char] cannot open %s\n", defpath); return NULL; }

    MgChar *ch = (MgChar*)calloc(1, sizeof(MgChar));
    strncpy(ch->dir, defpath, 500);
    char *slash = strrchr(ch->dir, '/');
    if (slash) *slash = 0;
    mg_path_fix(ch->dir);

    char stcommon[256] = "", st[8][256]; int nst = 0;
    char cnsfile[256] = "", cmdfile[256] = "", sfffile[256] = "", airfile[256] = "", sndfile[256] = "";
    char sprite_key[64] = "sprite";

    while (mg_text_next(&it)) {
        if (it.name[0] == 0) continue;
        if (mg_str_ieq(it.section, "Info")) {
            if (mg_str_ieq(it.name, "name")) strncpy(ch->name, it.value, 127);
            else if (mg_str_ieq(it.name, "displayname")) strncpy(ch->displayname, it.value, 127);
            else if (mg_str_ieq(it.name, "localcoord")) {
                int v[2] = {320, 240};
                mg_parse_ints(it.value, v, 2);
                ch->localcoord_w = v[0]; ch->localcoord_h = v[1];
            }
        } else if (mg_str_ieq(it.section, "Files")) {
            if (mg_str_ieq(it.name, "cmd")) strncpy(cmdfile, it.value, 255);
            else if (mg_str_ieq(it.name, "cns")) strncpy(cnsfile, it.value, 255);
            else if (mg_str_ieq(it.name, "stcommon")) strncpy(stcommon, it.value, 255);
            else if (mg_str_ieq(it.name, "st") || (strncmp(it.name, "st", 2) == 0 && it.name[2] >= '0' && it.name[2] <= '9')) {
                if (nst < 8) strncpy(st[nst++], it.value, 255);
            }
            else if (mg_str_ieq(it.name, "sprite")) strncpy(sfffile, it.value, 255);
            else if (mg_str_ieq(it.name, "anim")) strncpy(airfile, it.value, 255);
            else if (mg_str_ieq(it.name, "sound")) strncpy(sndfile, it.value, 255);
        }
    }
    free(it.text);

    if (!ch->displayname[0]) strncpy(ch->displayname, ch->name, 127);
    if (!ch->localcoord_w) { ch->localcoord_w = 320; ch->localcoord_h = 240; }
    ch->pal_scale = 240.0 / ch->localcoord_h;

    /* defaults */
    mg_char_setconst(ch, "data.life", 1000);
    mg_char_setconst(ch, "data.attack", 100);
    mg_char_setconst(ch, "data.defence", 100);

    /* load files relative to def dir */
    if (cnsfile[0]) {
        char *p = mg_path_join(ch->dir, cnsfile); mg_path_fix(p);
        parse_states(ch, p, &ch->states, &ch->nstates);
        free(p);
    }
    if (stcommon[0]) {
        char *p = mg_path_join(ch->dir, stcommon); mg_path_fix(p);
        if (!mg_file_exists(p)) {
            /* common states live in data/ for the default screenpack */
            free(p);
            p = mg_path_join(g_game.base_dir, "data");
            char *p2 = mg_path_join(p, stcommon);
            free(p);
            p = p2; mg_path_fix(p);
        }
        parse_states(ch, p, &ch->states, &ch->nstates);
        free(p);
    }
    for (int i = 0; i < nst; i++) {
        char *p = mg_path_join(ch->dir, st[i]); mg_path_fix(p);
        parse_states(ch, p, &ch->states, &ch->nstates);
        free(p);
    }
    if (cmdfile[0]) {
        char *p = mg_path_join(ch->dir, cmdfile); mg_path_fix(p);
        parse_cmd(ch, p);
        free(p);
    }
    if (sfffile[0]) {
        char *p = mg_path_join(ch->dir, sfffile); mg_path_fix(p);
        ch->sff = mg_sff_load(p);
        free(p);
    }
    if (airfile[0]) {
        char *p = mg_path_join(ch->dir, airfile); mg_path_fix(p);
        ch->air = mg_air_load(p);
        free(p);
    }
    if (sndfile[0]) {
        char *p = mg_path_join(ch->dir, sndfile); mg_path_fix(p);
        ch->snd = mg_snd_load(p);
        free(p);
    }
    fprintf(stderr, "[char] %s: %d states, %d commands, %d sprites, %d anims\n",
            ch->name, ch->nstates, ch->ncommands,
            ch->sff ? ch->sff->nsprites : 0, ch->air ? ch->air->nanims : 0);
    return ch;
}
