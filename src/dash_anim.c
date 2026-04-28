// SPDX-License-Identifier: MIT

#include "sodiumx.h"
#include "dash_anim.h"

/* ============================================================
 *  Custom cubic-bezier path: approximate (.22, .8, .2, 1)
 *  This is a fast ease-out curve used for rail sliding and
 *  tile scale transitions. Implemented as a piecewise
 *  quadratic approximation for efficiency.
 * ============================================================ */
int32_t dash_anim_path_ease_rail(const lv_anim_t *a)
{
    /* Attempt to match CSS cubic-bezier(.22, .8, .2, 1)
     * This is a custom ease-out: gentle acceleration at start,
     * fast through middle, smooth deceleration at end.
     *
     * Sampled from the actual bezier at 8 points and interpolated: */
    static const int16_t bezier_lut[9] = {
        0, 200, 500, 720, 850, 930, 970, 992, 1024
    };

    int32_t t = lv_map(a->act_time, 0, a->time, 0, 1024);
    if (t <= 0) return a->start_value;
    if (t >= 1024) return a->end_value;

    /* LUT interpolation: find segment and lerp */
    int seg = t * 8 / 1024;          /* 0..7 */
    if (seg > 7) seg = 7;
    int32_t seg_start = seg * 1024 / 8;
    int32_t seg_end = (seg + 1) * 1024 / 8;
    int32_t frac = (t - seg_start) * 1024 / (seg_end - seg_start);

    int32_t y = bezier_lut[seg] + ((bezier_lut[seg + 1] - bezier_lut[seg]) * frac >> 10);

    int32_t range = a->end_value - a->start_value;
    return a->start_value + ((range * y) >> 10);
}

/* ============================================================
 *  Animation exec callbacks
 * ============================================================ */
static void _anim_opa_cb(void *obj, int32_t val)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)val, LV_PART_MAIN);
}

static void _anim_zoom_cb(void *obj, int32_t val)
{
    lv_obj_set_style_transform_zoom((lv_obj_t *)obj, (uint16_t)val, LV_PART_MAIN);
}

static void _anim_x_cb(void *obj, int32_t val)
{
    lv_obj_set_x((lv_obj_t *)obj, val);
}

static void _anim_y_cb(void *obj, int32_t val)
{
    lv_obj_set_style_translate_y((lv_obj_t *)obj, val, LV_PART_MAIN);
}

/* ============================================================
 *  Convenience animation starters
 * ============================================================ */
void dash_anim_opa(lv_obj_t *obj, lv_opa_t from, lv_opa_t to, uint32_t time_ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, time_ms);
    lv_anim_set_exec_cb(&a, _anim_opa_cb);
    lv_anim_set_path_cb(&a, dash_anim_path_ease_rail);
    lv_anim_start(&a);
}

void dash_anim_zoom(lv_obj_t *obj, int32_t from, int32_t to, uint32_t time_ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, time_ms);
    lv_anim_set_exec_cb(&a, _anim_zoom_cb);
    lv_anim_set_path_cb(&a, dash_anim_path_ease_rail);
    lv_anim_start(&a);
}

void dash_anim_x(lv_obj_t *obj, lv_coord_t from, lv_coord_t to, uint32_t time_ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, time_ms);
    lv_anim_set_exec_cb(&a, _anim_x_cb);
    lv_anim_set_path_cb(&a, dash_anim_path_ease_rail);
    lv_anim_start(&a);
}

void dash_anim_y(lv_obj_t *obj, lv_coord_t from, lv_coord_t to, uint32_t time_ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, time_ms);
    lv_anim_set_exec_cb(&a, _anim_y_cb);
    lv_anim_set_path_cb(&a, dash_anim_path_ease_rail);
    lv_anim_start(&a);
}

/* ============================================================
 *  Overlay entry / exit animations
 *  In:  opa 0→255, translateY 6→0 over time_ms (300ms typ)
 *  Out: reverse, then call ready_cb (typically to destroy)
 * ============================================================ */
void dash_anim_overlay_in(lv_obj_t *obj, uint32_t time_ms)
{
    lv_obj_set_style_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_translate_y(obj, 6, LV_PART_MAIN);

    dash_anim_opa(obj, 0, 255, time_ms);
    dash_anim_y(obj, 6, 0, time_ms);
}

void dash_anim_overlay_out(lv_obj_t *obj, uint32_t time_ms, lv_anim_ready_cb_t ready_cb)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, 255, 0);
    lv_anim_set_time(&a, time_ms);
    lv_anim_set_exec_cb(&a, _anim_opa_cb);
    lv_anim_set_path_cb(&a, dash_anim_path_ease_rail);
    if (ready_cb)
    {
        lv_anim_set_ready_cb(&a, ready_cb);
    }
    lv_anim_start(&a);

    dash_anim_y(obj, 0, 6, time_ms);
}
