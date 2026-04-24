// SPDX-License-Identifier: MIT

#ifndef lv_draw_xgu_H
#define lv_draw_xgu_H

#ifdef __cplusplus
extern "C" {
#endif

#include "src/draw/lv_draw.h"
#include "src/core/lv_disp.h"
#include "src/misc/lv_lru.h"
#include "libs/xgu/xgu.h"
#include "libs/xgu/xgux.h"

typedef struct {
    lv_lru_t *texture_cache;
    uint32_t current_tex;
    uint32_t tex_enabled;
    uint32_t combiner_mode;
} lv_draw_xgu_data_t;

typedef struct {
    lv_draw_ctx_t base_draw;
    lv_draw_xgu_data_t *xgu_data;
} lv_draw_xgu_ctx_t;

typedef struct
{
    void *texture;
    uint32_t tw;
    uint32_t th;
    uint32_t iw;
    uint32_t ih;
    XguTexFormatColor format;
    uint32_t bytes_pp;
} draw_cache_value_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/
#define SKIP_BORDER(dsc) ((dsc)->border_opa <= LV_OPA_MIN || (dsc)->border_width == 0 || (dsc)->border_side == LV_BORDER_SIDE_NONE || (dsc)->border_post)
#define SKIP_SHADOW(dsc) ((dsc)->shadow_width == 0 || (dsc)->shadow_opa <= LV_OPA_MIN || ((dsc)->shadow_width == 1 && (dsc)->shadow_spread <= 0 && (dsc)->shadow_ofs_x == 0 && (dsc)->shadow_ofs_y == 0))
#define SKIP_IMAGE(dsc) ((dsc)->bg_img_src == NULL || (dsc)->bg_img_opa <= LV_OPA_MIN)
#define SKIP_OUTLINE(dsc) ((dsc)->outline_opa <= LV_OPA_MIN || (dsc)->outline_width == 0)

/* Compensate for nv2a linear-space alpha blending vs LVGL's gamma-space
 * software blend. Without this, GPU-blended colors appear ~10-12 brighter
 * per channel. Empirically calibrated +12.5% boost. */
static inline uint8_t xgu_correct_opa(uint8_t opa)
{
    if (opa == 0 || opa >= 250) return opa;
    int corrected = opa + (opa >> 3);
    return (uint8_t)(corrected > 255 ? 255 : corrected);
}

static inline int npot2pot(int num)
{
    if (num != 0)
    {
        num--;
        num |= (num >> 1);  // Or first 2 bits
        num |= (num >> 2);  // Or next 2 bits
        num |= (num >> 4);  // Or next 4 bits
        num |= (num >> 8);  // Or next 8 bits
        num |= (num >> 16); // Or next 16 bits
        num++;
    }
    return num;
}

void lv_draw_xgu_init_ctx(lv_disp_drv_t *drv, lv_draw_ctx_t *draw_ctx);
void lv_draw_xgu_deinit_ctx(lv_disp_drv_t *drv, lv_draw_ctx_t *draw_ctx);

// Shared helpers
void xgu_draw_rect_rounded(const lv_area_t *area, lv_coord_t radius,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a);

//Rect types
void xgu_draw_rect(struct _lv_draw_ctx_t *draw_ctx, const lv_draw_rect_dsc_t *dsc, const lv_area_t *coords);
void xgu_draw_bg(struct _lv_draw_ctx_t *draw_ctx, const lv_draw_rect_dsc_t *draw_dsc, const lv_area_t *coords);
void xgu_draw_polygon(struct _lv_draw_ctx_t *draw_ctx, const lv_draw_rect_dsc_t *draw_dsc,
                  const lv_point_t *points, uint16_t point_cnt);

//Texture tyes
void xgu_draw_letter(struct _lv_draw_ctx_t *draw_ctx, const lv_draw_label_dsc_t *dsc, const lv_point_t *pos_p,
                     uint32_t letter);
void xgu_draw_img_decoded(struct _lv_draw_ctx_t *draw_ctx, const lv_draw_img_dsc_t *dsc,
                          const lv_area_t *src_area, const uint8_t *src_buf, lv_img_cf_t cf);
lv_res_t xgu_draw_img(struct _lv_draw_ctx_t *draw_ctx, const lv_draw_img_dsc_t *dsc,
                      const lv_area_t *src_area, const void *src_buf);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*lv_draw_xgu_H*/