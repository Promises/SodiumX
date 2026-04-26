// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2022 Ryzee119

#include "lithiumx.h"

/* ============================================================
 *  Legacy style globals (kept for transition)
 * ============================================================ */
lv_style_t dash_background_style;
lv_style_t menu_table_style;
lv_style_t menu_table_highlight_style;
lv_style_t menu_table_cell_style;
lv_style_t object_style;
lv_style_t titleview_style;
lv_style_t titleview_image_container_style;
lv_style_t titleview_image_text_style;
lv_style_t titleview_header_footer_style;
lv_color_t dash_base_theme_color;

/* ============================================================
 *  New Everforest style globals
 * ============================================================ */
lv_color_t dash_accent_color;

lv_style_t status_bar_style;
lv_style_t status_chip_style;
lv_style_t tab_active_style;
lv_style_t tab_inactive_style;
lv_style_t tab_focused_style;
lv_style_t rail_tile_style;
lv_style_t rail_tile_focused_style;
lv_style_t controls_bar_style;
lv_style_t overlay_scrim_style;
lv_style_t overlay_card_style;
lv_style_t overlay_item_style;
lv_style_t overlay_item_focused_style;
lv_style_t panel_style;
lv_style_t panel_nav_style;
lv_style_t panel_nav_item_style;
lv_style_t panel_nav_item_active_style;
lv_style_t setting_row_style;
lv_style_t toggle_off_style;
lv_style_t toggle_on_style;
lv_style_t toggle_thumb_style;
lv_style_t seg_control_style;
lv_style_t seg_active_style;
lv_style_t stat_card_style;
lv_style_t progress_bar_track_style;
lv_style_t progress_bar_fill_style;
lv_style_t file_row_style;
lv_style_t file_row_selected_style;
lv_style_t file_header_row_style;
lv_style_t eyebrow_style;
lv_style_t meta_title_style;
lv_style_t body_muted_style;
lv_style_t mono_small_style;
lv_style_t meta_pill_style;

/* ============================================================
 *  Accent color lookup
 * ============================================================ */
lv_color_t dash_accent_from_enum(dash_accent_t a)
{
    switch (a) {
    case ACCENT_GREEN:  return EF_GREEN;
    case ACCENT_AQUA:   return EF_AQUA;
    case ACCENT_BLUE:   return EF_BLUE;
    case ACCENT_YELLOW: return EF_YELLOW;
    case ACCENT_ORANGE: return EF_ORANGE;
    case ACCENT_PURPLE: return EF_PURPLE;
    default:            return EF_GREEN;
    }
}

/* ============================================================
 *  Style initialization
 * ============================================================ */
void dash_styles_init(lv_color_t theme_colour)
{
    dash_base_theme_color = theme_colour;
    dash_accent_color = theme_colour;

    /* ── Legacy styles (preserved for menu widget, synop, etc.) ── */

    lv_style_init(&dash_background_style);
    lv_style_set_border_width(&dash_background_style, 0);
    lv_style_set_radius(&dash_background_style, 0);
    lv_style_set_bg_color(&dash_background_style, EF_BG_DIM);
    lv_style_set_bg_grad_color(&dash_background_style, theme_colour);
    lv_style_set_bg_grad_dir(&dash_background_style, LV_GRAD_DIR_VER);

    lv_style_init(&menu_table_style);
    lv_style_set_bg_color(&menu_table_style, EF_BG0);
    lv_style_set_bg_opa(&menu_table_style, 240);
    lv_style_set_border_width(&menu_table_style, 1);
    lv_style_set_border_color(&menu_table_style, lv_color_white());
    lv_style_set_pad_all(&menu_table_style, 0);
    lv_style_set_radius(&menu_table_style, 0);
    lv_style_set_text_color(&menu_table_style, EF_FG);
    lv_style_set_text_font(&menu_table_style, &lv_font_rubik_20);
    lv_style_set_outline_width(&menu_table_style, 0);
    lv_style_set_text_line_space(&menu_table_style, 10);

    lv_style_init(&menu_table_cell_style);
    lv_style_set_border_width(&menu_table_cell_style, 1);
    lv_style_set_border_color(&menu_table_cell_style, EF_BG3);
    lv_style_set_bg_opa(&menu_table_cell_style, 0);
    lv_style_set_text_color(&menu_table_cell_style, EF_FG);
    lv_style_set_text_font(&menu_table_cell_style, &lv_font_rubik_20);
    lv_style_set_pad_top(&menu_table_cell_style, 10);
    lv_style_set_pad_bottom(&menu_table_cell_style, 10);
    lv_style_set_radius(&menu_table_cell_style, 0);
    lv_style_set_outline_width(&menu_table_cell_style, 0);

    lv_style_init(&object_style);
    lv_style_set_bg_color(&object_style, EF_BG0);
    lv_style_set_bg_opa(&object_style, 240);
    lv_style_set_border_width(&object_style, 0);
    lv_style_set_pad_all(&object_style, 0);
    lv_style_set_radius(&object_style, 0);
    lv_style_set_text_color(&object_style, EF_FG);
    lv_style_set_text_font(&object_style, &lv_font_rubik_20);
    lv_style_set_outline_width(&object_style, 0);
    lv_style_set_text_line_space(&object_style, 10);

    lv_style_init(&menu_table_highlight_style);
    lv_style_set_bg_color(&menu_table_highlight_style, theme_colour);

    lv_style_init(&titleview_style);
    lv_style_set_radius(&titleview_style, 0);
    lv_style_set_border_width(&titleview_style, 0);
    lv_style_set_bg_color(&titleview_style, EF_BG1);
    lv_style_set_pad_all(&titleview_style, 0);
    lv_style_set_pad_row(&titleview_style, 0);
    lv_style_set_pad_column(&titleview_style, 0);

    lv_style_init(&titleview_image_container_style);
    lv_style_set_radius(&titleview_image_container_style, 0);
    lv_style_set_bg_color(&titleview_image_container_style, EF_BG1);
    lv_style_set_border_color(&titleview_image_container_style, lv_color_darken(theme_colour, 100));
    lv_style_set_pad_all(&titleview_image_container_style, 0);
    lv_style_set_border_width(&titleview_image_container_style, 1);

    lv_style_init(&titleview_image_text_style);
    lv_style_set_align(&titleview_image_text_style, LV_ALIGN_CENTER);
    lv_style_set_text_font(&titleview_image_text_style, &lv_font_rubik_20);
    lv_style_set_text_color(&titleview_image_text_style, EF_FG);
    lv_style_set_text_align(&titleview_image_text_style, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&titleview_header_footer_style);
    lv_style_set_bg_color(&titleview_header_footer_style, theme_colour);
    lv_style_set_text_color(&titleview_header_footer_style, lv_color_white());
    lv_style_set_text_font(&titleview_header_footer_style, &lv_font_rubik_26);

    /* ── Status bar (top 44px) ── */

    lv_style_init(&status_bar_style);
    lv_style_set_bg_opa(&status_bar_style, LV_OPA_TRANSP);
    lv_style_set_border_width(&status_bar_style, 0);
    lv_style_set_pad_left(&status_bar_style, 24);
    lv_style_set_pad_right(&status_bar_style, 24);
    lv_style_set_pad_top(&status_bar_style, 0);
    lv_style_set_pad_bottom(&status_bar_style, 0);
    lv_style_set_text_color(&status_bar_style, EF_FG);
    lv_style_set_text_font(&status_bar_style, &lv_font_rubik_12);
    lv_style_set_radius(&status_bar_style, 0);

    /* Status chip pills: rgba(46,56,60,.55) bg, 1px border, radius 999 */
    lv_style_init(&status_chip_style);
    lv_style_set_bg_color(&status_chip_style, lv_color_hex(0x2e383c));
    lv_style_set_bg_opa(&status_chip_style, 140); /* 55% */
    lv_style_set_border_width(&status_chip_style, 1);
    lv_style_set_border_color(&status_chip_style, EF_FG);
    lv_style_set_border_opa(&status_chip_style, 20); /* 8% */
    lv_style_set_radius(&status_chip_style, LV_RADIUS_CIRCLE);
    lv_style_set_pad_left(&status_chip_style, 10);
    lv_style_set_pad_right(&status_chip_style, 10);
    lv_style_set_pad_top(&status_chip_style, 6);
    lv_style_set_pad_bottom(&status_chip_style, 6);
    lv_style_set_pad_column(&status_chip_style, 8);
    lv_style_set_text_font(&status_chip_style, &lv_font_rubik_12);
    lv_style_set_text_color(&status_chip_style, EF_FG);

    /* ── Tab bar pills ── */

    /* Active tab: accent bg tint, accent text, accent border */
    lv_style_init(&tab_active_style);
    lv_style_set_bg_color(&tab_active_style, dash_accent_color);
    lv_style_set_bg_opa(&tab_active_style, 31); /* ~12% */
    lv_style_set_border_width(&tab_active_style, 1);
    lv_style_set_border_color(&tab_active_style, dash_accent_color);
    lv_style_set_border_opa(&tab_active_style, 77); /* ~30% */
    lv_style_set_radius(&tab_active_style, LV_RADIUS_CIRCLE);
    lv_style_set_pad_left(&tab_active_style, 18);
    lv_style_set_pad_right(&tab_active_style, 18);
    lv_style_set_pad_top(&tab_active_style, 8);
    lv_style_set_pad_bottom(&tab_active_style, 8);
    lv_style_set_text_color(&tab_active_style, dash_accent_color);
    lv_style_set_text_font(&tab_active_style, &lv_font_rubik_14);

    /* Inactive tab: muted text, no bg */
    lv_style_init(&tab_inactive_style);
    lv_style_set_bg_opa(&tab_inactive_style, LV_OPA_TRANSP);
    lv_style_set_border_width(&tab_inactive_style, 0);
    lv_style_set_radius(&tab_inactive_style, LV_RADIUS_CIRCLE);
    lv_style_set_pad_left(&tab_inactive_style, 18);
    lv_style_set_pad_right(&tab_inactive_style, 18);
    lv_style_set_pad_top(&tab_inactive_style, 8);
    lv_style_set_pad_bottom(&tab_inactive_style, 8);
    lv_style_set_text_color(&tab_inactive_style, EF_FG_MUTED);
    lv_style_set_text_font(&tab_inactive_style, &lv_font_rubik_14);

    /* Focused tab (during tab navigation): same as active but brighter */
    lv_style_init(&tab_focused_style);
    lv_style_set_bg_color(&tab_focused_style, dash_accent_color);
    lv_style_set_bg_opa(&tab_focused_style, 51); /* ~20% */
    lv_style_set_border_width(&tab_focused_style, 1);
    lv_style_set_border_color(&tab_focused_style, dash_accent_color);
    lv_style_set_border_opa(&tab_focused_style, 128); /* ~50% */
    lv_style_set_radius(&tab_focused_style, LV_RADIUS_CIRCLE);
    lv_style_set_pad_left(&tab_focused_style, 18);
    lv_style_set_pad_right(&tab_focused_style, 18);
    lv_style_set_pad_top(&tab_focused_style, 8);
    lv_style_set_pad_bottom(&tab_focused_style, 8);
    lv_style_set_text_color(&tab_focused_style, EF_FG);
    lv_style_set_text_font(&tab_focused_style, &lv_font_rubik_14);

    /* ── Horizontal rail tiles ── */

    /* Default tile: 230x320, radius 14, visible bg with border for contrast */
    lv_style_init(&rail_tile_style);
    lv_style_set_radius(&rail_tile_style, 14);
    lv_style_set_bg_color(&rail_tile_style, EF_BG2);    /* #374145 — lighter than bg for contrast */
    lv_style_set_bg_opa(&rail_tile_style, LV_OPA_COVER);
    lv_style_set_border_width(&rail_tile_style, 1);
    lv_style_set_border_color(&rail_tile_style, EF_FG);
    lv_style_set_border_opa(&rail_tile_style, 51);       /* ~20% — visible border */
    lv_style_set_pad_all(&rail_tile_style, 0);
    lv_style_set_clip_corner(&rail_tile_style, true);
    lv_style_set_shadow_width(&rail_tile_style, 20);
    lv_style_set_shadow_opa(&rail_tile_style, 102);      /* 40% */
    lv_style_set_shadow_ofs_y(&rail_tile_style, 10);
    lv_style_set_shadow_color(&rail_tile_style, lv_color_black());

    /* Focused tile: scale 1.05, full opa, accent border 3px, accent glow shadow */
    lv_style_init(&rail_tile_focused_style);
    lv_style_set_border_width(&rail_tile_focused_style, 3);
    lv_style_set_border_color(&rail_tile_focused_style, dash_accent_color);
    lv_style_set_border_opa(&rail_tile_focused_style, LV_OPA_COVER);
    lv_style_set_shadow_width(&rail_tile_focused_style, 60);
    lv_style_set_shadow_spread(&rail_tile_focused_style, 0);
    lv_style_set_shadow_ofs_y(&rail_tile_focused_style, 30);
    lv_style_set_shadow_color(&rail_tile_focused_style, lv_color_black());
    lv_style_set_shadow_opa(&rail_tile_focused_style, 140); /* 55% */
    lv_style_set_outline_width(&rail_tile_focused_style, 0);

    /* ── Controls bar (bottom 52px) ── */

    lv_style_init(&controls_bar_style);
    lv_style_set_bg_color(&controls_bar_style, lv_color_black());
    lv_style_set_bg_opa(&controls_bar_style, 140); /* 55% */
    lv_style_set_bg_grad_color(&controls_bar_style, lv_color_black());
    lv_style_set_bg_grad_dir(&controls_bar_style, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&controls_bar_style, 0);
    lv_style_set_radius(&controls_bar_style, 0);
    lv_style_set_pad_left(&controls_bar_style, 24);
    lv_style_set_pad_right(&controls_bar_style, 24);
    lv_style_set_text_color(&controls_bar_style, EF_FG);
    lv_style_set_text_font(&controls_bar_style, &lv_font_rubik_14);

    /* ── Overlay scrim + card ── */

    /* Scrim: rgba(11,13,14,.55) → opa 140, black */
    lv_style_init(&overlay_scrim_style);
    lv_style_set_bg_color(&overlay_scrim_style, lv_color_hex(0x0b0d0e));
    lv_style_set_bg_opa(&overlay_scrim_style, 140);
    lv_style_set_border_width(&overlay_scrim_style, 0);
    lv_style_set_radius(&overlay_scrim_style, 0);

    /* Card: rgba(39,46,51,.92) bg, radius 18, 1px border rgba(d3c6aa,.12), heavy shadow */
    lv_style_init(&overlay_card_style);
    lv_style_set_bg_color(&overlay_card_style, lv_color_hex(0x272e33));
    lv_style_set_bg_opa(&overlay_card_style, 234); /* 92% */
    lv_style_set_border_width(&overlay_card_style, 1);
    lv_style_set_border_color(&overlay_card_style, EF_FG);
    lv_style_set_border_opa(&overlay_card_style, 31); /* 12% */
    lv_style_set_radius(&overlay_card_style, 18);
    lv_style_set_shadow_width(&overlay_card_style, 80);
    lv_style_set_shadow_ofs_y(&overlay_card_style, 40);
    lv_style_set_shadow_color(&overlay_card_style, lv_color_black());
    lv_style_set_shadow_opa(&overlay_card_style, 153); /* 60% */
    lv_style_set_clip_corner(&overlay_card_style, true);
    lv_style_set_text_color(&overlay_card_style, EF_FG);
    lv_style_set_text_font(&overlay_card_style, &lv_font_rubik_16);

    /* Menu item: padding 14x16, radius 10 */
    lv_style_init(&overlay_item_style);
    lv_style_set_bg_opa(&overlay_item_style, LV_OPA_TRANSP);
    lv_style_set_border_width(&overlay_item_style, 0);
    lv_style_set_radius(&overlay_item_style, 10);
    lv_style_set_pad_left(&overlay_item_style, 16);
    lv_style_set_pad_right(&overlay_item_style, 16);
    lv_style_set_pad_top(&overlay_item_style, 14);
    lv_style_set_pad_bottom(&overlay_item_style, 14);
    lv_style_set_pad_column(&overlay_item_style, 14);
    lv_style_set_text_color(&overlay_item_style, EF_FG);
    lv_style_set_text_font(&overlay_item_style, &lv_font_rubik_16);

    /* Selected menu item: accent gradient bg, white text, translateX(4px) */
    lv_style_init(&overlay_item_focused_style);
    lv_style_set_bg_color(&overlay_item_focused_style, dash_accent_color);
    lv_style_set_bg_opa(&overlay_item_focused_style, 46); /* ~18% */
    lv_style_set_radius(&overlay_item_focused_style, 10);
    lv_style_set_text_color(&overlay_item_focused_style, lv_color_white());
    lv_style_set_border_width(&overlay_item_focused_style, 0);

    /* ── Two-pane panel ── */

    /* Panel: same as overlay_card but with grid layout inset */
    lv_style_init(&panel_style);
    lv_style_set_bg_color(&panel_style, lv_color_hex(0x272e33));
    lv_style_set_bg_opa(&panel_style, 234);
    lv_style_set_border_width(&panel_style, 1);
    lv_style_set_border_color(&panel_style, EF_FG);
    lv_style_set_border_opa(&panel_style, 31);
    lv_style_set_radius(&panel_style, 18);
    lv_style_set_shadow_width(&panel_style, 80);
    lv_style_set_shadow_ofs_y(&panel_style, 40);
    lv_style_set_shadow_color(&panel_style, lv_color_black());
    lv_style_set_shadow_opa(&panel_style, 128); /* 50% */
    lv_style_set_clip_corner(&panel_style, true);
    lv_style_set_pad_all(&panel_style, 0);
    lv_style_set_text_color(&panel_style, EF_FG);

    /* Panel left nav column: border-right, padding */
    lv_style_init(&panel_nav_style);
    lv_style_set_bg_opa(&panel_nav_style, LV_OPA_TRANSP);
    lv_style_set_border_width(&panel_nav_style, 0);
    lv_style_set_border_side(&panel_nav_style, LV_BORDER_SIDE_RIGHT);
    lv_style_set_pad_left(&panel_nav_style, 14);
    lv_style_set_pad_right(&panel_nav_style, 14);
    lv_style_set_pad_top(&panel_nav_style, 20);
    lv_style_set_pad_bottom(&panel_nav_style, 20);
    lv_style_set_pad_row(&panel_nav_style, 2);
    lv_style_set_text_color(&panel_nav_style, EF_FG_MUTED);
    lv_style_set_text_font(&panel_nav_style, &lv_font_rubik_14);

    /* Nav item: padding 10x12, radius 8 */
    lv_style_init(&panel_nav_item_style);
    lv_style_set_bg_opa(&panel_nav_item_style, LV_OPA_TRANSP);
    lv_style_set_border_width(&panel_nav_item_style, 0);
    lv_style_set_radius(&panel_nav_item_style, 8);
    lv_style_set_pad_left(&panel_nav_item_style, 12);
    lv_style_set_pad_right(&panel_nav_item_style, 12);
    lv_style_set_pad_top(&panel_nav_item_style, 10);
    lv_style_set_pad_bottom(&panel_nav_item_style, 10);
    lv_style_set_pad_column(&panel_nav_item_style, 10);
    lv_style_set_text_color(&panel_nav_item_style, EF_FG_MUTED);

    /* Active nav item: accent bg 14%, accent text */
    lv_style_init(&panel_nav_item_active_style);
    lv_style_set_bg_color(&panel_nav_item_active_style, dash_accent_color);
    lv_style_set_bg_opa(&panel_nav_item_active_style, 36); /* ~14% */
    lv_style_set_radius(&panel_nav_item_active_style, 8);
    lv_style_set_text_color(&panel_nav_item_active_style, dash_accent_color);
    lv_style_set_border_width(&panel_nav_item_active_style, 0);

    /* ── Settings controls ── */

    /* Setting row: flex between, top border */
    lv_style_init(&setting_row_style);
    lv_style_set_bg_opa(&setting_row_style, LV_OPA_TRANSP);
    lv_style_set_border_width(&setting_row_style, 1);
    lv_style_set_border_side(&setting_row_style, LV_BORDER_SIDE_TOP);
    lv_style_set_border_color(&setting_row_style, EF_FG);
    lv_style_set_border_opa(&setting_row_style, 15); /* ~6% */
    lv_style_set_pad_top(&setting_row_style, 14);
    lv_style_set_pad_bottom(&setting_row_style, 14);
    lv_style_set_pad_left(&setting_row_style, 0);
    lv_style_set_pad_right(&setting_row_style, 0);
    lv_style_set_radius(&setting_row_style, 0);
    lv_style_set_text_color(&setting_row_style, EF_FG);

    /* Toggle off: 42x24 pill, bg3 */
    lv_style_init(&toggle_off_style);
    lv_style_set_bg_color(&toggle_off_style, EF_BG3);
    lv_style_set_bg_opa(&toggle_off_style, LV_OPA_COVER);
    lv_style_set_radius(&toggle_off_style, LV_RADIUS_CIRCLE);
    lv_style_set_border_width(&toggle_off_style, 0);
    lv_style_set_pad_all(&toggle_off_style, 3);

    /* Toggle on: accent color */
    lv_style_init(&toggle_on_style);
    lv_style_set_bg_color(&toggle_on_style, dash_accent_color);
    lv_style_set_bg_opa(&toggle_on_style, LV_OPA_COVER);
    lv_style_set_radius(&toggle_on_style, LV_RADIUS_CIRCLE);
    lv_style_set_border_width(&toggle_on_style, 0);
    lv_style_set_pad_all(&toggle_on_style, 3);

    /* Toggle thumb: 18x18 white circle */
    lv_style_init(&toggle_thumb_style);
    lv_style_set_bg_color(&toggle_thumb_style, lv_color_white());
    lv_style_set_bg_opa(&toggle_thumb_style, LV_OPA_COVER);
    lv_style_set_radius(&toggle_thumb_style, LV_RADIUS_CIRCLE);
    lv_style_set_border_width(&toggle_thumb_style, 0);

    /* Segmented control track: bg2, radius 8, pad 3 */
    lv_style_init(&seg_control_style);
    lv_style_set_bg_color(&seg_control_style, EF_BG2);
    lv_style_set_bg_opa(&seg_control_style, LV_OPA_COVER);
    lv_style_set_radius(&seg_control_style, 8);
    lv_style_set_pad_all(&seg_control_style, 3);
    lv_style_set_pad_column(&seg_control_style, 0);
    lv_style_set_border_width(&seg_control_style, 0);
    lv_style_set_text_color(&seg_control_style, EF_FG_MUTED);
    lv_style_set_text_font(&seg_control_style, &lv_font_rubik_12);

    /* Segmented active option: bg4, fg text, radius 6 */
    lv_style_init(&seg_active_style);
    lv_style_set_bg_color(&seg_active_style, EF_BG4);
    lv_style_set_bg_opa(&seg_active_style, LV_OPA_COVER);
    lv_style_set_radius(&seg_active_style, 6);
    lv_style_set_text_color(&seg_active_style, EF_FG);
    lv_style_set_border_width(&seg_active_style, 0);

    /* ── System info stat cards ── */

    /* Stat card: rgba(55,65,69,.5) bg, 1px border, radius 14 */
    lv_style_init(&stat_card_style);
    lv_style_set_bg_color(&stat_card_style, lv_color_hex(0x374145));
    lv_style_set_bg_opa(&stat_card_style, 128); /* 50% */
    lv_style_set_border_width(&stat_card_style, 1);
    lv_style_set_border_color(&stat_card_style, EF_FG);
    lv_style_set_border_opa(&stat_card_style, 15); /* ~6% */
    lv_style_set_radius(&stat_card_style, 14);
    lv_style_set_pad_all(&stat_card_style, 18);
    lv_style_set_text_color(&stat_card_style, EF_FG);

    /* Progress bar track: 6px height, rounded, subtle bg */
    lv_style_init(&progress_bar_track_style);
    lv_style_set_bg_color(&progress_bar_track_style, EF_FG);
    lv_style_set_bg_opa(&progress_bar_track_style, 20); /* ~8% */
    lv_style_set_radius(&progress_bar_track_style, 4);
    lv_style_set_border_width(&progress_bar_track_style, 0);

    /* Progress bar fill: green→aqua gradient */
    lv_style_init(&progress_bar_fill_style);
    lv_style_set_bg_color(&progress_bar_fill_style, EF_GREEN);
    lv_style_set_bg_grad_color(&progress_bar_fill_style, EF_AQUA);
    lv_style_set_bg_grad_dir(&progress_bar_fill_style, LV_GRAD_DIR_HOR);
    lv_style_set_bg_opa(&progress_bar_fill_style, LV_OPA_COVER);
    lv_style_set_radius(&progress_bar_fill_style, 4);
    lv_style_set_border_width(&progress_bar_fill_style, 0);

    /* ── File browser rows ── */

    lv_style_init(&file_row_style);
    lv_style_set_bg_opa(&file_row_style, LV_OPA_TRANSP);
    lv_style_set_border_width(&file_row_style, 1);
    lv_style_set_border_side(&file_row_style, LV_BORDER_SIDE_TOP);
    lv_style_set_border_color(&file_row_style, EF_FG);
    lv_style_set_border_opa(&file_row_style, 13); /* ~5% */
    lv_style_set_pad_left(&file_row_style, 14);
    lv_style_set_pad_right(&file_row_style, 14);
    lv_style_set_pad_top(&file_row_style, 9);
    lv_style_set_pad_bottom(&file_row_style, 9);
    lv_style_set_pad_column(&file_row_style, 12);
    lv_style_set_radius(&file_row_style, 0);
    lv_style_set_text_color(&file_row_style, EF_FG);
    lv_style_set_text_font(&file_row_style, &lv_font_rubik_14);

    lv_style_init(&file_row_selected_style);
    lv_style_set_bg_color(&file_row_selected_style, dash_accent_color);
    lv_style_set_bg_opa(&file_row_selected_style, 31); /* ~12% */
    lv_style_set_text_color(&file_row_selected_style, lv_color_white());

    lv_style_init(&file_header_row_style);
    lv_style_set_bg_color(&file_header_row_style, EF_FG);
    lv_style_set_bg_opa(&file_header_row_style, 8); /* ~3% */
    lv_style_set_text_color(&file_header_row_style, EF_FG_MUTED);
    lv_style_set_text_font(&file_header_row_style, &lv_font_rubik_12);

    /* ── Typography styles ── */

    /* Eyebrow: 11px uppercase, tracked, muted (color overridden per-use) */
    lv_style_init(&eyebrow_style);
    lv_style_set_text_font(&eyebrow_style, &lv_font_rubik_12);
    lv_style_set_text_color(&eyebrow_style, EF_FG_MUTED);
    lv_style_set_text_letter_space(&eyebrow_style, 2);
    lv_style_set_bg_opa(&eyebrow_style, LV_OPA_TRANSP);
    lv_style_set_border_width(&eyebrow_style, 0);
    lv_style_set_pad_all(&eyebrow_style, 0);

    /* Meta title: 22px, white */
    lv_style_init(&meta_title_style);
    lv_style_set_text_font(&meta_title_style, &lv_font_rubik_22);
    lv_style_set_text_color(&meta_title_style, lv_color_white());
    lv_style_set_bg_opa(&meta_title_style, LV_OPA_TRANSP);
    lv_style_set_border_width(&meta_title_style, 0);
    lv_style_set_pad_all(&meta_title_style, 0);

    /* Body muted: 13px, muted fg */
    lv_style_init(&body_muted_style);
    lv_style_set_text_font(&body_muted_style, &lv_font_rubik_14);
    lv_style_set_text_color(&body_muted_style, EF_FG_MUTED);
    lv_style_set_bg_opa(&body_muted_style, LV_OPA_TRANSP);
    lv_style_set_border_width(&body_muted_style, 0);
    lv_style_set_pad_all(&body_muted_style, 0);

    /* Mono small: 12px, muted */
    lv_style_init(&mono_small_style);
    lv_style_set_text_font(&mono_small_style, &lv_font_rubik_12);
    lv_style_set_text_color(&mono_small_style, EF_FG_MUTED);
    lv_style_set_bg_opa(&mono_small_style, LV_OPA_TRANSP);
    lv_style_set_border_width(&mono_small_style, 0);
    lv_style_set_pad_all(&mono_small_style, 0);

    /* Meta pill: radius 999, subtle bg+border */
    lv_style_init(&meta_pill_style);
    lv_style_set_bg_color(&meta_pill_style, lv_color_hex(0x374145));
    lv_style_set_bg_opa(&meta_pill_style, 179); /* ~70% */
    lv_style_set_border_width(&meta_pill_style, 1);
    lv_style_set_border_color(&meta_pill_style, EF_FG);
    lv_style_set_border_opa(&meta_pill_style, 20); /* ~8% */
    lv_style_set_radius(&meta_pill_style, LV_RADIUS_CIRCLE);
    lv_style_set_pad_left(&meta_pill_style, 12);
    lv_style_set_pad_right(&meta_pill_style, 12);
    lv_style_set_pad_top(&meta_pill_style, 6);
    lv_style_set_pad_bottom(&meta_pill_style, 6);
    lv_style_set_text_color(&meta_pill_style, EF_FG);
    lv_style_set_text_font(&meta_pill_style, &lv_font_rubik_12);

    lv_obj_mark_layout_as_dirty(lv_scr_act());
}

/* ============================================================
 *  Style cleanup
 * ============================================================ */
void dash_styles_deinit(void)
{
    /* Legacy */
    lv_style_reset(&dash_background_style);
    lv_style_reset(&menu_table_style);
    lv_style_reset(&menu_table_highlight_style);
    lv_style_reset(&menu_table_cell_style);
    lv_style_reset(&object_style);
    lv_style_reset(&titleview_style);
    lv_style_reset(&titleview_image_container_style);
    lv_style_reset(&titleview_image_text_style);
    lv_style_reset(&titleview_header_footer_style);

    /* New */
    lv_style_reset(&status_bar_style);
    lv_style_reset(&status_chip_style);
    lv_style_reset(&tab_active_style);
    lv_style_reset(&tab_inactive_style);
    lv_style_reset(&tab_focused_style);
    lv_style_reset(&rail_tile_style);
    lv_style_reset(&rail_tile_focused_style);
    lv_style_reset(&controls_bar_style);
    lv_style_reset(&overlay_scrim_style);
    lv_style_reset(&overlay_card_style);
    lv_style_reset(&overlay_item_style);
    lv_style_reset(&overlay_item_focused_style);
    lv_style_reset(&panel_style);
    lv_style_reset(&panel_nav_style);
    lv_style_reset(&panel_nav_item_style);
    lv_style_reset(&panel_nav_item_active_style);
    lv_style_reset(&setting_row_style);
    lv_style_reset(&toggle_off_style);
    lv_style_reset(&toggle_on_style);
    lv_style_reset(&toggle_thumb_style);
    lv_style_reset(&seg_control_style);
    lv_style_reset(&seg_active_style);
    lv_style_reset(&stat_card_style);
    lv_style_reset(&progress_bar_track_style);
    lv_style_reset(&progress_bar_fill_style);
    lv_style_reset(&file_row_style);
    lv_style_reset(&file_row_selected_style);
    lv_style_reset(&file_header_row_style);
    lv_style_reset(&eyebrow_style);
    lv_style_reset(&meta_title_style);
    lv_style_reset(&body_muted_style);
    lv_style_reset(&mono_small_style);
    lv_style_reset(&meta_pill_style);
}
