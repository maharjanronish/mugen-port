/* stage.c — stage definition loader */
#include "mugen.h"

MgStage *mg_stage_load(const char *defpath) {
    MgTextIter it;
    mg_text_open(&it, defpath);
    if (!it.text) { fprintf(stderr, "[stage] cannot open %s\n", defpath); return NULL; }

    MgStage *st = (MgStage*)calloc(1, sizeof(MgStage));
    strncpy(st->defpath, defpath, 500);
    char dir[512] = "";
    strncpy(dir, defpath, 500);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0;
    mg_path_fix(dir);

    char sfffile[256] = "";
    int cap = 16;
    st->bgs = (MgStageBg*)malloc(cap * sizeof(MgStageBg));
    MgStageBg *cur = NULL;
    char cursec[64] = "";

    /* defaults */
    st->localcoord_w = 320; st->localcoord_h = 240;
    st->boundleft = -200; st->boundright = 200;
    st->zoffset = 200;
    st->p1startx = -70; st->p2startx = 70;
    st->scale_top = 1; st->scale_bottom = 1;

    while (mg_text_next(&it)) {
        if (it.name[0] == 0) {
            strncpy(cursec, it.section, 63); cursec[63] = 0;
            mg_str_lower(cursec);
            if (strncmp(cursec, "bg ", 3) == 0) {
                if (st->nbgs >= cap) {
                    cap *= 2;
                    st->bgs = (MgStageBg*)realloc(st->bgs, cap * sizeof(MgStageBg));
                }
                cur = &st->bgs[st->nbgs++];
                memset(cur, 0, sizeof(*cur));
                cur->xscale = 1; cur->yscale = 1;
                cur->delta_x = 1; cur->delta_y = 1;
                cur->mask = 1;
                cur->window_x1 = -999999; cur->window_y1 = -999999;
                cur->window_x2 = -999999; cur->window_y2 = -999999;
                cur->layerno = 0;
                cur->visible = 1;
                strncpy(cur->name, it.section + 3, 31);
            } else {
                cur = NULL;
            }
            continue;
        }
        char nm[64]; strncpy(nm, it.name, 63); nm[63] = 0; mg_str_lower(nm);

        if (mg_str_ieq(cursec, "info")) {
            if (mg_str_ieq(nm, "name")) strncpy(st->name, it.value, 127);
            else if (mg_str_ieq(nm, "localcoord")) {
                int v[2] = {320,240}; mg_parse_ints(it.value, v, 2);
                st->localcoord_w = v[0]; st->localcoord_h = v[1];
            }
        }
        else if (mg_str_ieq(cursec, "camera")) {
            if (mg_str_ieq(nm, "startx")) st->startx = mg_geti(it.value, 0);
            else if (mg_str_ieq(nm, "starty")) st->starty = mg_geti(it.value, 0);
            else if (mg_str_ieq(nm, "boundleft")) st->boundleft = mg_geti(it.value, -200);
            else if (mg_str_ieq(nm, "boundright")) st->boundright = mg_geti(it.value, 200);
            else if (mg_str_ieq(nm, "boundhigh")) st->boundhigh = mg_geti(it.value, 0);
            else if (mg_str_ieq(nm, "boundlow")) st->boundlow = mg_geti(it.value, 0);
            else if (mg_str_ieq(nm, "verticalfollow")) st->verticalfollow = mg_geti(it.value, 0);
            else if (mg_str_ieq(nm, "floortension")) st->floortension = mg_geti(it.value, 0);
            else if (mg_str_ieq(nm, "tension")) st->tension = mg_geti(it.value, 50);
        }
        else if (mg_str_ieq(cursec, "playerinfo")) {
            if (mg_str_ieq(nm, "p1startx")) st->p1startx = mg_geti(it.value, -70);
            else if (mg_str_ieq(nm, "p1starty")) st->p1starty = mg_geti(it.value, 0);
            else if (mg_str_ieq(nm, "p1facing")) st->p1facing = mg_geti(it.value, 1);
            else if (mg_str_ieq(nm, "p2startx")) st->p2startx = mg_geti(it.value, 70);
            else if (mg_str_ieq(nm, "p2starty")) st->p2starty = mg_geti(it.value, 0);
            else if (mg_str_ieq(nm, "p2facing")) st->p2facing = mg_geti(it.value, -1);
            else if (mg_str_ieq(nm, "leftbound")) st->leftbound = mg_geti(it.value, -1000);
            else if (mg_str_ieq(nm, "rightbound")) st->rightbound = mg_geti(it.value, 1000);
            else if (mg_str_ieq(nm, "topbound")) st->topbound = mg_geti(it.value, 0);
            else if (mg_str_ieq(nm, "botbound")) st->botbound = mg_geti(it.value, 0);
        }
        else if (mg_str_ieq(cursec, "scaling")) {
            if (mg_str_ieq(nm, "topscale")) st->scale_top = mg_getf(it.value, 1);
            else if (mg_str_ieq(nm, "bottomscale")) st->scale_bottom = mg_getf(it.value, 1);
        }
        else if (mg_str_ieq(cursec, "bound")) {
            if (mg_str_ieq(nm, "screenleft")) {} /* MVP: fixed 15% margin */
            else if (mg_str_ieq(nm, "screenright")) {}
        }
        else if (mg_str_ieq(cursec, "stageinfo")) {
            if (mg_str_ieq(nm, "zoffset")) st->zoffset = mg_geti(it.value, 200);
            else if (mg_str_ieq(nm, "zoffsetlink")) st->zoffsetlink = mg_geti(it.value, 0);
            else if (mg_str_ieq(nm, "autoturn")) st->autoscroll = mg_geti(it.value, 1);
        }
        else if (mg_str_ieq(cursec, "files")) {
            if (mg_str_ieq(nm, "sprite")) strncpy(sfffile, it.value, 255);
        }
        else if (cur && strncmp(cursec, "bg ", 3) == 0) {
            if (mg_str_ieq(nm, "spr")) {
                int v[2] = {0,0};
                mg_parse_ints(it.value, v, 2);
                if (v[0] == -1) { cur->visible = 0; }
                cur->spr_group = v[0]; cur->spr_number = v[1];
            }
            else if (mg_str_ieq(nm, "x")) cur->x = mg_geti(it.value, 0);
            else if (mg_str_ieq(nm, "y")) cur->y = mg_geti(it.value, 0);
            else if (mg_str_ieq(nm, "tile")) {
                int v[2] = {0,0}; mg_parse_ints(it.value, v, 2);
                cur->tile = v[0]; cur->tile_x = v[0];
            }
            else if (mg_str_ieq(nm, "tilespacing")) {
                int v[2] = {0,0}; mg_parse_ints(it.value, v, 2);
                cur->tilespacing_x = v[0]; cur->tilespacing_y = v[1];
            }
            else if (mg_str_ieq(nm, "delta")) {
                double v[2] = {1,1}; mg_parse_floats(it.value, v, 2);
                cur->delta_x = v[0]; cur->delta_y = v[1];
            }
            else if (mg_str_ieq(nm, "xscale")) {
                double v[2] = {1,1}; mg_parse_floats(it.value, v, 2);
                cur->parallax_scale_top = v[0]; cur->parallax_scale_bottom = v[1];
            }
            else if (mg_str_ieq(nm, "yscale")) cur->yscale = mg_getf(it.value, 1);
            else if (mg_str_ieq(nm, "mask")) cur->mask = mg_geti(it.value, 1);
            else if (mg_str_ieq(nm, "layerno")) cur->layerno = mg_geti(it.value, 0);
            else if (mg_str_ieq(nm, "window")) {
                int v[4]; mg_parse_ints(it.value, v, 4);
                cur->window_x1 = v[0]; cur->window_y1 = v[1];
                cur->window_x2 = v[2]; cur->window_y2 = v[3];
            }
            else if (mg_str_ieq(nm, "velocity")) {
                double v[2] = {0,0}; mg_parse_floats(it.value, v, 2);
                cur->velocity_x = v[0]; cur->velocity_y = v[1];
            }
            else if (mg_str_ieq(nm, "sin.x")) {
                double v[4] = {0,0,0,0}; mg_parse_floats(it.value, v, 4);
                cur->sin_amp_x = v[0]; cur->sin_period_x = v[1]; cur->sin_time_x = v[2];
            }
            else if (mg_str_ieq(nm, "sin.y")) {
                double v[4] = {0,0,0,0}; mg_parse_floats(it.value, v, 4);
                cur->sin_amp_y = v[0]; cur->sin_period_y = v[1]; cur->sin_time_y = v[2];
            }
            else if (mg_str_ieq(nm, "animation")) cur->anim_no = mg_geti(it.value, 0);
        }
    }
    free(it.text);

    if (sfffile[0]) {
        char *p = mg_path_join(dir, sfffile);
        mg_path_fix(p);
        st->sff = mg_sff_load(p);
        free(p);
    }
    fprintf(stderr, "[stage] %s: %d bg elems, zoffset=%d\n", st->name, st->nbgs, st->zoffset);
    return st;
}
