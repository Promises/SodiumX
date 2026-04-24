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

    lv_img_cf_t cf = LV_IMG_CF_TRUE_COLOR;
    if (BACKDROP_BPP * 8 != LV_COLOR_DEPTH)
    {
        cf = (BACKDROP_BPP == 2) ? LV_IMG_CF_RGB565 : LV_IMG_CF_RGBA8888;
    }

    lv_canvas_set_buffer(slot->canvas, img, w, h, cf);

    /* Zoom to cover the screen — GPU does the scaling (bilinear = natural blur) */
    lv_coord_t scr_w = lv_obj_get_width(lv_scr_act());
    lv_coord_t scr_h = lv_obj_get_height(lv_scr_act());
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
    lv_obj_set_style_bg_opa(backdrop_color, 40, LV_PART_MAIN); /* subtle ~15% */
    lv_obj_set_style_bg_grad_color(backdrop_color, EF_PURPLE, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(backdrop_color, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_width(backdrop_color, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(backdrop_color, 0, LV_PART_MAIN);
    lv_obj_clear_flag(backdrop_color, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Slot A canvas */
    slot_a.canvas = lv_canvas_create(parent);
    lv_obj_set_pos(slot_a.canvas, 0, 0);
    lv_obj_set_style_opa(slot_a.canvas, 0, LV_PART_MAIN);
    lv_obj_clear_flag(slot_a.canvas, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_size_mode(slot_a.canvas, LV_IMG_SIZE_MODE_REAL);
    slot_a.mem = NULL;
    slot_a.path[0] = '\0';

    /* Slot B canvas */
    slot_b.canvas = lv_canvas_create(parent);
    lv_obj_set_pos(slot_b.canvas, 0, 0);
    lv_obj_set_style_opa(slot_b.canvas, 0, LV_PART_MAIN);
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
