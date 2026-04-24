// SPDX-License-Identifier: MIT

#ifndef _DASH_ANIM_H
#define _DASH_ANIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations — full LVGL types come via lithiumx.h in the .c file */
struct _lv_anim_t;
struct _lv_obj_t;

/* Custom cubic-bezier approximation of (.22, .8, .2, 1) — "ease rail" */
int32_t dash_anim_path_ease_rail(const struct _lv_anim_t *a);

/* Convenience animation starters */
void dash_anim_opa(struct _lv_obj_t *obj, uint8_t from, uint8_t to, uint32_t time_ms);
void dash_anim_zoom(struct _lv_obj_t *obj, int32_t from, int32_t to, uint32_t time_ms);
void dash_anim_x(struct _lv_obj_t *obj, int32_t from, int32_t to, uint32_t time_ms);
void dash_anim_y(struct _lv_obj_t *obj, int32_t from, int32_t to, uint32_t time_ms);

/* Overlay entry/exit (opa + translateY) */
void dash_anim_overlay_in(struct _lv_obj_t *obj, uint32_t time_ms);
void dash_anim_overlay_out(struct _lv_obj_t *obj, uint32_t time_ms, void (*ready_cb)(struct _lv_anim_t *));

#ifdef __cplusplus
}
#endif

#endif
