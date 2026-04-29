// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2022 Ryzee119

#include "sodiumx.h"
#include "dash_anim.h"
#include "dash_pill_data.h"
#include <src/misc/lv_lru.h>
#include <src/misc/lv_ll.h>

/* ============================================================
 *  State
 * ============================================================ */
parse_handle_t *parsers[DASH_MAX_PAGES];
static lv_obj_t *rail_wrap;      /* rail viewport (clipped) */
static lv_obj_t *rail;           /* horizontal flex container, translated */
static int page_current;
static int selected_index;       /* index within current page (1-based, 0=null) */
static bool suppress_focus_anim; /* skip animated rail_update during page setup */
static lv_lru_t *thumbnail_cache;
static size_t thumbnail_cache_size = (10 * 1024 * 1024);

/* Indicator dots */
static lv_obj_t *dots_container;
#define MAX_DOTS 32

/* Empty page label — created once, shown/hidden as needed */
static lv_obj_t *empty_page_label;

/* Pre-rendered tile shadows — assembled at runtime from 9-slice pieces */
static lv_img_dsc_t shadow_unfocused_img;
static lv_img_dsc_t shadow_focused_img;
static bool shadows_ready;

static void shadow_assemble(lv_img_dsc_t *out, const shadow_9slice_t *s,
                            int tile_w, int tile_h, uint8_t id)
{
    int abs_ox = s->ofs_x < 0 ? -s->ofs_x : s->ofs_x;
    int abs_oy = s->ofs_y < 0 ? -s->ofs_y : s->ofs_y;
    int img_w = tile_w + 2 * s->pad + abs_ox;
    int img_h = tile_h + 2 * s->pad + abs_oy;

    size_t buf_size = img_w * img_h * 4;
    uint8_t *buf = lv_mem_alloc(buf_size);
    if (!buf) return;
    lv_memset(buf, 0, buf_size);

    int cl = s->corner_l;
    int cr = s->corner_r;
    int ct = s->corner_top_h;
    int cb = s->corner_bot_h;
    int mid_w = img_w - cl - cr;
    int mid_h = img_h - ct - cb;

    #define BLIT(src_dsc, dx, dy) do { \
        const uint8_t *sd = (src_dsc)->data; \
        int sw = (src_dsc)->header.w, sh = (src_dsc)->header.h; \
        for (int _y = 0; _y < sh; _y++) { \
            int dst_row = (dy) + _y; \
            if (dst_row < 0 || dst_row >= img_h) continue; \
            for (int _x = 0; _x < sw; _x++) { \
                int dst_col = (dx) + _x; \
                if (dst_col < 0 || dst_col >= img_w) continue; \
                int si = (_y * sw + _x) * 4; \
                int di = (dst_row * img_w + dst_col) * 4; \
                buf[di] = sd[si]; buf[di+1] = sd[si+1]; \
                buf[di+2] = sd[si+2]; buf[di+3] = sd[si+3]; \
            } \
        } \
    } while(0)

    #define TILE_H_STRIP(src_dsc, dx, dy, w) do { \
        const uint8_t *sd = (src_dsc)->data; \
        int sh = (src_dsc)->header.h; \
        for (int _y = 0; _y < sh; _y++) { \
            int dst_row = (dy) + _y; \
            if (dst_row < 0 || dst_row >= img_h) continue; \
            int si = _y * 4; \
            for (int _x = 0; _x < (w); _x++) { \
                int di = (dst_row * img_w + (dx) + _x) * 4; \
                buf[di] = sd[si]; buf[di+1] = sd[si+1]; \
                buf[di+2] = sd[si+2]; buf[di+3] = sd[si+3]; \
            } \
        } \
    } while(0)

    #define TILE_V_STRIP(src_dsc, dx, dy, h) do { \
        const uint8_t *sd = (src_dsc)->data; \
        int sw = (src_dsc)->header.w; \
        for (int _y = 0; _y < (h); _y++) { \
            int dst_row = (dy) + _y; \
            if (dst_row < 0 || dst_row >= img_h) continue; \
            for (int _x = 0; _x < sw; _x++) { \
                int di = (dst_row * img_w + (dx) + _x) * 4; \
                buf[di] = sd[_x * 4]; buf[di+1] = sd[_x * 4 + 1]; \
                buf[di+2] = sd[_x * 4 + 2]; buf[di+3] = sd[_x * 4 + 3]; \
            } \
        } \
    } while(0)

    BLIT(s->tl, 0, 0);
    BLIT(s->tr, img_w - cr, 0);
    BLIT(s->bl, 0, img_h - cb);
    BLIT(s->br, img_w - cr, img_h - cb);

    if (mid_w > 0) {
        TILE_H_STRIP(s->t, cl, 0, mid_w);
        TILE_H_STRIP(s->b, cl, img_h - cb, mid_w);
    }
    if (mid_h > 0) {
        TILE_V_STRIP(s->l, 0, ct, mid_h);
        TILE_V_STRIP(s->r, img_w - cr, ct, mid_h);
    }

    if (mid_w > 0 && mid_h > 0) {
        const uint8_t *cd = s->c->data;
        for (int _y = 0; _y < mid_h; _y++) {
            int dst_row = ct + _y;
            for (int _x = 0; _x < mid_w; _x++) {
                int di = (dst_row * img_w + cl + _x) * 4;
                buf[di] = cd[0]; buf[di+1] = cd[1];
                buf[di+2] = cd[2]; buf[di+3] = cd[3];
            }
        }
    }

    #undef BLIT
    #undef TILE_H_STRIP
    #undef TILE_V_STRIP

    /* XGU texture cache fix: the cache hashes the first 64 bytes of image data.
     * All-zero (transparent) data produces key=0 which breaks caching.
     * Write unique values per shadow using id to differentiate. */
    for (int i = 0; i < 16 && i < img_w; i++)
        buf[i * 4 + 3] = (uint8_t)(i + 1 + id * 17);  /* unique alpha per shadow */

    out->header.always_zero = 0;
    out->header.w = img_w;
    out->header.h = img_h;
    out->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    out->data_size = buf_size;
    out->data = buf;
}

static void shadows_init(void)
{
    if (shadows_ready) return;
    shadows_ready = true;
    shadow_assemble(&shadow_unfocused_img, &shadow_unfocused, DASH_TILE_W, DASH_TILE_H, 0);
    shadow_assemble(&shadow_focused_img, &shadow_focused, DASH_TILE_W, DASH_TILE_H, 1);
}

/* Zoom values (256 = 1.0x) */
/* Tile size scaling factors (percent of DASH_TILE_W/H) */
#define SIZE_PCT_DEFAULT   75
#define SIZE_PCT_ADJACENT  85
#define SIZE_PCT_SELECTED 100

#define OPA_DEFAULT    200  /* ~78% */
#define OPA_ADJACENT   235  /* ~92% */
#define OPA_SELECTED   255  /* 100% */
#define RAIL_ANIM_MS   550
#define ART_ZOOM_MS    700

#ifdef NXDK
#define JPEG_BPP (2)
#else
#define JPEG_BPP (4)
#endif

typedef struct
{
    lv_obj_t *image_container;
} jpeg_ll_value_t;
static lv_ll_t jpeg_decomp_list;

/* ============================================================
 *  Tab ↔ page sync
 * ============================================================ */
/* Map TOML page name to tab index: Home=0, Recently Played=1, Apps=2, Files=3, System=4 */
static int tab_index_for_page(const char *page_title)
{
    if (strcmp(page_title, "Recent") == 0)       return 0;
    if (strcmp(page_title, "Applications") == 0)  return 2;
    if (strcmp(page_title, "Homebrew") == 0)      return 2;
    if (strcmp(page_title, "Apps") == 0)          return 2;
    return 1; /* "Games" and anything else → Games tab */
}

static void dash_scroller_sync_tab(void)
{
    if (parsers[page_current])
    {
        int tab = tab_index_for_page(parsers[page_current]->page_title);
        dash_set_tab(tab);
    }
}

/* ============================================================
 *  Slot-based positioning
 *  7 predefined screen positions. Slot 3 = center.
 *  Tiles animate into these slots based on their distance
 *  from the selected index. Slots beyond 0/6 are off-screen.
 * ============================================================ */
#define NUM_SLOTS 7
#define CENTER_SLOT 3

/* Slot X positions (left edge of tile) for 1280px screen.
 * Computed so slot 3 centers a DASH_TILE_W tile at screen center. */
static lv_coord_t slot_positions[NUM_SLOTS];
static bool slots_initialized = false;

static void slots_init(void)
{
    if (slots_initialized) return;
    lv_coord_t scr_w = lv_obj_get_width(lv_scr_act());
    lv_coord_t base_w = DASH_TILE_W;
    lv_coord_t gap = DASH_TILE_GAP;

    /* Center slot: tile centered on screen */
    lv_coord_t center_x = scr_w / 2 - base_w / 2;
    slot_positions[CENTER_SLOT] = center_x;

    /* Spread outward from center. Each slot is base_w + gap apart. */
    for (int i = 1; i <= CENTER_SLOT; i++)
    {
        slot_positions[CENTER_SLOT - i] = center_x - i * (base_w + gap);
        slot_positions[CENTER_SLOT + i] = center_x + i * (base_w + gap);
    }
    slots_initialized = true;
}

/* Get the target X position for a tile at `index` when `sel` is selected.
 * Returns the screen X for the tile's left edge. Off-screen tiles get
 * pushed further out to hide smoothly. */
static lv_coord_t tile_target_x(int index, int sel)
{
    int slot = CENTER_SLOT + (index - sel);

    if (slot < 0)
        return slot_positions[0] - (-slot) * (DASH_TILE_W + DASH_TILE_GAP);
    if (slot >= NUM_SLOTS)
        return slot_positions[NUM_SLOTS - 1] + (slot - NUM_SLOTS + 1) * (DASH_TILE_W + DASH_TILE_GAP);

    return slot_positions[slot];
}

/* Tile position/size animation callbacks */
static void _anim_width_cb(void *obj, int32_t val) { lv_obj_set_width((lv_obj_t *)obj, val); }
static void _anim_height_cb(void *obj, int32_t val) { lv_obj_set_height((lv_obj_t *)obj, val); }
static void _anim_tile_x_cb(void *obj, int32_t val) { lv_obj_set_x((lv_obj_t *)obj, val); }
static void _anim_tile_y_cb(void *obj, int32_t val) { lv_obj_set_y((lv_obj_t *)obj, val); }
static void _anim_art_zoom_cb(void *obj, int32_t val) { lv_img_set_zoom((lv_obj_t *)obj, (uint16_t)val); }

/* Apply position, size, opa, and focus ring to a tile based on its slot */
static void tile_apply_state(lv_obj_t *tile, int index, int sel, bool animate)
{
    int delta = (index > sel) ? (index - sel) : (sel - index);
    int size_pct, opa;

    if (delta == 0)      { size_pct = SIZE_PCT_SELECTED; opa = OPA_SELECTED; }
    else if (delta == 1) { size_pct = SIZE_PCT_ADJACENT; opa = OPA_ADJACENT; }
    else                 { size_pct = SIZE_PCT_DEFAULT;   opa = OPA_DEFAULT;  }

    lv_coord_t target_w = DASH_TILE_W * size_pct / 100;
    lv_coord_t target_h = DASH_TILE_H * size_pct / 100;

    /* Target X from slot system — position tile so its center aligns with slot center */
    lv_coord_t slot_x = tile_target_x(index, sel);
    lv_coord_t target_x = slot_x + (DASH_TILE_W - target_w) / 2;

    /* Vertical center in the rail viewport */
    lv_coord_t rail_h = rail_wrap ? lv_obj_get_height(rail_wrap) : DASH_TILE_H;
    lv_coord_t target_y = (rail_h - target_h) / 2;

    /* Animate boxart canvas zoom to match tile size */
    title_t *td = tile->user_data;
    uint16_t target_art_zoom = 256;
    if (td && td->jpg_info && td->jpg_info->canvas && td->jpg_info->w > 0)
    {
        uint16_t zoom_w = target_w * 256 / td->jpg_info->w;
        uint16_t zoom_h = target_h * 256 / td->jpg_info->h;
        target_art_zoom = LV_MIN(zoom_w, zoom_h);
    }

    if (animate)
    {
        /* Animate position */
        lv_anim_t ax;
        lv_anim_init(&ax);
        lv_anim_set_var(&ax, tile);
        lv_anim_set_values(&ax, lv_obj_get_x(tile), target_x);
        lv_anim_set_time(&ax, RAIL_ANIM_MS);
        lv_anim_set_exec_cb(&ax, _anim_tile_x_cb);
        lv_anim_set_path_cb(&ax, dash_anim_path_ease_rail);
        lv_anim_start(&ax);

        lv_anim_t ay;
        lv_anim_init(&ay);
        lv_anim_set_var(&ay, tile);
        lv_anim_set_values(&ay, lv_obj_get_y(tile), target_y);
        lv_anim_set_time(&ay, RAIL_ANIM_MS);
        lv_anim_set_exec_cb(&ay, _anim_tile_y_cb);
        lv_anim_set_path_cb(&ay, dash_anim_path_ease_rail);
        lv_anim_start(&ay);

        /* Animate size */
        lv_anim_t aw, ah;
        lv_anim_init(&aw);
        lv_anim_set_var(&aw, tile);
        lv_anim_set_values(&aw, lv_obj_get_width(tile), target_w);
        lv_anim_set_time(&aw, RAIL_ANIM_MS);
        lv_anim_set_exec_cb(&aw, _anim_width_cb);
        lv_anim_set_path_cb(&aw, dash_anim_path_ease_rail);
        lv_anim_start(&aw);

        lv_anim_init(&ah);
        lv_anim_set_var(&ah, tile);
        lv_anim_set_values(&ah, lv_obj_get_height(tile), target_h);
        lv_anim_set_time(&ah, RAIL_ANIM_MS);
        lv_anim_set_exec_cb(&ah, _anim_height_cb);
        lv_anim_set_path_cb(&ah, dash_anim_path_ease_rail);
        lv_anim_start(&ah);

        dash_anim_opa(tile, lv_obj_get_style_opa(tile, LV_PART_MAIN), opa, RAIL_ANIM_MS);

        /* Animate art zoom in sync with tile size */
        if (td && td->jpg_info && td->jpg_info->canvas)
        {
            lv_anim_t az;
            lv_anim_init(&az);
            lv_anim_set_var(&az, td->jpg_info->canvas);
            lv_anim_set_values(&az, lv_img_get_zoom(td->jpg_info->canvas), target_art_zoom);
            lv_anim_set_time(&az, RAIL_ANIM_MS);
            lv_anim_set_exec_cb(&az, _anim_art_zoom_cb);
            lv_anim_set_path_cb(&az, dash_anim_path_ease_rail);
            lv_anim_start(&az);
        }
    }
    else
    {
        /* Cancel in-flight animations so they don't overwrite snapped values */
        lv_anim_del(tile, NULL);
        if (td && td->jpg_info && td->jpg_info->canvas)
            lv_anim_del(td->jpg_info->canvas, NULL);

        lv_obj_set_pos(tile, target_x, target_y);
        lv_obj_set_size(tile, target_w, target_h);
        lv_obj_set_style_opa(tile, opa, LV_PART_MAIN);
        if (td && td->jpg_info && td->jpg_info->canvas)
        {
            lv_img_set_zoom(td->jpg_info->canvas, target_art_zoom);
        }
    }

    /* Focus ring — only update when the tile's focused state changes.
     * border_width differs between selected (3) and unselected (1),
     * so we use it to detect whether styles need updating. */
    bool is_focused = (delta == 0);
    bool was_focused = (lv_obj_get_style_border_width(tile, LV_PART_MAIN) == 3);
    if (is_focused != was_focused)
    {
        if (is_focused)
        {
            lv_obj_set_style_bg_color(tile, dash_accent_color, LV_PART_MAIN);
            lv_obj_set_style_border_width(tile, 3, LV_PART_MAIN);
            lv_obj_set_style_border_color(tile, dash_accent_color, LV_PART_MAIN);
            lv_obj_set_style_border_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
        }
        else
        {
            lv_obj_set_style_bg_color(tile, EF_BG2, LV_PART_MAIN);
            lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(tile, EF_FG, LV_PART_MAIN);
            lv_obj_set_style_border_opa(tile, 51, LV_PART_MAIN);
        }
    }

    /* Position and scale pre-rendered shadow behind tile.
     * lv_img zooms from center pivot, so we position center-on-center. */
    if (td && td->shadow_img)
    {
        const lv_img_dsc_t *src = is_focused ? &shadow_focused_img : &shadow_unfocused_img;
        int img_w = src->header.w;
        int img_h = src->header.h;
        uint16_t shd_zoom = (uint16_t)(size_pct * 256 / 100);

        lv_coord_t tile_cx = target_x + target_w / 2;
        lv_coord_t tile_cy = target_y + target_h / 2;
        lv_coord_t shd_x = tile_cx - img_w / 2;
        lv_coord_t shd_y = tile_cy - img_h / 2;

        lv_img_set_src(td->shadow_img, src);

        if (!is_focused) {
            lv_obj_add_flag(td->shadow_img, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(td->shadow_img, LV_OBJ_FLAG_HIDDEN);
        }

        if (animate)
        {
            lv_anim_t sx;
            lv_anim_init(&sx);
            lv_anim_set_var(&sx, td->shadow_img);
            lv_anim_set_values(&sx, lv_obj_get_x(td->shadow_img), shd_x);
            lv_anim_set_time(&sx, RAIL_ANIM_MS);
            lv_anim_set_exec_cb(&sx, _anim_tile_x_cb);
            lv_anim_set_path_cb(&sx, dash_anim_path_ease_rail);
            lv_anim_start(&sx);

            lv_anim_t sy;
            lv_anim_init(&sy);
            lv_anim_set_var(&sy, td->shadow_img);
            lv_anim_set_values(&sy, lv_obj_get_y(td->shadow_img), shd_y);
            lv_anim_set_time(&sy, RAIL_ANIM_MS);
            lv_anim_set_exec_cb(&sy, _anim_tile_y_cb);
            lv_anim_set_path_cb(&sy, dash_anim_path_ease_rail);
            lv_anim_start(&sy);

            lv_anim_t sz;
            lv_anim_init(&sz);
            lv_anim_set_var(&sz, td->shadow_img);
            lv_anim_set_values(&sz, lv_img_get_zoom(td->shadow_img), shd_zoom);
            lv_anim_set_time(&sz, RAIL_ANIM_MS);
            lv_anim_set_exec_cb(&sz, _anim_art_zoom_cb);
            lv_anim_set_path_cb(&sz, dash_anim_path_ease_rail);
            lv_anim_start(&sz);

            dash_anim_opa(td->shadow_img,
                          lv_obj_get_style_opa(td->shadow_img, LV_PART_MAIN),
                          opa, RAIL_ANIM_MS);
        }
        else
        {
            lv_anim_del(td->shadow_img, NULL);
            lv_obj_set_pos(td->shadow_img, shd_x, shd_y);
            lv_img_set_zoom(td->shadow_img, shd_zoom);
            lv_obj_set_style_opa(td->shadow_img, opa, LV_PART_MAIN);
        }
    }
}

/* ============================================================
 *  JPEG thumbnail pipeline (preserved from original)
 * ============================================================ */
static void jpg_decompression_complete_cb(void *img, void *mem, int w, int h, void *user_data)
{
    lv_obj_t *image_container = user_data;
    title_t *t = image_container->user_data;

    lvgl_getlock();

    if (img == NULL)
    {
        t->jpg_info->decomp_handle = NULL;
        lvgl_removelock();
        return;
    }

    t->jpg_info->mem = mem;
    t->jpg_info->image = img;
    t->jpg_info->w = w;
    t->jpg_info->h = h;
    t->jpg_info->decomp_handle = NULL;

    t->jpg_info->canvas = lv_canvas_create(image_container);

    lv_img_cf_t cf = LV_IMG_CF_TRUE_COLOR;
    assert(JPEG_BPP == 2 || JPEG_BPP == 4);
    if (JPEG_BPP * 8 != LV_COLOR_DEPTH)
    {
        cf = (JPEG_BPP == 2) ? LV_IMG_CF_RGB565 : LV_IMG_CF_RGBA8888;
    }

    lv_canvas_set_buffer(t->jpg_info->canvas, img, w, h, cf);
    lv_img_set_size_mode(t->jpg_info->canvas, LV_IMG_SIZE_MODE_REAL);

    /* Zoom to cover the tile (like CSS background-size: cover).
     * Always use the base tile dimensions — the tile may not have been
     * laid out yet on initial load, so lv_obj_get_width could return 0
     * or a stale value. The focus animation scales the tile from here. */
    uint16_t zoom_w = DASH_TILE_W * 256 / w;
    uint16_t zoom_h = DASH_TILE_H * 256 / h;
    lv_img_set_zoom(t->jpg_info->canvas, LV_MIN(zoom_w, zoom_h));
    lv_obj_mark_layout_as_dirty(t->jpg_info->canvas);

    lv_lru_set(thumbnail_cache, &image_container, sizeof(lv_obj_t *), t, w * h * JPEG_BPP);
    dash_perf_set_thumb_cache(0, /* count not tracked by lv_lru */
                              thumbnail_cache->total_memory - thumbnail_cache->free_memory);
    lvgl_removelock();
}

static void update_thumbnail_callback(lv_event_t *event)
{
    lv_obj_t *image_container = lv_event_get_target(event);
    title_t *t = image_container->user_data;

    if (t->jpg_info == NULL) return;

    if (t->jpg_info->decomp_handle == NULL && t->jpg_info->mem == NULL)
    {
        t->jpg_info->decomp_handle = jpeg_decoder_queue(t->jpg_info->thumb_path,
                                                        jpg_decompression_complete_cb, image_container);
        if (t->jpg_info->decomp_handle)
        {
            jpeg_ll_value_t *n = _lv_ll_ins_tail(&jpeg_decomp_list);
            n->image_container = image_container;
        }
    }

    if (t->jpg_info->mem)
    {
        lv_obj_t *obj;
        lv_lru_get(thumbnail_cache, &image_container, sizeof(lv_obj_t *), (void **)&obj);
    }
}

/* ============================================================
 *  Selection / navigation callbacks
 * ============================================================ */
static int get_launch_path_callback(void *param, int argc, char **argv, char **azColName)
{
    (void)param; (void)azColName; (void)argc;
    assert(argc == 1);
    strncpy(dash_launch_path, argv[0], DASH_MAX_PATH);
    return 0;
}

/* Fetch developer/year for the meta subtitle */
static char meta_sub_buf[256];
static int meta_info_callback(void *param, int argc, char **argv, char **azColName)
{
    (void)param; (void)azColName;
    /* SELECT * returns all columns — find developer and release_date by name */
    const char *developer = NULL;
    const char *release_date = NULL;
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(azColName[i], SQL_TITLE_DEVELOPER) == 0 && argv[i])
            developer = argv[i];
        if (strcmp(azColName[i], SQL_TITLE_RELEASE_DATE) == 0 && argv[i])
            release_date = argv[i];
    }
    /* Build "Developer · Year · Genre · Size" string */
    const char *genre = NULL;
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(azColName[i], "page") == 0 && argv[i])
            genre = argv[i];
    }
    char year[5] = "";
    if (release_date && strlen(release_date) >= 4)
    {
        strncpy(year, release_date, 4);
        year[4] = '\0';
    }
    int cursor = 0;
    meta_sub_buf[0] = '\0';
    if (developer && strlen(developer) > 0)
        cursor += lv_snprintf(meta_sub_buf + cursor, sizeof(meta_sub_buf) - cursor, "%s", developer);
    if (strlen(year) > 0)
        cursor += lv_snprintf(meta_sub_buf + cursor, sizeof(meta_sub_buf) - cursor, "%s%s",
                              cursor > 0 ? " \xC2\xB7 " : "", year);
    if (genre && strlen(genre) > 0 && strcmp(genre, "__RECENT__") != 0)
        cursor += lv_snprintf(meta_sub_buf + cursor, sizeof(meta_sub_buf) - cursor, "%s%s",
                              cursor > 0 ? " \xC2\xB7 " : "", genre);
    return 0;
}

/* "NOW FOCUSED" badge — created once, moved between tiles */
static lv_obj_t *focus_badge;

static void ensure_focus_badge(lv_obj_t *tile)
{
    if (!focus_badge)
    {
        focus_badge = lv_obj_create(tile);
        lv_obj_set_size(focus_badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(focus_badge, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(focus_badge, 140, LV_PART_MAIN); /* 55% */
        lv_obj_set_style_radius(focus_badge, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_left(focus_badge, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_right(focus_badge, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_top(focus_badge, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(focus_badge, 4, LV_PART_MAIN);
        lv_obj_set_style_border_width(focus_badge, 0, LV_PART_MAIN);
        lv_obj_clear_flag(focus_badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(focus_badge, LV_ALIGN_TOP_LEFT, 10, 10);

        lv_obj_t *lbl = lv_label_create(focus_badge);
        lv_label_set_text(lbl, "NOW FOCUSED");
        lv_obj_set_style_text_font(lbl, &dash_font_ui_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, EF_FG, LV_PART_MAIN);
        lv_obj_set_style_text_letter_space(lbl, 1, LV_PART_MAIN);
    }
    else
    {
        /* Re-parent the badge to the new tile */
        lv_obj_set_parent(focus_badge, tile);
        lv_obj_align(focus_badge, LV_ALIGN_TOP_LEFT, 10, 10);
    }
}

static int prev_dot_sel = -1;

static void update_indicator_dots(int sel, int total)
{
    if (!dots_container) return;

    int dot_total = (total > MAX_DOTS) ? MAX_DOTS : total;

    /* Clamp sel to dot range — when more items than dots, the bar
     * maps proportionally across the dot strip. */
    int dot_sel = (total > MAX_DOTS) ? (sel * (MAX_DOTS - 1) / (total - 1)) : sel;
    if (dot_sel >= dot_total) dot_sel = dot_total - 1;
    if (dot_sel < 0) dot_sel = 0;

    /* Rebuild dots if count changed */
    int existing = lv_obj_get_child_cnt(dots_container);
    if (existing != dot_total)
    {
        lv_obj_clean(dots_container);
        for (int i = 0; i < dot_total; i++)
        {
            lv_obj_t *dot = lv_img_create(dots_container);
            lv_img_set_src(dot, (i == dot_sel) ? &pill_dot_bar : &pill_dot_inactive);
        }
        prev_dot_sel = dot_sel;
        return;
    }

    /* Only update the old and new dot — O(1) instead of O(n) */
    if (dot_sel != prev_dot_sel)
    {
        int dot_cnt = (int)lv_obj_get_child_cnt(dots_container);
        if (prev_dot_sel >= 0 && prev_dot_sel < dot_cnt)
            lv_img_set_src(lv_obj_get_child(dots_container, prev_dot_sel), &pill_dot_inactive);
        if (dot_sel >= 0 && dot_sel < dot_cnt)
            lv_img_set_src(lv_obj_get_child(dots_container, dot_sel), &pill_dot_bar);
        prev_dot_sel = dot_sel;
    }
}

static void rail_update_focus(bool animate)
{
    if (!rail) return;
    int child_cnt = lv_obj_get_child_cnt(rail);
    if (child_cnt <= 1) return;

    slots_init();

    /* Clamp selected_index */
    int last = child_cnt - 1;
    selected_index = LV_CLAMP(1, selected_index, last);

    dash_printf(LEVEL_TRACE, "[RAIL] update_focus: sel=%d/%d\n", selected_index, last);

    /* Position each tile into its slot.
     * ±4: animate smoothly (covers all visible tiles on screen)
     * rest: snap position/size instantly (no animation overhead) */
    for (int i = 1; i < child_cnt; i++)
    {
        int delta = (i > selected_index) ? (i - selected_index) : (selected_index - i);
        bool tile_animate = animate && (delta <= 4);
        lv_obj_t *tile = lv_obj_get_child(rail, i);
        tile_apply_state(tile, i, selected_index, tile_animate);
    }

    /* Update meta, badge, backdrop */
    lv_obj_t *focus_tile = lv_obj_get_child(rail, selected_index);
    if (focus_tile)
    {
        title_t *t = focus_tile->user_data;

        /* Fetch developer/year from DB — cached after first lookup */
        if (!t->meta_cached && t->db_id >= 0)
        {
            meta_sub_buf[0] = '\0';
            char cmd[SQL_MAX_COMMAND_LEN];
            lv_snprintf(cmd, sizeof(cmd), SQL_TITLE_GET_BY_ID, t->db_id);
            db_command_with_callback(cmd, meta_info_callback, NULL);
            strncpy(t->meta_subtitle, meta_sub_buf, sizeof(t->meta_subtitle) - 1);
            t->meta_subtitle[sizeof(t->meta_subtitle) - 1] = '\0';
            t->meta_cached = true;
        }
        dash_update_meta(t->title, t->meta_subtitle);
        dash_update_hero_counter(selected_index - 1, child_cnt - 1);

        /* "NOW FOCUSED" badge disabled */

        /* Update backdrop with fanart (or fallback to poster, or color wash) */
        if (t->jpg_info && t->jpg_info->thumb_path)
        {
            dash_backdrop_update(t->jpg_info->thumb_path);
        }
        else
        {
            dash_backdrop_update(NULL);
        }
    }

    /* Update indicator dots */
    update_indicator_dots(selected_index - 1, child_cnt - 1);
}

static void item_selection_callback(lv_event_t *event)
{
    lv_event_code_t e = lv_event_get_code(event);
    lv_obj_t *item_container = lv_event_get_target(event);
    title_t *t = item_container->user_data;

    if (e == LV_EVENT_FOCUSED)
    {
        /* O(1) index lookup via cached rail_index */
        selected_index = t->rail_index;
        if (suppress_focus_anim)
        {
            suppress_focus_anim = false;
            rail_update_focus(true);
        }
        else
        {
            rail_update_focus(true);
        }
    }
    else if (e == LV_EVENT_KEY)
    {
        lv_key_t key = *((lv_key_t *)lv_event_get_param(event));
        int child_cnt = lv_obj_get_child_cnt(rail);
        int last = child_cnt - 1;

        if (key == LV_KEY_RIGHT)
        {
            if (selected_index < last) selected_index++;
        }
        else if (key == LV_KEY_LEFT)
        {
            if (selected_index > 1) selected_index--;
        }
        else if (key == 'L' || key == 'R')
        {
            /* Fast scroll: jump by 6 tiles (LT/RT triggers) */
            int jump = (key == 'R') ? 6 : -6;
            selected_index = LV_CLAMP(1, selected_index + jump, last);
        }
        else if (key == DASH_PREV_PAGE || key == DASH_NEXT_PAGE)
        {
            /* Page switching (LB/RB) — cycle through TOML pages directly */
            int page_count = dash_scroller_get_page_count();
            page_current += (key == DASH_NEXT_PAGE) ? 1 : -1;
            if (page_current < 0) page_current = page_count - 1;
            if (page_current >= page_count) page_current = 0;

            dash_printf(LEVEL_TRACE, "[NAV] Page switch: page_current=%d page_count=%d page='%s'\n",
                        page_current, page_count,
                        parsers[page_current] ? parsers[page_current]->page_title : "NULL");

            selected_index = 1; /* reset selection when switching pages */
            dash_scroller_set_page();
            dash_scroller_sync_tab();
            dash_update_meta("", "");
            dash_update_hero_counter(0, 0);
            return;
        }
        else if (key == DASH_INFO_PAGE && selected_index > 0)
        {
            dash_synop_open(t->db_id);
            return;
        }
        else if (key == DASH_CONTEXT_PAGE && selected_index > 0)
        {
            dash_context_menu_open(t->db_id);
            return;
        }
        else if (key == DASH_SETTINGS_PAGE)
        {
            dash_mainmenu_open();
            return;
        }
        else if (key == LV_KEY_ENTER && selected_index > 0)
        {
            char cmd[SQL_MAX_COMMAND_LEN];
            char time_str[20];
            lv_snprintf(cmd, sizeof(cmd), SQL_TITLE_GET_LAUNCH_PATH, t->db_id);
            db_command_with_callback(cmd, get_launch_path_callback, NULL);

            if (dash_launcher_is_launchable(dash_launch_path))
            {
                platform_get_iso8601_time(time_str);
                lv_snprintf(cmd, sizeof(cmd), SQL_TITLE_SET_LAST_LAUNCH_DATETIME, time_str, t->db_id);
                db_command_with_callback(cmd, NULL, NULL);
                lv_set_quit(LV_QUIT_OTHER);
            }
            return;
        }
        else if (key == LV_KEY_UP)
        {
            dash_tab_bar_enter_nav(page_current);
            return;
        }
        else
        {
            return;
        }

        /* Move focus to the new selected tile.
         * dash_focus_change triggers LV_EVENT_FOCUSED which calls rail_update_focus(true). */
        lv_obj_t *new_tile = lv_obj_get_child(rail, selected_index);
        if (new_tile && lv_obj_is_valid(new_tile))
        {
            dash_focus_change(new_tile);
        }

        /* Pre-load thumbnails around selection */
        for (int i = LV_MAX(1, selected_index - 3); i <= LV_MIN(last, selected_index + 3); i++)
        {
            lv_obj_t *tile = lv_obj_get_child(rail, i);
            if (tile)
            {
                title_t *tt = tile->user_data;
                if (tt->jpg_info)
                {
                    tt->jpg_info->prevent_abort = true;
                    lv_event_t ev;
                    ev.target = tile;
                    update_thumbnail_callback(&ev);
                }
            }
        }
    }
}

static void item_deletion_callback(lv_event_t *event)
{
    lv_obj_t *item_container = lv_event_get_target(event);
    title_t *t = item_container->user_data;
    if (t->jpg_info)
    {
        t->jpg_info->decomp_handle = NULL;
        lv_mem_free(t->jpg_info->thumb_path);
        lv_mem_free(t->jpg_info);
    }
    lv_mem_free(t);
}

/* ============================================================
 *  DB scan thread items (preserved from original)
 * ============================================================ */
typedef struct item_strings
{
    char id[8];
    char title[MAX_META_LEN];
    char *launch_path;
    lv_obj_t *item_container;
    struct item_strings *next;
} item_strings_t;

typedef struct item_strings_callback
{
    item_strings_t *head;
    item_strings_t *tail;
} item_strings_callback_t;

static int item_scan_callback(void *param, int argc, char **argv, char **azColName)
{
    item_strings_callback_t *item_cb = param;
    (void)argc; (void)azColName;

    dash_printf(LEVEL_TRACE, "[SCAN] item_scan_callback: argc=%d id=%s title=%s path=%s\n",
                argc,
                argv[0] ? argv[0] : "NULL",
                argv[1] ? argv[1] : "NULL",
                argv[2] ? argv[2] : "NULL");

    item_strings_t *item = lv_mem_alloc(sizeof(item_strings_t));
    lv_memset(item, 0, sizeof(item_strings_t));

    assert(strcmp(azColName[0], SQL_TITLE_DB_ID) == 0);
    assert(strcmp(azColName[1], SQL_TITLE_NAME) == 0);
    assert(strcmp(azColName[2], SQL_TITLE_LAUNCH_PATH) == 0);

    strncpy(item->id, argv[0], sizeof(item->id) - 1);
    strncpy(item->title, argv[1], sizeof(item->title) - 1);

    if (strlen(argv[2]) <= 3)
    {
        dash_printf(LEVEL_WARN, "[SCAN] Skipping item '%s': launch path too short (%s)\n",
                    item->title, argv[2]);
        lv_mem_free(item);
        return 0;
    }

    item->launch_path = lv_strdup(argv[2]);

    if (item_cb->tail == NULL)
    {
        assert(item_cb->head == NULL);
        item_cb->head = item;
        item_cb->tail = item;
    }
    else
    {
        item_cb->tail->next = item;
        item_cb->tail = item;
    }
    return 0;
}

static void item_scan_add(lv_obj_t *scroller, item_strings_callback_t *item_cb)
{
    item_strings_t *item = item_cb->head;
    int add_count = 0;

    dash_printf(LEVEL_TRACE, "[SCAN] item_scan_add: head=%p scroller=%p\n",
                (void *)item, (void *)scroller);

    /* First pass: add all items to the rail */
    while (item)
    {
        title_t *t = lv_mem_alloc(sizeof(title_t));
        assert(t);
        if (t == NULL) { item = item->next; continue; }
        t->jpg_info = NULL;
        t->title[0] = '\0';
        t->meta_subtitle[0] = '\0';
        t->meta_cached = false;
        t->db_id = atoi(item->id);
        t->rail_index = add_count + 1;  /* 1-based: child[0] is spacer */
        t->shadow_img = NULL;

        lvgl_getlock();
        shadows_init();

        t->shadow_img = NULL; /* created after tile below */

        /* Create tile container */
        lv_obj_t *tile = lv_obj_create(scroller);
        item->item_container = tile;
        tile->user_data = t;
        lv_obj_add_style(tile, &rail_tile_style, LV_PART_MAIN);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(tile, DASH_TILE_W, DASH_TILE_H);

        /* Initial state: default size/opa, will be positioned by rail_update_focus */
        lv_obj_set_size(tile, DASH_TILE_W * SIZE_PCT_DEFAULT / 100,
                              DASH_TILE_H * SIZE_PCT_DEFAULT / 100);
        lv_obj_set_style_opa(tile, OPA_DEFAULT, LV_PART_MAIN);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_CHAIN);

        /* Check if box art exists — if so, skip the card overlay.
         * We check the file directly since jpg_info isn't set until the second pass. */
        {
            char thumb_check[DASH_MAX_PATH];
            strncpy(thumb_check, item->launch_path, DASH_MAX_PATH - 1);
            thumb_check[DASH_MAX_PATH - 1] = '\0';
            char *sep = strrchr(thumb_check, DASH_PATH_SEPARATOR);
            bool has_art = false;
            if (sep)
            {
                strcpy(sep + 1, DASH_GAME_THUMBNAIL);
                DWORD attr = GetFileAttributes(thumb_check);
                has_art = (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
            }
            if (!has_art)
            {
            /* Bottom gradient strip for title legibility */
            lv_obj_t *grad = lv_obj_create(tile);
            lv_obj_set_size(grad, DASH_TILE_W, DASH_TILE_H * 2 / 5);
            lv_obj_align(grad, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_set_style_bg_color(grad, lv_color_black(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(grad, 180, LV_PART_MAIN);
            lv_obj_set_style_bg_grad_color(grad, lv_color_black(), LV_PART_MAIN);
            lv_obj_set_style_bg_grad_dir(grad, LV_GRAD_DIR_VER, LV_PART_MAIN);
            lv_obj_set_style_bg_main_stop(grad, 0, LV_PART_MAIN);
            lv_obj_set_style_bg_grad_stop(grad, 255, LV_PART_MAIN);
            lv_obj_set_style_border_width(grad, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(grad, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(grad, 0, LV_PART_MAIN);
            lv_obj_clear_flag(grad, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

            /* Title label at bottom */
            lv_obj_t *title_lbl = lv_label_create(tile);
            lv_obj_set_style_text_font(title_lbl, &dash_font_ui_16, LV_PART_MAIN);
            lv_obj_set_style_text_color(title_lbl, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_width(title_lbl, DASH_TILE_W - 28);
            lv_label_set_text(title_lbl, item->title);
            lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_DOT);
            lv_obj_align(title_lbl, LV_ALIGN_BOTTOM_LEFT, 14, -12);
            }
        }

        /* Input callbacks */
        lv_group_add_obj(lv_group_get_default(), tile);
        lv_obj_add_event_cb(tile, item_selection_callback, LV_EVENT_KEY, NULL);
        lv_obj_add_event_cb(tile, item_selection_callback, LV_EVENT_FOCUSED, NULL);
        lv_obj_add_event_cb(tile, item_deletion_callback, LV_EVENT_DELETE, NULL);

        /* Create shadow (hidden until tile_apply_state shows it for focused tile) */
        {
            lv_obj_t *shd = lv_img_create(rail_wrap);
            lv_img_set_src(shd, &shadow_unfocused_img);
            lv_obj_clear_flag(shd, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(shd, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_to_index(shd, 0);
            t->shadow_img = shd;
        }

        strncpy(t->title, item->title, sizeof(t->title) - 1);
        add_count++;
        dash_printf(LEVEL_TRACE, "[SCAN] Added tile #%d: '%s' (db_id=%d)\n",
                    add_count, t->title, t->db_id);
        lvgl_removelock();
        item = item->next;
    }

    dash_printf(LEVEL_TRACE, "[SCAN] item_scan_add: total tiles added = %d, scroller child_cnt = %d\n",
                add_count, (int)lv_obj_get_child_cnt(scroller));

    /* Second pass: scan for thumbnails */
    char *thumb_path = lv_mem_alloc(DASH_MAX_PATH);
    item = item_cb->head;
    while (item)
    {
        lv_obj_t *item_container = item->item_container;
        title_t *t = item_container->user_data;

        strcpy(thumb_path, item->launch_path);
        lv_mem_free(item->launch_path);
        char *b = strrchr(thumb_path, DASH_PATH_SEPARATOR);
        if (b == NULL) { item = item->next; continue; }
        strcpy(&b[1], DASH_GAME_THUMBNAIL);

        DWORD fileAttributes = GetFileAttributes(thumb_path);
        if (fileAttributes == INVALID_FILE_ATTRIBUTES || (fileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            item = item->next;
            continue;
        }

        jpg_info_t *jpg_info = lv_mem_alloc(sizeof(jpg_info_t));
        assert(jpg_info);
        if (jpg_info == NULL) { item = item->next; continue; }
        lv_memset(jpg_info, 0, sizeof(jpg_info_t));
        jpg_info->thumb_path = lv_strdup(thumb_path);

        lvgl_getlock();
        t->jpg_info = jpg_info;
        lv_obj_add_event_cb(item_container, update_thumbnail_callback, LV_EVENT_DRAW_MAIN_END, NULL);
        lv_obj_invalidate(item_container);
        lvgl_removelock();
        item = item->next;
    }
    lv_mem_free(thumb_path);
}

static void dash_scroller_get_sort_strings(unsigned int sort_index, const char **sort_by, const char **order_by)
{
    switch (sort_index)
    {
    case DASH_SORT_RATING:       *sort_by = SQL_TITLE_RATING;       *order_by = "DESC"; break;
    case DASH_SORT_LAST_LAUNCH:  *sort_by = SQL_TITLE_LAST_LAUNCH;  *order_by = "DESC"; break;
    case DASH_SORT_RELEASE_DATE: *sort_by = SQL_TITLE_RELEASE_DATE; *order_by = "DESC"; break;
    default:                     *sort_by = SQL_TITLE_NAME;          *order_by = "ASC";
    }
}

static int db_scan_thread_f(void *param)
{
    parse_handle_t *p = param;
    char cmd[SQL_MAX_COMMAND_LEN];
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);

    dash_printf(LEVEL_TRACE, "[SCAN] db_scan_thread_f: page='%s' scroller=%p\n",
                p->page_title, (void *)p->scroller);

    item_strings_callback_t item_cb;
    lv_memset(&item_cb, 0, sizeof(item_strings_callback_t));

    if (strcmp(p->page_title, "Recent") == 0)
    {
        lv_snprintf(cmd, sizeof(cmd), SQL_TITLE_GET_RECENT,
                    SQL_TITLE_DB_ID "," SQL_TITLE_NAME "," SQL_TITLE_LAUNCH_PATH,
                    dash_settings.earliest_recent_date, dash_settings.max_recent_items);
        dash_printf(LEVEL_TRACE, "[SCAN] Recent query: %s\n", cmd);
        db_command_with_callback(cmd, item_scan_callback, &item_cb);
        item_scan_add(p->scroller, &item_cb);
    }
    else
    {
        int sort_index = 0;
        const char *sort_by, *order_by;
        dash_scroller_get_sort_value(p->page_title, &sort_index);
        dash_scroller_get_sort_strings(sort_index, &sort_by, &order_by);

        lv_snprintf(cmd, sizeof(cmd), SQL_TITLE_GET_SORTED_LIST,
                    SQL_TITLE_DB_ID "," SQL_TITLE_NAME "," SQL_TITLE_LAUNCH_PATH,
                    p->page_title, sort_by, order_by);
        dash_printf(LEVEL_TRACE, "[SCAN] Page query: %s\n", cmd);
        db_command_with_callback(cmd, item_scan_callback, &item_cb);
        item_scan_add(p->scroller, &item_cb);
    }

    while (item_cb.head)
    {
        item_strings_t *next_item = item_cb.head->next;
        lv_mem_free(item_cb.head);
        item_cb.head = next_item;
    }

    /* After items are added, update focus and potentially switch to this page */
    lvgl_getlock();
    int child_cnt = lv_obj_get_child_cnt(p->scroller);
    int active_child_cnt = rail ? (int)lv_obj_get_child_cnt(rail) : 0;
    dash_printf(LEVEL_TRACE, "[SCAN] Post-scan: page='%s' child_cnt=%d is_active=%d active_cnt=%d\n",
                p->page_title, child_cnt, (p->scroller == rail), active_child_cnt);

    if (p->scroller == rail && child_cnt > 1)
    {
        /* Active page got content — hide empty label and focus first tile */
        if (empty_page_label)
            lv_obj_add_flag(empty_page_label, LV_OBJ_FLAG_HIDDEN);
        selected_index = 1;
        lv_obj_t *first_tile = lv_obj_get_child(p->scroller, 1);
        dash_focus_set_final(lv_obj_get_child(p->scroller, 0));
        suppress_focus_anim = true;
        dash_focus_change(first_tile);
        rail_update_focus(false);
    }
    else if (child_cnt > 1 && active_child_cnt <= 1)
    {
        /* Active page is empty but this page has content — auto-switch */
        dash_printf(LEVEL_TRACE, "[SCAN] Auto-switching to page '%s' (active page empty)\n", p->page_title);
        for (int i = 0; i < DASH_MAX_PAGES; i++)
        {
            if (parsers[i] && parsers[i]->scroller == p->scroller)
            {
                page_current = i;
                selected_index = 1;
                dash_scroller_set_page();
                /* Sync tab bar */
                dash_scroller_sync_tab();
                break;
            }
        }
    }
    lvgl_removelock();

    return 0;
}

static void cache_free(title_t *t)
{
    assert(lv_obj_is_valid(t->jpg_info->canvas));
    jpeg_decoder_abort(t->jpg_info->decomp_handle);
    assert(t->jpg_info->mem);
    free(t->jpg_info->mem);
    lv_obj_del(t->jpg_info->canvas);
    t->jpg_info->decomp_handle = NULL;
    t->jpg_info->mem = NULL;
    t->jpg_info->image = NULL;
}

void dash_scroller_clear_page(const char *page_title)
{
    for (int i = 0; i < DASH_MAX_PAGES; i++)
    {
        if (parsers[i] == NULL) continue;
        if (strcmp(page_title, parsers[i]->page_title) == 0)
        {
            lv_obj_t *scroller = parsers[i]->scroller;
            lv_obj_t *child = lv_obj_get_child(scroller, 1);
            while (child)
            {
                lv_obj_del(child);
                child = lv_obj_get_child(scroller, 1);
            }
        }
    }
}

void dash_scroller_rescan_page(const char *page_title)
{
    dash_printf(LEVEL_TRACE, "[RESCAN] Clearing and rescanning '%s'\n", page_title);

    /* Clear existing tiles from the scroller */
    dash_scroller_clear_page(page_title);

    /* Delete DB entries for this page and rescan from disk */
    db_rebuild_page(dash_search_paths, page_title);

    /* Re-scan DB into the scroller by launching the scan thread */
    for (int i = 0; i < DASH_MAX_PAGES; i++)
    {
        if (parsers[i] && strcmp(parsers[i]->page_title, page_title) == 0)
        {
            parsers[i]->db_scan_thread = SDL_CreateThread(
                db_scan_thread_f, "rescan_thread", parsers[i]);
            break;
        }
    }
}

static void jpeg_clear_timer(lv_timer_t *t)
{
    (void)t;
    jpeg_ll_value_t *item = _lv_ll_get_head(&jpeg_decomp_list);
    while (item)
    {
        lv_obj_t *image_container = item->image_container;
        title_t *title = image_container->user_data;
        assert(title->jpg_info);
        if (lv_obj_is_visible(image_container) == false && title->jpg_info->prevent_abort == false)
        {
            jpeg_decoder_abort(title->jpg_info->decomp_handle);
            title->jpg_info->decomp_handle = NULL;
        }
        if (title->jpg_info->decomp_handle == NULL)
        {
            _lv_ll_remove(&jpeg_decomp_list, item);
            lv_mem_free(item);
            item = _lv_ll_get_head(&jpeg_decomp_list);
        }
        else
        {
            item = _lv_ll_get_next(&jpeg_decomp_list, item);
        }
    }
}

/* ============================================================
 *  Public API
 * ============================================================ */
void dash_scroller_set_page()
{
    toml_array_t *pages = toml_array_in(dash_search_paths, "pages");
    int page_max = LV_MIN(toml_array_nelem(pages), DASH_MAX_PAGES);
    page_current = LV_CLAMP(0, page_current, page_max - 1);

    dash_printf(LEVEL_TRACE, "[PAGE] dash_scroller_set_page: page_current=%d page_max=%d\n",
                page_current, page_max);

    /* Find the parser for the current page and make its scroller the active rail */
    if (parsers[page_current] == NULL)
    {
        dash_printf(LEVEL_WARN, "[PAGE] parser[%d] is NULL!\n", page_current);
        return;
    }

    rail = parsers[page_current]->scroller;
    selected_index = LV_MAX(1, selected_index);
    prev_dot_sel = -1;  /* force dot rebuild on page switch */

    int child_cnt = lv_obj_get_child_cnt(rail);
    dash_printf(LEVEL_TRACE, "[PAGE] rail=%p child_cnt=%d selected_index=%d page='%s'\n",
                (void *)rail, child_cnt, selected_index, parsers[page_current]->page_title);
    if (child_cnt <= 1)
    {
        selected_index = 0;
        dash_printf(LEVEL_TRACE, "[PAGE] No items (only null item), selected_index=0\n");

        /* Show empty page message */
        if (!empty_page_label)
        {
            empty_page_label = lv_label_create(rail_wrap);
            lv_obj_set_style_text_font(empty_page_label, &dash_font_ui_16, LV_PART_MAIN);
            lv_obj_set_style_text_color(empty_page_label, EF_FG_MUTED, LV_PART_MAIN);
            lv_obj_center(empty_page_label);
        }
        lv_label_set_text_fmt(empty_page_label, "No content in %s", parsers[page_current]->page_title);
        lv_obj_clear_flag(empty_page_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        selected_index = LV_CLAMP(1, selected_index, child_cnt - 1);
        dash_printf(LEVEL_TRACE, "[PAGE] Has %d items, selected_index=%d\n", child_cnt - 1, selected_index);
        /* Hide empty label if visible */
        if (empty_page_label)
            lv_obj_add_flag(empty_page_label, LV_OBJ_FLAG_HIDDEN);
    }

    /* Show only the active page's scroller (and shadows), hide others */
    for (int i = 0; i < DASH_MAX_PAGES; i++)
    {
        if (parsers[i] == NULL) continue;
        lv_obj_t *sc = parsers[i]->scroller;
        bool active = (i == page_current);

        if (active)
            lv_obj_clear_flag(sc, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(sc, LV_OBJ_FLAG_HIDDEN);

        /* Show/hide shadow images for this page's tiles */
        int cnt = lv_obj_get_child_cnt(sc);
        for (int j = 1; j < cnt; j++)
        {
            lv_obj_t *tile = lv_obj_get_child(sc, j);
            title_t *tt = tile ? tile->user_data : NULL;
            if (tt && tt->shadow_img)
            {
                if (active)
                    lv_obj_clear_flag(tt->shadow_img, LV_OBJ_FLAG_HIDDEN);
                else
                    lv_obj_add_flag(tt->shadow_img, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    /* Position the rail and set initial focus */
    rail_update_focus(false);

    /* Don't steal focus when tab bar navigation is active */
    if (!dash_tab_nav_is_active())
    {
        dash_focus_set_final(lv_obj_get_child(rail, 0));
        if (child_cnt > 1)
        {
            /* Suppress the animated rail_update that LV_EVENT_FOCUSED triggers —
             * tiles are already at correct positions from rail_update_focus(false) above. */
            suppress_focus_anim = true;
            lv_obj_t *focus_item = lv_obj_get_child(rail, selected_index);
            dash_focus_change(focus_item);
        }
        else
        {
            dash_focus_change(lv_obj_get_child(rail, 0));
        }
    }
}

void dash_scroller_init()
{
    lv_coord_t scr_w = lv_obj_get_width(lv_scr_act());
    lv_coord_t scr_h = lv_obj_get_height(lv_scr_act());
    page_current = dash_settings.startup_page_index;
    selected_index = 1;

    lv_memset(parsers, 0, sizeof(parsers));
    thumbnail_cache = lv_lru_create(thumbnail_cache_size, DASH_TILE_W * DASH_TILE_H * JPEG_BPP,
                                    (lv_lru_free_t *)cache_free, NULL);

    jpeg_decoder_init(JPEG_BPP * 8, 256);
    _lv_ll_init(&jpeg_decomp_list, sizeof(jpeg_ll_value_t));
    lv_timer_create(jpeg_clear_timer, LV_DISP_DEF_REFR_PERIOD, NULL);

    /* Rail viewport: occupies the space between hero strip and meta row */
    rail_wrap = lv_obj_create(lv_scr_act());
    lv_obj_set_size(rail_wrap, scr_w, scr_h - 140 - 120); /* 140px from top, 120px from bottom */
    lv_obj_set_pos(rail_wrap, 0, 140);
    lv_obj_set_style_bg_opa(rail_wrap, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(rail_wrap, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(rail_wrap, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(rail_wrap, 0, LV_PART_MAIN);
    lv_obj_clear_flag(rail_wrap, LV_OBJ_FLAG_SCROLLABLE);

    /* Indicator dots container — centered below the rail area */
    dots_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(dots_container, LV_SIZE_CONTENT, 10);
    lv_obj_align(dots_container, LV_ALIGN_TOP_MID, 0, scr_h - 130); /* ~y=590 */
    lv_obj_set_style_bg_opa(dots_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(dots_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dots_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(dots_container, 6, LV_PART_MAIN);
    lv_obj_set_layout(dots_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(dots_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(dots_container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

void dash_scroller_scan_db()
{
    toml_table_t *paths = dash_search_paths;
    toml_array_t *pages = toml_array_in(paths, "pages");
    int dash_num_pages = LV_MIN(toml_array_nelem(pages), DASH_MAX_PAGES);

    /* Clean up old parsers */
    for (int i = 0; i < DASH_MAX_PAGES; i++)
    {
        if (parsers[i]) { lv_mem_free(parsers[i]); parsers[i] = NULL; }
    }

    lv_coord_t rail_h = lv_obj_get_height(rail_wrap);

    for (int i = 0; i < dash_num_pages; i++)
    {
        parse_handle_t *parser = lv_mem_alloc(sizeof(parse_handle_t));
        assert(parser);
        parsers[i] = parser;
        lv_memset(parser, 0, sizeof(parse_handle_t));

        /* Get page name from TOML */
        toml_datum_t name_str = toml_string_in(toml_table_at(pages, i), "name");
        if (name_str.ok)
        {
            strncpy(parser->page_title, name_str.u.s, sizeof(parser->page_title) - 1);
        }

        /* Create the rail container for this page (absolute positioning, no flex) */
        lv_obj_t *scroller = lv_obj_create(rail_wrap);
        parser->scroller = scroller;
        parser->tile = scroller;

        lv_obj_update_layout(rail_wrap);
        lv_coord_t wrap_w = lv_obj_get_width(rail_wrap);
        lv_coord_t wrap_h = lv_obj_get_height(rail_wrap);
        lv_obj_set_size(scroller, wrap_w, wrap_h);
        lv_obj_set_pos(scroller, 0, 0);
        lv_obj_set_style_bg_opa(scroller, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(scroller, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(scroller, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(scroller, 0, LV_PART_MAIN);
        lv_obj_clear_flag(scroller, LV_OBJ_FLAG_SCROLLABLE);

        dash_printf(LEVEL_TRACE, "[INIT] Scroller page %d: wrap=%dx%d tile_h=%d\n",
                    i, wrap_w, wrap_h, DASH_TILE_H);

        /* Null item (always index 0, hidden) */
        title_t *t = lv_mem_alloc(sizeof(title_t));
        t->db_id = -1;
        t->jpg_info = NULL;
        strcpy(t->title, "No item selected");
        lv_obj_t *null_item = lv_obj_create(scroller);
        null_item->user_data = t;
        lv_obj_add_flag(null_item, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(null_item, 0, 0);
        lv_group_add_obj(lv_group_get_default(), null_item);
        lv_obj_add_event_cb(null_item, item_selection_callback, LV_EVENT_KEY, NULL);
        lv_obj_add_event_cb(null_item, item_selection_callback, LV_EVENT_FOCUSED, NULL);

        /* Initially hide all but the first page */
        if (i != page_current)
        {
            lv_obj_add_flag(scroller, LV_OBJ_FLAG_HIDDEN);
        }

        /* Start DB scan thread */
        parser->db_scan_thread = SDL_CreateThread(db_scan_thread_f, "game_parser_thread", parser);
    }

    /* Point rail to the current page's scroller */
    if (parsers[page_current])
    {
        rail = parsers[page_current]->scroller;
    }
}

void dash_scroller_clear_empty_label(void)
{
    if (empty_page_label)
        lv_obj_add_flag(empty_page_label, LV_OBJ_FLAG_HIDDEN);
}

void dash_scroller_restore_focus(void)
{
    /* Restore focus to the currently selected tile without resetting
     * selection or re-snapping positions. Used when returning from
     * tab nav to the same page. */
    if (!rail) return;
    int child_cnt = lv_obj_get_child_cnt(rail);
    if (child_cnt <= 1) return;
    /* selected_index may have been corrupted by LVGL focus cycling during
     * tab nav — use the saved origin value instead. */
    extern int tab_nav_origin_sel;
    selected_index = LV_CLAMP(1, tab_nav_origin_sel, child_cnt - 1);
    dash_printf(LEVEL_TRACE, "[RESTORE] origin_sel=%d selected_index=%d child_cnt=%d\n",
                tab_nav_origin_sel, selected_index, child_cnt);

    suppress_focus_anim = true;
    dash_focus_set_final(lv_obj_get_child(rail, 0));
    lv_obj_t *tile = lv_obj_get_child(rail, selected_index);
    if (tile)
        dash_focus_change(tile);
}

void dash_scroller_set_page_index(int index)
{
    int page_count = dash_scroller_get_page_count();
    if (index < 0 || index >= page_count) return;
    page_current = index;
    selected_index = 1;
    dash_scroller_set_page();
    dash_scroller_sync_tab();
    dash_update_meta("", "");
    dash_update_hero_counter(0, 0);
}

const char *dash_scroller_get_title(int index)
{
    if (index >= DASH_MAX_PAGES || parsers[index] == NULL) return NULL;
    return parsers[index]->page_title;
}

int dash_scroller_get_page_count()
{
    int cnt = 0;
    for (int i = 0; i < DASH_MAX_PAGES; i++)
    {
        if (parsers[i]) cnt = i + 1;
    }
    return cnt;
}

bool dash_scroller_get_sort_value(const char *page_title, int *sort_value)
{
    if (page_title == NULL || sort_value == NULL) return false;

    const char *valueStart = strstr(dash_settings.sort_strings, page_title);
    if (valueStart == NULL) return false;

    valueStart += strlen(page_title) + 1;
    int value;
    if (sscanf(valueStart, "%d", &value) != 1) return false;

    value = LV_CLAMP(0, value, DASH_SORT_MAX - 1);
    *sort_value = value;
    return true;
}

struct resort_param
{
    int sort_index;
    lv_obj_t **sorted_objs;
};

static int resort_page_callback(void *param, int argc, char **argv, char **azColName)
{
    (void)azColName; (void)argc;
    assert(argc == 1);

    struct resort_param *p = param;
    lv_obj_t *scroller = p->sorted_objs[0];
    int db_id = atoi(argv[0]);
    lv_task_handler();
    for (unsigned int i = 1; i < lv_obj_get_child_cnt(scroller); i++)
    {
        lv_obj_t *item_container = lv_obj_get_child(scroller, i);
        title_t *t = item_container->user_data;
        if (t->db_id == db_id)
        {
            p->sorted_objs[p->sort_index] = item_container;
            p->sort_index++;
            return 0;
        }
    }
    assert(0);
    return 0;
}

void dash_scroller_resort_page(const char *page_title)
{
    char cmd[SQL_MAX_COMMAND_LEN];
    int sort_index;
    if (dash_scroller_get_sort_value(page_title, &sort_index) == false) return;

    lv_obj_t *scroller = NULL;
    for (int i = 0; i < DASH_MAX_PAGES; i++)
    {
        if (parsers[i] == NULL) continue;
        if (strcmp(page_title, parsers[i]->page_title) == 0)
        {
            scroller = parsers[i]->scroller;
            break;
        }
    }
    assert(scroller);

    if (lv_obj_get_child_cnt(scroller) <= 1) return;

    const char *sort_by, *order_by;
    dash_scroller_get_sort_strings(sort_index, &sort_by, &order_by);

    lv_snprintf(cmd, sizeof(cmd), SQL_TITLE_GET_SORTED_LIST, SQL_TITLE_DB_ID, page_title, sort_by, order_by);

    int child_cnt = lv_obj_get_child_cnt(scroller);
    struct resort_param *p = lv_mem_alloc(sizeof(struct resort_param));
    p->sort_index = 1;
    p->sorted_objs = lv_mem_alloc(sizeof(lv_obj_t *) * child_cnt);
    lv_memset(p->sorted_objs, 0, sizeof(lv_obj_t *) * child_cnt);
    p->sorted_objs[0] = scroller;

    db_command_with_callback(cmd, resort_page_callback, p);
    for (int i = 1; i < child_cnt; i++)
    {
        scroller->spec_attr->children[i] = p->sorted_objs[i];
        /* Update cached rail_index after reorder */
        title_t *t = p->sorted_objs[i]->user_data;
        t->rail_index = i;
    }
    lv_mem_free(p->sorted_objs);
    lv_mem_free(p);
    lv_obj_mark_layout_as_dirty(scroller);
}

/* ── Remote debug accessors ── */

int dash_scroller_get_page_current(void) { return page_current; }
int dash_scroller_get_selected_index(void) { return selected_index; }

int dash_scroller_get_item_count(void)
{
    if (!rail) return 0;
    int cnt = lv_obj_get_child_cnt(rail);
    return (cnt > 1) ? cnt - 1 : 0; /* subtract null item */
}

const char *dash_scroller_get_focused_title(void)
{
    if (!rail) return NULL;
    int cnt = lv_obj_get_child_cnt(rail);
    if (selected_index < 1 || selected_index >= cnt) return NULL;
    lv_obj_t *tile = lv_obj_get_child(rail, selected_index);
    if (!tile) return NULL;
    title_t *t = tile->user_data;
    return t ? t->title : NULL;
}

/* ── Snapshot for remote status ── */
static int scroller_snapshot(char *buf, int size)
{
    int page = page_current;
    const char *page_name = dash_scroller_get_title(page);
    int sel = selected_index;
    int total = dash_scroller_get_item_count();
    const char *focused = dash_scroller_get_focused_title();
    int tab = dash_get_tab();
    int page_count = dash_scroller_get_page_count();

    int pos = 0;
    pos += snprintf(buf + pos, size - pos, "[rail]\n");
    pos += snprintf(buf + pos, size - pos, "tab=%d\n", tab);
    pos += snprintf(buf + pos, size - pos, "page=%d/%d name=\"%s\"\n",
                    page + 1, page_count, page_name ? page_name : "?");
    pos += snprintf(buf + pos, size - pos, "selected=%d/%d\n", sel, total);
    pos += snprintf(buf + pos, size - pos, "title=\"%s\"\n", focused ? focused : "");

    /* List nearby tiles for context */
    if (rail) {
        int cnt = lv_obj_get_child_cnt(rail);
        int start = (sel - 3 > 1) ? sel - 3 : 1;
        int end = (sel + 3 < cnt - 1) ? sel + 3 : cnt - 1;
        pos += snprintf(buf + pos, size - pos, "visible_tiles=");
        for (int i = start; i <= end && pos < size - 1; i++) {
            lv_obj_t *tile = lv_obj_get_child(rail, i);
            if (!tile) continue;
            title_t *t = tile->user_data;
            const char *name = (t && t->title[0]) ? t->title : "?";
            pos += snprintf(buf + pos, size - pos, "%s\"%s\"",
                            i > start ? ", " : "", name);
            if (i == sel)
                pos += snprintf(buf + pos, size - pos, " [SELECTED]");
        }
        pos += snprintf(buf + pos, size - pos, "\n");
    }

    return pos;
}

void dash_scroller_snapshot_register(void)
{
    dash_snapshot_register(scroller_snapshot);
}
