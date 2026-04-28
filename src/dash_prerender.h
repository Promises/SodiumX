// SPDX-License-Identifier: MIT
#ifndef _DASH_PRERENDER_H
#define _DASH_PRERENDER_H

#include "sodiumx.h"

/* Pre-rendered pill (3-slice: left endcap + middle strip + right endcap) */
typedef struct {
    lv_img_dsc_t left;
    lv_img_dsc_t right;
    lv_img_dsc_t mid;     /* 1px wide strip, tiled for variable-width middle */
    int height;
    int radius;
} prerender_pill_t;

/* Render a pill-shaped element (LV_RADIUS_CIRCLE).
 * Height determines radius (height/2). Border is drawn inside. */
void dash_prerender_pill(prerender_pill_t *out, int height,
                         int border_w,
                         lv_color_t bg_color, uint8_t bg_opa,
                         lv_color_t border_color, uint8_t border_opa);

/* Render a fixed-size rounded rectangle. Caller must free result->data. */
void dash_prerender_roundrect(lv_img_dsc_t *out, int w, int h, int radius,
                              int border_w,
                              lv_color_t bg_color, uint8_t bg_opa,
                              lv_color_t border_color, uint8_t border_opa);

/* Render a filled circle. Caller must free result->data. */
void dash_prerender_circle(lv_img_dsc_t *out, int diameter,
                           lv_color_t color, uint8_t opa);

/* Fill a canvas buffer by tiling a 1px-wide middle strip */
void dash_prerender_tile_middle(void *dst_buf, int width, int height,
                                const prerender_pill_t *pill);

#endif
