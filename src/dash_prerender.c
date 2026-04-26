// SPDX-License-Identifier: MIT
#include "dash_prerender.h"
#include <math.h>

/* BGRA pixel (matches lv_color_t on little-endian) */
#define BGRA(r, g, b, a) ((uint32_t)(b) | ((uint32_t)(g) << 8) | \
                           ((uint32_t)(r) << 16) | ((uint32_t)(a) << 24))

/* Render a single pixel based on distance from circle center */
static uint32_t render_pixel(float dist, float r_inner, float r_outer,
                              lv_color_t bg, uint8_t bg_a,
                              lv_color_t bc, uint8_t bc_a)
{
    if (dist <= r_inner - 0.5f)
    {
        return BGRA(bg.ch.red, bg.ch.green, bg.ch.blue, bg_a);
    }
    else if (dist <= r_inner + 0.5f)
    {
        float fill_cov = LV_CLAMP(0.0f, r_inner + 0.5f - dist, 1.0f);
        float border_cov = 1.0f - fill_cov;
        uint8_t a = (uint8_t)(fill_cov * bg_a + border_cov * bc_a);
        uint8_t r = (uint8_t)(fill_cov * bg.ch.red + border_cov * bc.ch.red);
        uint8_t g = (uint8_t)(fill_cov * bg.ch.green + border_cov * bc.ch.green);
        uint8_t b = (uint8_t)(fill_cov * bg.ch.blue + border_cov * bc.ch.blue);
        return BGRA(r, g, b, a);
    }
    else if (dist <= r_outer - 0.5f)
    {
        return BGRA(bc.ch.red, bc.ch.green, bc.ch.blue, bc_a);
    }
    else if (dist <= r_outer + 0.5f)
    {
        float cov = LV_CLAMP(0.0f, r_outer + 0.5f - dist, 1.0f);
        uint8_t a = (uint8_t)(cov * bc_a);
        return BGRA(bc.ch.red, bc.ch.green, bc.ch.blue, a);
    }
    return 0;
}

static void init_img_dsc(lv_img_dsc_t *dsc, int w, int h, uint8_t *data, size_t size)
{
    dsc->header.always_zero = 0;
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    dsc->data_size = size;
    dsc->data = data;
}

void dash_prerender_pill(prerender_pill_t *out, int height,
                         int border_w,
                         lv_color_t bg_color, uint8_t bg_opa,
                         lv_color_t border_color, uint8_t border_opa)
{
    int radius = height / 2;
    out->height = height;
    out->radius = radius;

    /* Endcaps: radius × height */
    size_t cap_size = radius * height * 4;
    uint8_t *left_buf = lv_mem_alloc(cap_size);
    uint8_t *right_buf = lv_mem_alloc(cap_size);
    lv_memset(left_buf, 0, cap_size);
    lv_memset(right_buf, 0, cap_size);

    uint32_t *left = (uint32_t *)left_buf;
    uint32_t *right = (uint32_t *)right_buf;

    float cy = ((float)height - 1.0f) / 2.0f;
    float r_outer = (float)height / 2.0f;
    float r_inner = r_outer - (float)border_w;

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < radius; x++)
        {
            float dx = (float)x - (float)radius + 0.5f;
            float dy = (float)y - cy;
            float dist = sqrtf(dx * dx + dy * dy);

            uint32_t pixel = render_pixel(dist, r_inner, r_outer,
                                          bg_color, bg_opa, border_color, border_opa);

            left[y * radius + x] = pixel;
            right[y * radius + (radius - 1 - x)] = pixel;
        }
    }

    init_img_dsc(&out->left, radius, height, left_buf, cap_size);
    init_img_dsc(&out->right, radius, height, right_buf, cap_size);

    /* Middle strip: 1px wide */
    size_t mid_size = height * 4;
    uint8_t *mid_buf = lv_mem_alloc(mid_size);
    uint32_t *mid = (uint32_t *)mid_buf;

    for (int y = 0; y < height; y++)
    {
        if (y < border_w || y >= height - border_w)
            mid[y] = BGRA(border_color.ch.red, border_color.ch.green,
                          border_color.ch.blue, border_opa);
        else
            mid[y] = BGRA(bg_color.ch.red, bg_color.ch.green,
                          bg_color.ch.blue, bg_opa);
    }

    init_img_dsc(&out->mid, 1, height, mid_buf, mid_size);
}

void dash_prerender_roundrect(lv_img_dsc_t *out, int w, int h, int radius,
                              int border_w,
                              lv_color_t bg_color, uint8_t bg_opa,
                              lv_color_t border_color, uint8_t border_opa)
{
    size_t buf_size = w * h * 4;
    uint8_t *buf = lv_mem_alloc(buf_size);
    lv_memset(buf, 0, buf_size);
    uint32_t *px = (uint32_t *)buf;

    float cy_top = (float)radius - 0.5f;
    float cy_bot = (float)(h - 1) - (float)radius + 0.5f;
    float cx_left = (float)radius - 0.5f;
    float cx_right = (float)(w - 1) - (float)radius + 0.5f;
    float r_outer = (float)radius;
    float r_inner = r_outer - (float)border_w;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            float dx = 0, dy = 0;
            bool in_corner = false;

            if (x < radius && y < radius)
            {   /* top-left corner */
                dx = (float)x - cx_left;
                dy = (float)y - cy_top;
                in_corner = true;
            }
            else if (x >= w - radius && y < radius)
            {   /* top-right corner */
                dx = (float)x - cx_right;
                dy = (float)y - cy_top;
                in_corner = true;
            }
            else if (x < radius && y >= h - radius)
            {   /* bottom-left corner */
                dx = (float)x - cx_left;
                dy = (float)y - cy_bot;
                in_corner = true;
            }
            else if (x >= w - radius && y >= h - radius)
            {   /* bottom-right corner */
                dx = (float)x - cx_right;
                dy = (float)y - cy_bot;
                in_corner = true;
            }

            uint32_t pixel;
            if (in_corner)
            {
                float dist = sqrtf(dx * dx + dy * dy);
                pixel = render_pixel(dist, r_inner, r_outer,
                                     bg_color, bg_opa, border_color, border_opa);
            }
            else
            {
                /* Straight edge — check if in border region */
                bool in_border = (y < border_w || y >= h - border_w ||
                                  x < border_w || x >= w - border_w);
                if (in_border)
                    pixel = BGRA(border_color.ch.red, border_color.ch.green,
                                 border_color.ch.blue, border_opa);
                else
                    pixel = BGRA(bg_color.ch.red, bg_color.ch.green,
                                 bg_color.ch.blue, bg_opa);
            }

            px[y * w + x] = pixel;
        }
    }

    init_img_dsc(out, w, h, buf, buf_size);
}

void dash_prerender_circle(lv_img_dsc_t *out, int diameter,
                           lv_color_t color, uint8_t opa)
{
    size_t buf_size = diameter * diameter * 4;
    uint8_t *buf = lv_mem_alloc(buf_size);
    lv_memset(buf, 0, buf_size);
    uint32_t *px = (uint32_t *)buf;

    float center = ((float)diameter - 1.0f) / 2.0f;
    float r = (float)diameter / 2.0f;

    for (int y = 0; y < diameter; y++)
    {
        for (int x = 0; x < diameter; x++)
        {
            float dx = (float)x - center;
            float dy = (float)y - center;
            float dist = sqrtf(dx * dx + dy * dy);

            if (dist <= r - 0.5f)
            {
                px[y * diameter + x] = BGRA(color.ch.red, color.ch.green,
                                            color.ch.blue, opa);
            }
            else if (dist <= r + 0.5f)
            {
                float cov = LV_CLAMP(0.0f, r + 0.5f - dist, 1.0f);
                uint8_t a = (uint8_t)(cov * opa);
                px[y * diameter + x] = BGRA(color.ch.red, color.ch.green,
                                            color.ch.blue, a);
            }
        }
    }

    init_img_dsc(out, diameter, diameter, buf, buf_size);
}

void dash_prerender_tile_middle(void *dst_buf, int width, int height,
                                const prerender_pill_t *pill)
{
    const uint32_t *strip = (const uint32_t *)pill->mid.data;
    uint32_t *dst = (uint32_t *)dst_buf;

    for (int y = 0; y < height; y++)
    {
        uint32_t pixel = strip[y];
        for (int x = 0; x < width; x++)
            dst[y * width + x] = pixel;
    }
}

#undef BGRA
