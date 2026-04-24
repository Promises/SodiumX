// SPDX-License-Identifier: MIT

#include "lv_xgu_draw.h"
#include "src/draw/lv_draw.h"
#include "libs/xgu/xgu.h"
#include "libs/xgu/xgux.h"
#include "dash_remote.h"

extern uint32_t *p;

#include <math.h>

/* Number of segments per corner arc. More = smoother, but more GPU vertices. */
#define CORNER_SEGMENTS 8



void draw_rect_simple(const lv_area_t *draw_area)
{
    p = xgu_begin(p, XGU_TRIANGLE_STRIP);
    p = xgu_vertex4f(p, (float)draw_area->x1, (float)draw_area->y1, 1, 1);
    p = xgu_vertex4f(p, (float)draw_area->x2, (float)draw_area->y1, 1, 1);
    p = xgu_vertex4f(p, (float)draw_area->x1, (float)draw_area->y2, 1, 1);
    p = xgu_vertex4f(p, (float)draw_area->x2, (float)draw_area->y2, 1, 1);
    p = xgu_end(p);
}

/* Draw a filled rounded rectangle using a triangle fan.
 * Center vertex + perimeter vertices around the rounded outline. */
void xgu_draw_rect_rounded(const lv_area_t *area, lv_coord_t radius,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    a = xgu_correct_opa(a);
    float x1 = (float)area->x1;
    float y1 = (float)area->y1;
    float x2 = (float)area->x2 + 1;
    float y2 = (float)area->y2 + 1;
    float w = x2 - x1;
    float h = y2 - y1;
    float rad = (float)radius;

    /* Clamp radius to half the smaller dimension */
    if (rad > w / 2) rad = w / 2;
    if (rad > h / 2) rad = h / 2;
    if (rad < 1) rad = 0;

    if (rad == 0)
    {
        /* No radius — simple quad */
        p = xgux_set_color4ub(p, r, g, b, a);
        p = xgu_begin(p, XGU_TRIANGLE_STRIP);
        p = xgu_vertex4f(p, x1, y1, 1, 1);
        p = xgu_vertex4f(p, x2, y1, 1, 1);
        p = xgu_vertex4f(p, x1, y2, 1, 1);
        p = xgu_vertex4f(p, x2, y2, 1, 1);
        p = xgu_end(p);
        return;
    }

    /* Triangle fan from center */
    float cx = (x1 + x2) / 2;
    float cy = (y1 + y2) / 2;

    p = xgux_set_color4ub(p, r, g, b, a);
    p = xgu_begin(p, XGU_TRIANGLE_FAN);

    /* Center vertex */
    p = xgu_vertex4f(p, cx, cy, 1, 1);

    /* Walk the perimeter: top-left corner, top edge, top-right corner, etc. */
    int seg;
    float angle;

    /* Top-left corner (180° to 270°) */
    for (seg = 0; seg <= CORNER_SEGMENTS; seg++)
    {
        angle = 3.14159f + (3.14159f / 2.0f) * ((float)seg / CORNER_SEGMENTS);
        p = xgu_vertex4f(p, x1 + rad + rad * cosf(angle),
                              y1 + rad + rad * sinf(angle), 1, 1);
    }
    /* Top-right corner (270° to 360°) */
    for (seg = 0; seg <= CORNER_SEGMENTS; seg++)
    {
        angle = 3.14159f * 1.5f + (3.14159f / 2.0f) * ((float)seg / CORNER_SEGMENTS);
        p = xgu_vertex4f(p, x2 - rad + rad * cosf(angle),
                              y1 + rad + rad * sinf(angle), 1, 1);
    }
    /* Bottom-right corner (0° to 90°) */
    for (seg = 0; seg <= CORNER_SEGMENTS; seg++)
    {
        angle = (3.14159f / 2.0f) * ((float)seg / CORNER_SEGMENTS);
        p = xgu_vertex4f(p, x2 - rad + rad * cosf(angle),
                              y2 - rad + rad * sinf(angle), 1, 1);
    }
    /* Bottom-left corner (90° to 180°) */
    for (seg = 0; seg <= CORNER_SEGMENTS; seg++)
    {
        angle = 3.14159f / 2.0f + (3.14159f / 2.0f) * ((float)seg / CORNER_SEGMENTS);
        p = xgu_vertex4f(p, x1 + rad + rad * cosf(angle),
                              y2 - rad + rad * sinf(angle), 1, 1);
    }
    /* Close the fan back to the first vertex */
    p = xgu_vertex4f(p, x1, y1 + rad, 1, 1);

    p = xgu_end(p);
}

/* Draw a rounded border by filling the outer rounded rect, then punching
 * out the interior with the stencil buffer. Works reliably on the nv2a. */
static void draw_border_rounded(const lv_area_t *area, lv_coord_t radius,
                                 lv_coord_t bw, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    lv_coord_t inner_radius = radius > bw ? radius - bw : 0;
    lv_area_t inner;
    inner.x1 = area->x1 + bw;
    inner.y1 = area->y1 + bw;
    inner.x2 = area->x2 - bw;
    inner.y2 = area->y2 - bw;

    /* Step 1: write inner rounded rect to stencil (mark area to NOT draw) */
    p = xgu_set_stencil_test_enable(p, true);
    p = xgu_set_stencil_mask(p, 0xFF);
    p = xgu_set_stencil_func(p, XGU_FUNC_ALWAYS);
    p = xgu_set_stencil_func_ref(p, 1);
    p = xgu_set_stencil_op_fail(p, XGU_STENCIL_OP_KEEP);
    p = xgu_set_stencil_op_zfail(p, XGU_STENCIL_OP_KEEP);
    p = xgu_set_stencil_op_zpass(p, XGU_STENCIL_OP_REPLACE);
    p = xgu_set_color_mask(p, 0); /* stencil only */
    xgu_draw_rect_rounded(&inner, inner_radius, 255, 255, 255, 255);

    /* Step 2: draw outer rounded rect only where stencil != 1 (the border ring) */
    p = xgu_set_color_mask(p, (XGU_BLUE | XGU_GREEN | XGU_RED | XGU_ALPHA));
    p = xgu_set_stencil_func(p, XGU_FUNC_NOT_EQUAL);
    p = xgu_set_stencil_func_ref(p, 1);
    p = xgu_set_stencil_func_mask(p, 0xFF);
    p = xgu_set_stencil_op_zpass(p, XGU_STENCIL_OP_KEEP);
    xgu_draw_rect_rounded(area, radius, r, g, b, a);

    /* Step 3: clear stencil */
    p = xgu_set_stencil_func(p, XGU_FUNC_ALWAYS);
    p = xgu_set_stencil_op_zpass(p, XGU_STENCIL_OP_ZERO);
    p = xgu_set_color_mask(p, 0);
    xgu_draw_rect_rounded(area, radius, 0, 0, 0, 255);
    p = xgu_set_color_mask(p, (XGU_BLUE | XGU_GREEN | XGU_RED | XGU_ALPHA));
    p = xgu_set_stencil_test_enable(p, false);
}

static void rect_draw_border(const lv_area_t *draw_area, const lv_draw_rect_dsc_t *dsc)
{
    if (SKIP_BORDER(dsc))
    {
        return;
    }

    if (dsc->radius > 0)
    {
        draw_border_rounded(draw_area, dsc->radius, dsc->border_width,
                            dsc->border_color.ch.red, dsc->border_color.ch.green,
                            dsc->border_color.ch.blue, xgu_correct_opa(dsc->border_opa));
        return;
    }

    /* Square border fallback */
    p = xgux_set_color4ub(p, dsc->border_color.ch.red,
                          dsc->border_color.ch.green,
                          dsc->border_color.ch.blue,
                          dsc->border_opa);

    lv_area_t border_quad;
    if (dsc->border_side & LV_BORDER_SIDE_TOP)
    {
        border_quad.x1 = draw_area->x1;
        border_quad.x2 = draw_area->x2;
        border_quad.y1 = draw_area->y1;
        border_quad.y2 = draw_area->y1 + dsc->border_width;
        draw_rect_simple(&border_quad);
    }
    if (dsc->border_side & LV_BORDER_SIDE_LEFT)
    {
        border_quad.x1 = draw_area->x1;
        border_quad.x2 = draw_area->x1 + dsc->border_width;
        border_quad.y1 = draw_area->y1;
        border_quad.y2 = draw_area->y2;
        draw_rect_simple(&border_quad);
    }
    if (dsc->border_side & LV_BORDER_SIDE_BOTTOM)
    {
        border_quad.x1 = draw_area->x1;
        border_quad.x2 = draw_area->x2;
        border_quad.y1 = draw_area->y2 - dsc->border_width;
        border_quad.y2 = draw_area->y2;
        draw_rect_simple(&border_quad);
    }
    if (dsc->border_side & LV_BORDER_SIDE_RIGHT)
    {
        border_quad.x1 = draw_area->x2 - dsc->border_width;
        border_quad.x2 = draw_area->x2;
        border_quad.y1 = draw_area->y1;
        border_quad.y2 = draw_area->y2;
        draw_rect_simple(&border_quad);
    }
}

static void rect_draw_shadow(lv_draw_xgu_ctx_t *xgu_ctx, const lv_area_t *obj_area,
                              const lv_draw_rect_dsc_t *dsc)
{
    (void)xgu_ctx;
    (void)obj_area;
    (void)dsc;
    /* TODO: implement proper shadow */
}

static void rect_draw_image(const lv_area_t *draw_area, const lv_draw_rect_dsc_t *dsc)
{
    if (SKIP_IMAGE(dsc))
    {
        return;
    }
    DbgPrint("%s - not supported\r\n", __FUNCTION__);
    /*
    const void * bg_img_src;
    const void * bg_img_symbol_font;
    lv_color_t bg_img_recolor;
    lv_opa_t bg_img_opa;
    lv_opa_t bg_img_recolor_opa;
    uint8_t bg_img_tiled;
    */
    // FIXME
}

static void rect_draw_outline(const lv_area_t *obj_area, const lv_draw_rect_dsc_t *dsc)
{
    if (SKIP_OUTLINE(dsc))
        return;

    /* Outline is drawn outside the object, offset by outline_pad */
    lv_coord_t pad = dsc->outline_pad;
    lv_area_t outline_area;
    outline_area.x1 = obj_area->x1 - pad - dsc->outline_width;
    outline_area.y1 = obj_area->y1 - pad - dsc->outline_width;
    outline_area.x2 = obj_area->x2 + pad + dsc->outline_width;
    outline_area.y2 = obj_area->y2 + pad + dsc->outline_width;

    lv_coord_t radius = dsc->radius;
    if (radius > 0) radius += pad + dsc->outline_width;

    uint8_t opa = xgu_correct_opa(dsc->outline_opa);

    draw_border_rounded(&outline_area, radius, dsc->outline_width,
                        dsc->outline_color.ch.red, dsc->outline_color.ch.green,
                        dsc->outline_color.ch.blue, opa);
}

void xgu_draw_rect(lv_draw_ctx_t *draw_ctx, const lv_draw_rect_dsc_t *dsc, const lv_area_t *src_area)
{
    lv_draw_xgu_ctx_t *xgu_ctx = (lv_draw_xgu_ctx_t *)draw_ctx;
    lv_area_t draw_area;
    if (!_lv_area_intersect(&draw_area, src_area, draw_ctx->clip_area))
    {
        return;
    }

    p = pb_begin();

    if (xgu_ctx->xgu_data->combiner_mode != 0)
    {
        #include "lvgl_drivers/video/xgu/notexture.inl"
        xgu_ctx->xgu_data->combiner_mode = 0;
    }

    if (xgu_ctx->xgu_data->tex_enabled == 1)
    {
        p = xgu_set_texture_control0(p, 0, false, 0, 0);
        xgu_ctx->xgu_data->tex_enabled = 0;
    }

    rect_draw_shadow(xgu_ctx, src_area, dsc);
    rect_draw_outline(src_area, dsc);

    if (dsc->bg_opa > LV_OPA_MIN)
    {
        if (dsc->radius > 0)
        {
            /* Rounded rectangle — use triangle fan with arc corners.
             * Gradient is approximated as the average color for rounded rects. */
            lv_color_t c = dsc->bg_color;
            if (dsc->bg_grad.dir != LV_GRAD_DIR_NONE)
            {
                /* Blend the two gradient stops for a rough average */
                c.ch.red = (dsc->bg_grad.stops[0].color.ch.red + dsc->bg_grad.stops[1].color.ch.red) / 2;
                c.ch.green = (dsc->bg_grad.stops[0].color.ch.green + dsc->bg_grad.stops[1].color.ch.green) / 2;
                c.ch.blue = (dsc->bg_grad.stops[0].color.ch.blue + dsc->bg_grad.stops[1].color.ch.blue) / 2;
            }
            /* Use the original unclipped area for proper radius, then let GPU clip */
            xgu_draw_rect_rounded(src_area, dsc->radius, c.ch.red, c.ch.green, c.ch.blue, dsc->bg_opa);
        }
        else
        {
            lv_color_t grad[4];
            if (dsc->bg_grad.dir == LV_GRAD_DIR_VER)
            {
                grad[0] = dsc->bg_grad.stops[0].color;
                grad[1] = dsc->bg_grad.stops[0].color;
                grad[2] = dsc->bg_grad.stops[1].color;
                grad[3] = dsc->bg_grad.stops[1].color;
            }
            else if (dsc->bg_grad.dir == LV_GRAD_DIR_HOR)
            {
                grad[0] = dsc->bg_grad.stops[0].color;
                grad[2] = dsc->bg_grad.stops[0].color;
                grad[1] = dsc->bg_grad.stops[1].color;
                grad[3] = dsc->bg_grad.stops[1].color;
            }
            else
            {
                grad[0] = dsc->bg_color;
                grad[1] = dsc->bg_color;
                grad[2] = dsc->bg_color;
                grad[3] = dsc->bg_color;
            }

            uint8_t opa_c = xgu_correct_opa(dsc->bg_opa);
            p = xgu_begin(p, XGU_TRIANGLE_STRIP);
            p = xgux_set_color4ub(p, grad[0].ch.red, grad[0].ch.green, grad[0].ch.blue, opa_c);
            p = xgu_vertex4f(p, (float)draw_area.x1, (float)draw_area.y1, 1, 1);
            p = xgux_set_color4ub(p, grad[1].ch.red, grad[1].ch.green, grad[1].ch.blue, opa_c);
            p = xgu_vertex4f(p, (float)draw_area.x2 + 1, (float)draw_area.y1, 1, 1);
            p = xgux_set_color4ub(p, grad[2].ch.red, grad[2].ch.green, grad[2].ch.blue, opa_c);
            p = xgu_vertex4f(p, (float)draw_area.x1, (float)draw_area.y2 + 1, 1, 1);
            p = xgux_set_color4ub(p, grad[3].ch.red, grad[3].ch.green, grad[3].ch.blue, opa_c);
            p = xgu_vertex4f(p, (float)draw_area.x2 + 1, (float)draw_area.y2 + 1, 1, 1);
            p = xgu_end(p);
        }
    }

    rect_draw_image(&draw_area, dsc);
    rect_draw_border(src_area, dsc);

    pb_end(p);
}

void xgu_draw_bg(struct _lv_draw_ctx_t *draw_ctx, const lv_draw_rect_dsc_t *draw_dsc, const lv_area_t *src_area)
{
    DbgPrint("%s - not supported\r\n", __FUNCTION__);
    lv_draw_xgu_ctx_t *xgu_ctx = (lv_draw_xgu_ctx_t *)draw_ctx;
    LV_UNUSED(xgu_ctx);
}

void xgu_draw_polygon(struct _lv_draw_ctx_t *draw_ctx, const lv_draw_rect_dsc_t *draw_dsc,
                      const lv_point_t *points, uint16_t point_cnt)
{
    DbgPrint("%s - not supported\r\n", __FUNCTION__);
    lv_draw_xgu_ctx_t *xgu_ctx = (lv_draw_xgu_ctx_t *)draw_ctx;
    LV_UNUSED(xgu_ctx);
}
