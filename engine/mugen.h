/* mugen.h — MUGEN Android port core (RE-assisted native port of Elecbyte 1.1b1) */
#ifndef MUGEN_H
#define MUGEN_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MG_VERSION "0.1.0-mvp"
#define MG_MAX_PLAYERS 2
#define MG_MAX_ENTITIES 8
#define MG_TICK_RATE 60
#define MG_CMD_MAX 32

/* ---------------- helpers ---------------- */
char *mg_strdup(const char *s);
void mg_str_lower(char *s);
int mg_str_ieq(const char *a, const char *b);
char *mg_trim(char *s);
int mg_file_exists(const char *path);
char *mg_read_file(const char *path, long *out_len);
char *mg_path_join(const char *dir, const char *name);
void mg_path_fix(char *s);
/* portable log: stderr on PC, SDL_Log (-> logcat, tag "SDL/APP") on Android.
 * Defined in util.c for non-Android builds, in platform_sdl.c for Android. */
void mg_logf(const char *fmt, ...);

/* ---------------- SFF ---------------- */
typedef struct {
    int16_t group, number;
    int w, h;
    int16_t axis_x, axis_y;
    uint32_t *rgba;
} MgSprite;

typedef struct {
    MgSprite *sprites; int nsprites;
    uint8_t (*palettes)[1024];
    int npalettes;
} MgSff;

MgSff *mg_sff_load(const char *path);
MgSprite *mg_sff_get(MgSff *sff, int group, int number);
int mg_decode_lz5(const uint8_t *src, int srclen, uint8_t *dst, int dstcap);
int mg_decode_rle8(const uint8_t *src, int srclen, uint8_t *dst, int dstcap);
int mg_decode_rle5(const uint8_t *src, int srclen, uint8_t *dst, int dstcap);
void mg_act_load(const char *path, uint8_t out[768]);

/* ---------------- text config ---------------- */
typedef struct {
    char *text; long len, pos;
    char section[128];
    char name[128];
    char value[512];
} MgTextIter;

void mg_text_open(MgTextIter *it, const char *path);
int  mg_text_next(MgTextIter *it);
int  mg_geti(const char *s, int defval);
double mg_getf(const char *s, double defval);
int  mg_parse_ints(const char *s, int *out, int maxn);
int  mg_parse_floats(const char *s, double *out, int maxn);
char *mg_def_get(const char *text, const char *section, const char *param, char *out, int outlen);

/* ---------------- animations ---------------- */
typedef struct { int32_t x1, y1, x2, y2; } MgClsn;

typedef struct {
    int group, number;
    int16_t xoff, yoff;
    int ticks;
    int flip_h, flip_v;
    int alpha_src, alpha_dst;
    MgClsn clsn1[16]; int nclsn1;
    MgClsn clsn2[16]; int nclsn2;
} MgAnimFrame;

typedef struct {
    int number;
    MgAnimFrame *frames; int nframes;
    MgClsn *dclsn1; int ndclsn1;
    MgClsn *dclsn2; int ndclsn2;
    int loopstart, total_ticks;
} MgAnim;

typedef struct { MgAnim *anims; int nanims; } MgAir;
MgAir *mg_air_load(const char *path);
MgAnim *mg_air_get(MgAir *air, int number);

/* ---------------- sound ---------------- */
typedef struct { int group, number; uint8_t *data; int len; } MgSndEntry;
typedef struct { MgSndEntry *entries; int nentries; } MgSnd;
MgSnd *mg_snd_load(const char *path);
MgSndEntry *mg_snd_get(MgSnd *snd, int group, int number);

/* ---------------- fonts ---------------- */
typedef struct {
    char name[64];
    int size_w, size_h, spacing_x, spacing_y, offset_x, offset_y;
    uint32_t *glyphs[256];
    int gw[256], gh[256];
    int banks[256];
} MgFont;
MgFont *mg_font_load(const char *dir, const char *name);

/* ---------------- expressions ---------------- */
typedef struct MgExpr MgExpr;
#define ET_INT 1
#define ET_FLOAT 2
#define ET_TRIG 3
#define ET_NODE 4
#define ET_STR 5

struct MgExpr {
    int type;
    int64_t ival;
    double fval;
    int trig;
    int op;
    MgExpr *a, *b, *c;
    char sval[48];
};

typedef struct { int is_float; int64_t i; double f; } MgVal;

enum {
    TR_TIME=1, TR_ANIMELEM, TR_ANIMTIME, TR_ANIMELEMTIME, TR_ANIM, TR_CTRL,
    TR_STATENO, TR_STATETYPE, TR_MOVECONTACT, TR_MOVEHIT, TR_MOVEGUARDED,
    TR_ALIVE, TR_LIFE, TR_LIFEMAX, TR_POWER, TR_POWERMAX, TR_POS_X, TR_POS_Y,
    TR_VEL_X, TR_VEL_Y, TR_COMMAND, TR_HITDEFATTR, TR_INGUARDDIST, TR_P2BODYDIST,
    TR_P2DIST_X, TR_P2DIST_Y, TR_P2STATETYPE, TR_P2MOVETYPE, TR_SELFANIMEXIST,
    TR_HITOVER, TR_HITSHAKEOVER, TR_HITFALL, TR_CANRECOVER,
    TR_HITVEL_X, TR_HITVEL_Y, TR_VAR, TR_FVAR, TR_SYSVAR, TR_SYSFVAR,
    TR_ANIMEXIST, TR_NUMTARGET, TR_FRONTEDGEBODYDIST, TR_BACKEDGEBODYDIST,
    TR_FRONTEDGE, TR_BACKEDGE, TR_GROUNDLEVEL, TR_ROUNDSTATE, TR_RANDOM,
    TR_TEAMSIDE, TR_UNIQHITCOUNT, TR_HITCOUNT, TR_GUARDCOUNT, TR_IFELSE,
    TR_CONST, TR_CONST720P, TR_Abs, TR_ceil, TR_floor, TR_min, TR_max, TR_sign,
    TR_GAMETIME, TR_ISHELPER, TR_FACING, TR_PREVSTATENO, TR_PI,
    TR_P2LIFE, TR_P2STATE, TR_P2STATENO, TR_P2CTRL, TR_P2DIST,
    TR_MOVEVICTIM, TR_MOVETYPE_, TR_COMBO, TR_AIRJUGGLE
};

#define OP_NOTEQ 257
#define OP_LE 258
#define OP_GE 259
#define OP_AND 260
#define OP_OR 261
#define OP_XOR 262
#define OP_LOGAND 263
#define OP_LOGOR 264
#define OP_LOGNOT 265
#define OP_POW 266
#define OP_NEG 267
#define OP_BITNOT 268

MgExpr *mg_expr_parse(const char *src);
MgVal mg_expr_eval(MgExpr *e, void *en);
void mg_expr_free(MgExpr *e);

/* ---------------- commands ---------------- */
typedef struct {
    char name[32];
    char cmd[128];
    int time, buffer;
} MgCommand;

/* ---------------- states ---------------- */
#define MG_MAX_STATE_PARAMS 24

typedef struct MgStateCtl {
    char type[32];
    MgExpr *trig_all[16];
    int ntrig_all;
    MgExpr *trig[8][8];      /* [idx][line] — lines within same idx are AND-ed */
    int ntrig_per[8];        /* count per idx */
    int ntrig_idx;           /* highest idx used */
    char *pnames[MG_MAX_STATE_PARAMS];
    char *pvals[MG_MAX_STATE_PARAMS];
    int nparams;
    int persistent, ignorehitpause;
} MgStateCtl;

typedef struct MgState {
    int statedef;
    char type, movetype, physics;
    int anim;
    double vel_x, vel_y;
    int ctrl, poweradd, juggle, faceclose;
    MgStateCtl *ctls; int nctls;
    int persistent_counters[128];  /* rolling per-ctl persistent state */
} MgState;

/* ---------------- character ---------------- */
typedef struct MgConstEntry { char key[48]; double val; struct MgConstEntry *next; } MgConstEntry;

typedef struct MgChar {
    char name[128], displayname[128];
    char dir[512];
    MgSff *sff; MgAir *air; MgSnd *snd;
    MgState *states; int nstates;
    MgCommand *commands; int ncommands;
    MgConstEntry *consts;
    double pal_scale;      /* localcoord scale to 320x240 world */
    int localcoord_w, localcoord_h;
} MgChar;

MgChar *mg_char_load(const char *defpath);
double mg_char_const(MgChar *c, const char *key, double defval);
void mg_char_setconst(MgChar *c, const char *key, double val);

/* ---------------- entity ---------------- */
typedef struct {
    int64_t var[64];
    double fvar[64];
    int64_t sysvar[4];
    double sysfvar[4];

    /* gethitvars */
    struct {
        double xvel, yvel, yaccel;
        int hitshaketime, hittime, slidetime, ctrltime;
        int xoff, yoff;
        int hitcount, fallcount;
        int attr, hitflag, groundtype, airtype, animtype, air_animtype, fall_animtype;
        int airfall, down_recover, down_recover_time;
        double fall_xvel, fall_yvel, fall_yaccel;
        int forceair, fall_fall, fall_recover, fall_recover_time;
        int fall_kill;
        int hitshaketime_left, hittime_left;
        int guardflag_ok;
    } gethit;

    int alive, life, lifemax, power, powermax;
    double pos_x, pos_y, vel_x, vel_y;
    int state_no, prev_state_no, state_time;
    char statetype, movetype, physics;
    int ctrl, ctrl_prev;
    int anim_no, anim_time, anim_elem_index;
    MgAnim *cur_anim;
    int facing;
    int attacking;                 /* active hitdef */
    struct {
        int active, used, attr, attr2;
        char attr_str[32], hitflag_str[16], guardflag_str[16];
        char animtype_str[16], air_animtype_str[16], fall_animtype_str[16];
        int priority, numhits;
        double damage, hitdamage, guarddamage;
        double pausetime, shaketime, guard_pausetime, guard_shaketime;
        double sparkxy_x, sparkxy_y;
        int hit_slidetime, guard_slidetime;
        double hit_velocity_x, hit_velocity_y;
        double guard_velocity_x, guard_velocity_y;
        double ground_velocity_x, ground_velocity_y, ground_cornerpush;
        double air_velocity_x, air_velocity_y, air_cornerpush;
        double air_fall_xvel, air_fall_yvel;
        double down_velocity_x, down_velocity_y, down_cornerpush;
        double airguard_velocity_x, airguard_velocity_y, airguard_cornerpush;
        int power_add, power_add_p2, guard_power_add;
        int hit_sound_group, hit_sound_number;
        int guard_sound_group, guard_sound_number;
        int p1sprpriority, p2sprpriority;
        int p2facing, p1getp2facing;
        int p2stateno, p1stateno, p2getp1state;
        int hitsperframe;
        double hit_damage, guard_damage;
        int juggle;
        int fall_fall, fall_recover, fall_recover_time;
        int already_hit_vic[MG_MAX_ENTITIES];
    } hitdef;
    int hitdef_used;

    int juggle_points;
    int move_contact, move_hit, move_guarded;
    int hit_count, uniq_hit_count, guard_count;
    int special_nowalk, special_nobrake, special_invincible, special_noshadow;
    int special_nostandguard, special_nocrouchguard, special_noairguard, special_nojugglecheck;
    int special_auto_turn_special;
    int pos_freeze;
    int screenbound_x;
    double DefenceMul;
    int sprpriority;
    int airjump_count, airjump_num;
    double airjump_height_used;
    int hitfall, hitshakeover, hitover, canrecover;
    int inguarddist;
    int ctrl_overridden;
    int player_no, teamside;
    int is_cpu;
    int ai_timer, ai_action, ai_action_t;   /* ai_action_t: frames inside current AI action */
    int angle;
    int drawpal_time;
    /* localcoord conversion: char-local units -> stage/world units.
     * set at fight start = world_h / ch->localcoord_h (1.0 for 320x240 chars
     * on a 320x240 stage; 1/3 for a 1280x720 char). All CNS-derived lengths
     * and velocities pass through this so HD chars play correctly. */
    double coord_scale;
    /* input */
    uint32_t buttons, buttons_prev;
    /* command buffer */
    struct { char name[32]; int time; } cmdbuf[MG_CMD_MAX];
    int ncmd;
    char last_cmd[32];
    /* clsn cache for current frame */
    MgClsn clsn1[16]; int nclsn1;
    MgClsn clsn2[16]; int nclsn2;
    int cur_frame_index;
    /* hit pause */
    int hitpause_time;
    int is_helper, helper_id, root_no;
    /* afterimage stub */
    int palfx_time; int palfx_add_r, palfx_add_g, palfx_add_b;
    int remap_pal;
    int lying_down;
    /* sctrl extras */
    int invuln_time;
    int width_edge_add, width_player_add;
    int trans_mode;
    int reversal_flag;
    int combo_count;
    MgChar *ch;
} MgEntity;

/* ---------------- stage ---------------- */
typedef struct MgStageBg {
        char name[32];
        int spr_group, spr_number;
        int64_t group_id;
        int x, y;
        int tile, tilespacing_x, tilespacing_y, tile_x;
        double delta_x, delta_y;
        double velocity_x, velocity_y;
        double xscale, yscale;
        double parallax_scale_top, parallax_scale_bottom;
        int mask;
        int window_x1, window_y1, window_x2, window_y2;
        int layerno;
        double sin_amp_x, sin_amp_y, sin_period_x, sin_period_y;
        double sin_time_x, sin_time_y;
        int anim_no, anim_active, anim_time;
        int visible;
    } MgStageBg;

typedef struct MgStage {
    char name[128];
    char defpath[512];
    MgSff *sff;
    MgAir *air;   /* optional bg animations */
    double localcoord_w, localcoord_h;
    int startx, starty, boundleft, boundright, boundhigh, boundlow;
    int verticalfollow, floortension, tension, tensionhigh, tensionlow;
    int p1startx, p1starty, p1facing, p2startx, p2starty, p2facing;
    int leftbound, rightbound, topbound, botbound;
    int zoffset, zoffsetlink, autoscroll;
    double scale_top, scale_bottom;
    MgStageBg *bgs; int nbgs;
    double camera_x, camera_y;
} MgStage;

MgStage *mg_stage_load(const char *defpath);

/* input buttons bitfield */
enum { BTN_A=1, BTN_B=2, BTN_C=4, BTN_X=8, BTN_Y=16, BTN_Z=32, BTN_S=64, BTN_START=64,
       BTN_U=128, BTN_D=256, BTN_BACK=512, BTN_FWD=1024 };

/* ---------------- engine globals ---------------- */
typedef struct {
    MgChar *chars[64];
    int nchars;
    char char_names[64][128];
    MgStage *stages[16];
    int nstages;
    char stage_names[16][128];
    int game_phase;      /* 0 title, 1 select, 2 fight, 3 victory */
    int menu_index;
    int selected[2];
    int p2_cpu;
    int selected_stage;
    int round_no, rounds_won[2];
    int round_state;
    int round_timer;
    int fight_timer;
    int time_left;
    char announce_text[64];
    int announce_timer;
    double camera_x, camera_y;
    int shake_time, shake_ampl, shake_freq;
    int paused, pause_timer, pause_buffer;
    char base_dir[512];
    MgFont *fonts[16];
    char font_files[16][64];
    struct {
        int p1_life_x, p1_life_y, p1_life_w, p1_life_h, p1_life_range[2];
        int p2_life_x, p2_life_y, p2_life_w, p2_life_h, p2_life_range[2];
        int p1_power_x, p1_power_y, p1_power_w, p1_power_h, p1_power_range[2];
        int p2_power_x, p2_power_y, p2_power_w, p2_power_h, p2_power_range[2];
        int p1_face_x, p1_face_y, p1_face_w, p1_face_h;
        int p2_face_x, p2_face_y, p2_face_w, p2_face_h;
        int timer_x, timer_y, timer_font, timer_scale;
        int name_p1_x, name_p1_y, name_p2_x, name_p2_y, name_font, name_scale;
        uint32_t p1_life_front, p1_life_back, p2_life_front, p2_life_back;
        uint32_t p1_power_front, p1_power_back, p2_power_front, p2_power_back;
        uint32_t name_p1_rgb, name_p2_rgb, timer_rgb;
        int p1_face_spr_group, p1_face_spr_num, p2_face_spr_group, p2_face_spr_num;
        int face_scale;
    } hud;
    int world_w, world_h;
    double world_scale;
    int win_w, win_h;
    int net_mode;   /* 0 none, 1 host, 2 client */
    int running;
    /* system.def basics */
    char title_name[128];
    int select_title;
    uint32_t title_bg_rgb;
    /* fight sounds from fight.def */
    int ko_sound_group, ko_sound_number;
    int fight_sound_group, fight_sound_number;
    int round_sound_group, round_sound_number;
    int win_sound_group, win_sound_number;
    int lose_sound_group, lose_sound_number;
    int cur_round_max;
    int single_mode_rounds;
    int debug_keys;
    int frames_done;
} MgGame;

extern MgGame g_game;
extern MgEntity g_entities[MG_MAX_ENTITIES];
extern int g_nentities;

/* engine API */
void mg_engine_init(const char *gamedir);
void mg_load_fight_def(const char *gamedir);
void mg_load_select(void);
void mg_fight_start(void);
void mg_fight_frame(void);
void mg_fight_render(void);
void mg_select_frame(void);
void mg_select_render(void);
void mg_title_frame(void);
void mg_title_render(void);
void mg_victory_frame(void);
void mg_victory_render(void);
MgState *mg_find_state(MgEntity *en, int state_no);
void mg_change_state(MgEntity *en, int state_no, int anim, int ctrl);
void mg_run_entity(MgEntity *en);
void mg_update_anim(MgEntity *en);
void mg_update_clsn(MgEntity *en);
int  mg_check_command(MgEntity *en, const char *name);
void mg_parse_input_commands(MgEntity *en);
void mg_spawn_hitspark(int x, int y, int guard);
void mg_update_camera(void);
void mg_render_stage(int layer);
void mg_render_entity(MgEntity *en);
void mg_render_hud(void);
double mg_char_const(MgChar *c, const char *key, double defval);

/* platform hooks */
void mg_render_sprite(MgSprite *spr, int dx, int dy, int dw, int dh,
                      int flip_h, int flip_v, int alpha_src, int alpha_dst);
void mg_fill_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void mg_play_wav(uint8_t *data, int len, int channel, int loop);
void mg_play_music(const char *path, int loop);
void mg_stop_channel(int channel);
void mg_render_text(MgFont *f, const char *text, int x, int y, int scale, uint32_t rgb);

#endif

/* stage edge helpers (fight.c) */
double mg_stage_left_edge(void);
double mg_stage_right_edge(void);
double mg_stage_floor(void);
MgEntity *mg_p2_of(MgEntity *en);

/* extra entity fields used by state controllers */
/* (appended to keep header single-pass friendly; see MgEntity in struct def) */
struct MgEntityExtra {
    int invuln_time;
    int width_edge_add, width_player_add;
    int trans_mode;
    int reversal_flag;
};
/* NOTE: the following are accessed via helper macros on MgEntity; defined in fight.c */

/* sctrl helpers implemented in fight.c */
void mg_apply_target_sctrl(MgEntity *en, const char *type, MgStateCtl *c);
void mg_spawn_explod(MgEntity *en, MgStateCtl *c);
void mg_remove_explods(MgEntity *en);
void mg_spawn_dust(MgEntity *en);
void mg_spawn_projectile(MgEntity *en, MgStateCtl *c);
int mg_try_match(MgEntity *en, MgCommand *c);
void mg_input_record(int player, uint32_t buttons);
void mg_platform_vibrate(int ms);

#define MG_DEFAULT_W 1280
#define MG_DEFAULT_H 720
void mg_engine_frame(void);
void mg_engine_render(void);
int mg_engine_running(void);
extern MgGame g_game;   /* defined in state.c */
MgSff *mg_sff_load_font(const char *path);
