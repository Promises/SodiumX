// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2022 Ryzee119

#ifndef _STYLES_H
#define _STYLES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lithiumx.h"

/* ============================================================
 *  Everforest Dark — color tokens
 *  https://github.com/sainnhe/everforest
 * ============================================================ */
#define EF_BG_DIM    lv_color_hex(0x1e2326)
#define EF_BG0       lv_color_hex(0x272e33)
#define EF_BG1       lv_color_hex(0x2e383c)
#define EF_BG2       lv_color_hex(0x374145)
#define EF_BG3       lv_color_hex(0x414b50)
#define EF_BG4       lv_color_hex(0x495156)
#define EF_FG        lv_color_hex(0xd3c6aa)
#define EF_FG_MUTED  lv_color_hex(0x9da9a0)
#define EF_RED       lv_color_hex(0xe67e80)
#define EF_ORANGE    lv_color_hex(0xe69875)
#define EF_YELLOW    lv_color_hex(0xdbbc7f)
#define EF_GREEN     lv_color_hex(0xa7c080)
#define EF_AQUA      lv_color_hex(0x83c092)
#define EF_BLUE      lv_color_hex(0x7fbbb3)
#define EF_PURPLE    lv_color_hex(0xd699b6)
#define EF_GREY0     lv_color_hex(0x7a8478)
#define EF_GREY1     lv_color_hex(0x859289)
#define EF_GREY2     lv_color_hex(0x9da9a0)

/* ============================================================
 *  Accent color selection
 * ============================================================ */
typedef enum {
    ACCENT_GREEN = 0,
    ACCENT_AQUA,
    ACCENT_BLUE,
    ACCENT_YELLOW,
    ACCENT_ORANGE,
    ACCENT_PURPLE,
    ACCENT_MAX
} dash_accent_t;

extern lv_color_t dash_accent_color;
lv_color_t dash_accent_from_enum(dash_accent_t a);

/* Legacy color string for recolor markup (used by dash_synop.c) */
#define DASH_MENU_COLOR "#9da9a0"

/* ============================================================
 *  Margins
 * ============================================================ */
#ifndef DASH_XMARGIN
#define DASH_XMARGIN 20
#endif
#ifndef DASH_YMARGIN
#define DASH_YMARGIN 20
#endif

/* ============================================================
 *  Legacy styles (kept for transition compatibility)
 * ============================================================ */
extern lv_color_t dash_base_theme_color;
extern lv_style_t dash_background_style;
extern lv_style_t menu_table_style;
extern lv_style_t menu_table_highlight_style;
extern lv_style_t menu_table_cell_style;
extern lv_style_t object_style;
extern lv_style_t titleview_style;
extern lv_style_t titleview_image_container_style;
extern lv_style_t titleview_image_text_style;
extern lv_style_t titleview_header_footer_style;

/* ============================================================
 *  New Everforest styles
 * ============================================================ */

/* Status bar (top 44px) */
extern lv_style_t status_bar_style;
extern lv_style_t status_chip_style;

/* Tab bar */
extern lv_style_t tab_active_style;
extern lv_style_t tab_inactive_style;

/* Horizontal rail tiles */
extern lv_style_t rail_tile_style;
extern lv_style_t rail_tile_focused_style;

/* Controls bar (bottom 52px) */
extern lv_style_t controls_bar_style;

/* Overlay (scrim + card) */
extern lv_style_t overlay_scrim_style;
extern lv_style_t overlay_card_style;
extern lv_style_t overlay_item_style;
extern lv_style_t overlay_item_focused_style;

/* Two-pane panel (settings, sysinfo, browser) */
extern lv_style_t panel_style;
extern lv_style_t panel_nav_style;
extern lv_style_t panel_nav_item_style;
extern lv_style_t panel_nav_item_active_style;

/* Settings controls */
extern lv_style_t setting_row_style;
extern lv_style_t toggle_off_style;
extern lv_style_t toggle_on_style;
extern lv_style_t toggle_thumb_style;
extern lv_style_t seg_control_style;
extern lv_style_t seg_active_style;

/* System info */
extern lv_style_t stat_card_style;
extern lv_style_t progress_bar_track_style;
extern lv_style_t progress_bar_fill_style;

/* File browser */
extern lv_style_t file_row_style;
extern lv_style_t file_row_selected_style;
extern lv_style_t file_header_row_style;

/* Typography */
extern lv_style_t eyebrow_style;
extern lv_style_t meta_title_style;
extern lv_style_t body_muted_style;
extern lv_style_t mono_small_style;

/* Meta pills */
extern lv_style_t meta_pill_style;

void dash_styles_init(lv_color_t theme_colour);
void dash_styles_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
