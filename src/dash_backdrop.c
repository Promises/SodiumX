// SPDX-License-Identifier: MIT
// Backdrop blur — GPU-friendly: decode fanart JPEG, display as lv_canvas
// scaled to fullscreen via lv_img_set_zoom (hardware texture scaling on nv2a).
// Falls back to poster art if no fanart exists.

#include "lithiumx.h"
#include "dash_anim.h"
#include "dash_backdrop.h"

#ifdef NXDK
#define BACKDROP_BPP 2
#else
#define BACKDROP_BPP 4
#endif

/* ── Gaussian blur (box blur x3) on decoded JPEG buffer ── */
#define BLUR_RADIUS  8
#define BLUR_PASSES  3

static void box_blur_rgb565(uint16_t *src, uint16_t *dst, int w, int h, int radius)
{
    /* Horizontal pass */
    for (int y = 0; y < h; y++)
    {
        int r_acc = 0, g_acc = 0, b_acc = 0;
        int count = 0;
        for (int x = 0; x <= radius && x < w; x++)
        {
            uint16_t c = src[y * w + x];
            r_acc += (c >> 11) & 0x1F;
            g_acc += (c >> 5)  & 0x3F;
            b_acc += c & 0x1F;
            count++;
        }
        for (int x = 0; x < w; x++)
        {
            dst[y * w + x] = (uint16_t)(((r_acc / count) << 11) |
                                         ((g_acc / count) << 5)  |
                                          (b_acc / count));
            int right = x + radius + 1;
            if (right < w)
            {
                uint16_t c = src[y * w + right];
                r_acc += (c >> 11) & 0x1F;
                g_acc += (c >> 5)  & 0x3F;
                b_acc += c & 0x1F;
                count++;
            }
            int left = x - radius;
            if (left >= 0)
            {
                uint16_t c = src[y * w + left];
                r_acc -= (c >> 11) & 0x1F;
                g_acc -= (c >> 5)  & 0x3F;
                b_acc -= c & 0x1F;
                count--;
            }
        }
    }
    /* Vertical pass */
    for (int x = 0; x < w; x++)
    {
        int r_acc = 0, g_acc = 0, b_acc = 0;
        int count = 0;
        for (int y = 0; y <= radius && y < h; y++)
        {
            uint16_t c = dst[y * w + x];
            r_acc += (c >> 11) & 0x1F;
            g_acc += (c >> 5)  & 0x3F;
            b_acc += c & 0x1F;
            count++;
        }
        for (int y = 0; y < h; y++)
        {
            src[y * w + x] = (uint16_t)(((r_acc / count) << 11) |
                                         ((g_acc / count) << 5)  |
                                          (b_acc / count));
            int bot = y + radius + 1;
            if (bot < h)
            {
                uint16_t c = dst[bot * w + x];
                r_acc += (c >> 11) & 0x1F;
                g_acc += (c >> 5)  & 0x3F;
                b_acc += c & 0x1F;
                count++;
            }
            int top = y - radius;
            if (top >= 0)
            {
                uint16_t c = dst[top * w + x];
                r_acc -= (c >> 11) & 0x1F;
                g_acc -= (c >> 5)  & 0x3F;
                b_acc -= c & 0x1F;
                count--;
            }
        }
    }
}

static void box_blur_bgra32(uint32_t *src, uint32_t *dst, int w, int h, int radius)
{
    /* Horizontal pass */
    for (int y = 0; y < h; y++)
    {
        int r_acc = 0, g_acc = 0, b_acc = 0;
        int count = 0;
        for (int x = 0; x <= radius && x < w; x++)
        {
            uint32_t c = src[y * w + x];
            b_acc += (c >> 16) & 0xFF;
            g_acc += (c >> 8)  & 0xFF;
            r_acc += c & 0xFF;
            count++;
        }
        for (int x = 0; x < w; x++)
        {
            dst[y * w + x] = (0xFF000000u) |
                              ((b_acc / count) << 16) |
                              ((g_acc / count) << 8)  |
                               (r_acc / count);
            int right = x + radius + 1;
            if (right < w)
            {
                uint32_t c = src[y * w + right];
                b_acc += (c >> 16) & 0xFF;
                g_acc += (c >> 8)  & 0xFF;
                r_acc += c & 0xFF;
                count++;
            }
            int left = x - radius;
            if (left >= 0)
            {
                uint32_t c = src[y * w + left];
                b_acc -= (c >> 16) & 0xFF;
                g_acc -= (c >> 8)  & 0xFF;
                r_acc -= c & 0xFF;
                count--;
            }
        }
    }
    /* Vertical pass */
    for (int x = 0; x < w; x++)
    {
        int r_acc = 0, g_acc = 0, b_acc = 0;
        int count = 0;
        for (int y = 0; y <= radius && y < h; y++)
        {
            uint32_t c = dst[y * w + x];
            b_acc += (c >> 16) & 0xFF;
            g_acc += (c >> 8)  & 0xFF;
            r_acc += c & 0xFF;
            count++;
        }
        for (int y = 0; y < h; y++)
        {
            src[y * w + x] = (0xFF000000u) |
                              ((b_acc / count) << 16) |
                              ((g_acc / count) << 8)  |
                               (r_acc / count);
            int bot = y + radius + 1;
            if (bot < h)
            {
                uint32_t c = dst[bot * w + x];
                b_acc += (c >> 16) & 0xFF;
                g_acc += (c >> 8)  & 0xFF;
                r_acc += c & 0xFF;
                count++;
            }
            int top = y - radius;
            if (top >= 0)
            {
                uint32_t c = dst[top * w + x];
                b_acc -= (c >> 16) & 0xFF;
                g_acc -= (c >> 8)  & 0xFF;
                r_acc -= c & 0xFF;
                count--;
            }
        }
    }
}

static void backdrop_blur(void *pixels, int w, int h)
{
    void *tmp = malloc(w * h * BACKDROP_BPP);
    if (!tmp) return;

    for (int i = 0; i < BLUR_PASSES; i++)
    {
        if (BACKDROP_BPP == 2)
            box_blur_rgb565((uint16_t *)pixels, (uint16_t *)tmp, w, h, BLUR_RADIUS);
        else
            box_blur_bgra32((uint32_t *)pixels, (uint32_t *)tmp, w, h, BLUR_RADIUS);
    }
    free(tmp);
}

/* Two image slots for cross-fade */
typedef struct {
    lv_obj_t *canvas;
    void *mem;          /* malloc'd decoded image buffer */
    void *image;        /* decoded pixel data within mem */
    int w, h;
    char path[DASH_MAX_PATH]; /* path of currently loaded image */
} backdrop_slot_t;

static backdrop_slot_t slot_a, slot_b;
static bool use_slot_a = true;
static lv_obj_t *backdrop_color;   /* Solid color wash fallback */
static lv_obj_t *backdrop_overlay;
static lv_obj_t *backdrop_parent;
static uint32_t backdrop_seq;      /* Sequence counter to discard stale decodes */

/* ── JPEG decode callback — called on decoder thread completion ── */
typedef struct {
    backdrop_slot_t *slot;
    uint32_t seq;  /* sequence number when this decode was requested */
} backdrop_decode_ctx_t;

static void backdrop_decode_cb(void *img, void *mem, int w, int h, void *user_data)
{
    backdrop_decode_ctx_t *ctx = user_data;
    backdrop_slot_t *slot = ctx->slot;
    uint32_t req_seq = ctx->seq;
    free(ctx);

    lvgl_getlock();

    if (img == NULL)
    {
        lvgl_removelock();
        return;
    }

    /* Discard if a newer request has been made since this was queued */
    if (req_seq != backdrop_seq)
    {
        free(mem);
        lvgl_removelock();
        return;
    }

    /* Free previous image if any */
    if (slot->mem)
    {
        free(slot->mem);
        slot->mem = NULL;
    }

    slot->mem = mem;
    slot->image = img;
    slot->w = w;
    slot->h = h;

    /* Gaussian blur approximation (box blur x3) on the small decoded image.
     * Runs once per backdrop change — zero per-frame cost. At 256px source
     * scaled 5x to screen, radius 8 here ≈ Gaussian sigma ~70 on screen. */
    backdrop_blur(img, w, h);

    lv_img_cf_t cf = LV_IMG_CF_TRUE_COLOR;
    if (BACKDROP_BPP * 8 != LV_COLOR_DEPTH)
    {
        cf = (BACKDROP_BPP == 2) ? LV_IMG_CF_RGB565 : LV_IMG_CF_RGBA8888;
    }

    lv_canvas_set_buffer(slot->canvas, img, w, h, cf);

    /* Zoom to cover the screen — GPU does the scaling */
    lv_coord_t scr_w = lv_obj_get_width(lv_scr_act());
    lv_coord_t scr_h = lv_obj_get_height(lv_scr_act());
    if (scr_w <= 0) scr_w = lv_disp_get_hor_res(NULL);
    if (scr_h <= 0) scr_h = lv_disp_get_ver_res(NULL);
    uint16_t zoom_w = scr_w * 256 / w;
    uint16_t zoom_h = scr_h * 256 / h;
    lv_img_set_zoom(slot->canvas, LV_MAX(zoom_w, zoom_h));

    /* Fade in this slot */
    dash_anim_opa(slot->canvas, 0, 130, 600);

    lv_obj_invalidate(slot->canvas);
    lvgl_removelock();
}

/* ── Public API ── */

void dash_backdrop_create(lv_obj_t *parent)
{
    backdrop_parent = parent;
    lv_coord_t scr_w = lv_obj_get_width(parent);
    lv_coord_t scr_h = lv_obj_get_height(parent);

    /* Color wash layer — ambient tint when no fanart, or blended under fanart.
     * Uses a vertical gradient from accent to a complementary hue. */
    backdrop_color = lv_obj_create(parent);
    lv_obj_set_size(backdrop_color, scr_w, scr_h);
    lv_obj_set_pos(backdrop_color, 0, 0);
    lv_obj_set_style_bg_color(backdrop_color, dash_accent_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(backdrop_color, 20, LV_PART_MAIN); /* subtle ~8% */
    lv_obj_set_style_bg_grad_color(backdrop_color, EF_PURPLE, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(backdrop_color, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_width(backdrop_color, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(backdrop_color, 0, LV_PART_MAIN);
    lv_obj_clear_flag(backdrop_color, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Canvas target size — must match LVGL's layout-computed size for the
     * pivot to be correct on the first frame (before layout runs). */
    lv_coord_t canvas_w, canvas_h;
    if (scr_w >= 1280) { canvas_w = 1284; canvas_h = 725; }
    else               { canvas_w = 644;  canvas_h = 485; }

    /* Slot A canvas */
    slot_a.canvas = lv_canvas_create(parent);
    lv_obj_set_pos(slot_a.canvas, 0, 0);
    lv_obj_set_size(slot_a.canvas, canvas_w, canvas_h);
    lv_obj_set_style_opa(slot_a.canvas, 0, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(slot_a.canvas, 0, LV_PART_MAIN);
    lv_img_set_antialias(slot_a.canvas, true);
    lv_obj_clear_flag(slot_a.canvas, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_size_mode(slot_a.canvas, LV_IMG_SIZE_MODE_REAL);
    slot_a.mem = NULL;
    slot_a.path[0] = '\0';

    /* Slot B canvas */
    slot_b.canvas = lv_canvas_create(parent);
    lv_obj_set_pos(slot_b.canvas, 0, 0);
    lv_obj_set_size(slot_b.canvas, canvas_w, canvas_h);
    lv_obj_set_style_opa(slot_b.canvas, 0, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(slot_b.canvas, 0, LV_PART_MAIN);
    lv_img_set_antialias(slot_b.canvas, true);
    lv_obj_clear_flag(slot_b.canvas, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_size_mode(slot_b.canvas, LV_IMG_SIZE_MODE_REAL);
    slot_b.mem = NULL;
    slot_b.path[0] = '\0';

    /* Dark overlay to dim the backdrop */
    backdrop_overlay = lv_obj_create(parent);
    lv_obj_set_size(backdrop_overlay, scr_w, scr_h);
    lv_obj_set_pos(backdrop_overlay, 0, 0);
    lv_obj_set_style_bg_color(backdrop_overlay, EF_BG_DIM, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(backdrop_overlay, 160, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(backdrop_overlay, EF_BG_DIM, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(backdrop_overlay, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_width(backdrop_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(backdrop_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(backdrop_overlay, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

/* Track what's currently displayed so we don't reload the same image */
static char current_backdrop_path[DASH_MAX_PATH];

void dash_backdrop_update(const char *thumb_path)
{
    if (!dash_settings.backdrop_blur) return;

    /* No art path — fade out images, let the color wash show */
    if (!thumb_path || strlen(thumb_path) == 0)
    {
        if (current_backdrop_path[0] != '\0')
        {
            dash_anim_opa(slot_a.canvas, lv_obj_get_style_opa(slot_a.canvas, LV_PART_MAIN), 0, 600);
            dash_anim_opa(slot_b.canvas, lv_obj_get_style_opa(slot_b.canvas, LV_PART_MAIN), 0, 600);
            current_backdrop_path[0] = '\0';
        }
        return;
    }

    /* Build fanart path: same directory as thumbnail, named "fanart.jpg" */
    char fanart_path[DASH_MAX_PATH];
    strncpy(fanart_path, thumb_path, DASH_MAX_PATH - 1);
    fanart_path[DASH_MAX_PATH - 1] = '\0';
    char *sep = strrchr(fanart_path, DASH_PATH_SEPARATOR);
    if (!sep) return;
    strcpy(sep + 1, "fanart.jpg");

    /* Check if fanart exists, fall back to poster */
    const char *img_path = fanart_path;
    DWORD attr = GetFileAttributes(fanart_path);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        img_path = thumb_path;
    }

    /* Skip if already showing this exact image */
    if (strcmp(current_backdrop_path, img_path) == 0) return;
    strncpy(current_backdrop_path, img_path, DASH_MAX_PATH - 1);
    current_backdrop_path[DASH_MAX_PATH - 1] = '\0';

    /* Pick the inactive slot (the one not currently visible) */
    backdrop_slot_t *new_slot = use_slot_a ? &slot_a : &slot_b;
    backdrop_slot_t *old_slot = use_slot_a ? &slot_b : &slot_a;
    use_slot_a = !use_slot_a;

    strncpy(new_slot->path, img_path, DASH_MAX_PATH - 1);

    /* Fade out the old slot immediately */
    dash_anim_opa(old_slot->canvas, lv_obj_get_style_opa(old_slot->canvas, LV_PART_MAIN), 0, 600);

    /* Increment sequence — any in-flight decodes with older seq will be discarded */
    backdrop_seq++;

    /* Queue async JPEG decode with sequence context */
    backdrop_decode_ctx_t *ctx = malloc(sizeof(backdrop_decode_ctx_t));
    ctx->slot = new_slot;
    ctx->seq = backdrop_seq;
    jpeg_decoder_queue(img_path, backdrop_decode_cb, ctx);
}
