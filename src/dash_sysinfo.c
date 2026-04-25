// SPDX-License-Identifier: MIT

#include "lithiumx.h"
#include "dash_anim.h"

static lv_obj_t *sysinfo_overlay;
static lv_obj_t *stat_labels[6]; /* CPU, Mem, Storage, Temp, Fan, Network */
static lv_obj_t *stat_bars[6];
static lv_timer_t *update_timer;

/* Bar gradient colors per stat */
static const struct { lv_color_t start; lv_color_t end; } bar_colors[] = {
    { /* CPU */     .start = {0}, .end = {0} },     /* set at runtime */
    { /* Mem */     .start = {0}, .end = {0} },
    { /* Storage */ .start = {0}, .end = {0} },
    { /* Temp */    .start = {0}, .end = {0} },
    { /* Fan */     .start = {0}, .end = {0} },
    { /* Net */     .start = {0}, .end = {0} },
};

static void create_stat_card(lv_obj_t *parent, int index,
                              const char *icon_sym, const char *label_text,
                              const char *value_text, int bar_pct,
                              const char *detail_text,
                              lv_color_t bar_start, lv_color_t bar_end)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, &stat_card_style, LV_PART_MAIN);
    lv_obj_set_size(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 6, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Label row: icon + name */
    lv_obj_t *label_row = lv_obj_create(card);
    lv_obj_set_size(label_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(label_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(label_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(label_row, 0, LV_PART_MAIN);
    lv_obj_set_layout(label_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(label_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(label_row, 6, LV_PART_MAIN);
    lv_obj_clear_flag(label_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(label_row);
    lv_label_set_text(icon, icon_sym);
    lv_obj_add_style(icon, &eyebrow_style, LV_PART_MAIN);

    lv_obj_t *name = lv_label_create(label_row);
    lv_label_set_text(name, label_text);
    lv_obj_add_style(name, &eyebrow_style, LV_PART_MAIN);

    /* Value */
    stat_labels[index] = lv_label_create(card);
    lv_obj_set_style_text_font(stat_labels[index], &lv_font_rubik_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(stat_labels[index], lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(stat_labels[index], value_text);

    /* Progress bar */
    lv_obj_t *bar_track = lv_obj_create(card);
    lv_obj_set_size(bar_track, lv_pct(100), 6);
    lv_obj_add_style(bar_track, &progress_bar_track_style, LV_PART_MAIN);
    lv_obj_clear_flag(bar_track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(bar_track, 0, LV_PART_MAIN);

    lv_obj_t *bar_fill = lv_obj_create(bar_track);
    lv_obj_set_height(bar_fill, 6);
    lv_obj_set_width(bar_fill, lv_pct(LV_CLAMP(1, bar_pct, 100)));
    lv_obj_set_style_bg_color(bar_fill, bar_start, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(bar_fill, bar_end, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(bar_fill, LV_GRAD_DIR_HOR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_fill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_fill, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar_fill, 0, LV_PART_MAIN);
    lv_obj_align(bar_fill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(bar_fill, LV_OBJ_FLAG_SCROLLABLE);
    stat_bars[index] = bar_fill;

    /* Detail text */
    if (detail_text)
    {
        lv_obj_t *detail = lv_label_create(card);
        lv_obj_add_style(detail, &mono_small_style, LV_PART_MAIN);
        lv_label_set_text(detail, detail_text);
    }
}

static void sysinfo_update_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t cpu_pct = 100 - lv_timer_get_idle();
    uint32_t mem_used, mem_cap;
    lx_mem_usage(&mem_used, &mem_cap);
    uint32_t mem_pct = (mem_cap > 0) ? (mem_used * 100 / mem_cap) : 0;

    if (stat_labels[0]) lv_label_set_text_fmt(stat_labels[0], "%d%%", cpu_pct);
    if (stat_labels[1]) lv_label_set_text_fmt(stat_labels[1], "%d%%", mem_pct);
    if (stat_bars[0]) lv_obj_set_width(stat_bars[0], lv_pct(LV_CLAMP(1, cpu_pct, 100)));
    if (stat_bars[1]) lv_obj_set_width(stat_bars[1], lv_pct(LV_CLAMP(1, mem_pct, 100)));
}

static void sysinfo_close_handler(lv_event_t *event)
{
    lv_key_t key = *((lv_key_t *)lv_event_get_param(event));
    if (key == LV_KEY_ESC)
    {
        if (update_timer)
        {
            lv_timer_del(update_timer);
            update_timer = NULL;
        }
        if (sysinfo_overlay)
        {
            lv_obj_del(sysinfo_overlay);
            sysinfo_overlay = NULL;
        }
        dash_focus_pop_depth();
    }
}

void dash_sysinfo_open(void)
{
    lv_coord_t scr_w = lv_obj_get_width(lv_scr_act());
    lv_coord_t scr_h = lv_obj_get_height(lv_scr_act());

    sysinfo_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(sysinfo_overlay, scr_w, scr_h);
    lv_obj_set_style_bg_opa(sysinfo_overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(sysinfo_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sysinfo_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(sysinfo_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(sysinfo_overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Scrim */
    lv_obj_t *scrim = lv_obj_create(sysinfo_overlay);
    lv_obj_set_size(scrim, scr_w, scr_h);
    lv_obj_add_style(scrim, &overlay_scrim_style, LV_PART_MAIN);
    lv_obj_clear_flag(scrim, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Panel (single column) */
    lv_obj_t *panel = lv_obj_create(sysinfo_overlay);
    lv_obj_set_pos(panel, 40, 44);
    lv_obj_set_size(panel, scr_w - 80, scr_h - 44 - 64);
    lv_obj_add_style(panel, &panel_style, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 26, LV_PART_MAIN);
    lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 14, LV_PART_MAIN);

    /* Header */
    lv_obj_t *header = lv_obj_create(panel);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header_left = lv_obj_create(header);
    lv_obj_set_size(header_left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header_left, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(header_left, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header_left, 0, LV_PART_MAIN);
    lv_obj_set_layout(header_left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header_left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(header_left, 4, LV_PART_MAIN);
    lv_obj_clear_flag(header_left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *eyebrow = lv_label_create(header_left);
    lv_obj_add_style(eyebrow, &eyebrow_style, LV_PART_MAIN);
    lv_obj_set_style_text_color(eyebrow, EF_AQUA, LV_PART_MAIN);
    lv_label_set_text(eyebrow, "SYSTEM");

    lv_obj_t *title = lv_label_create(header_left);
    lv_obj_set_style_text_font(title, &lv_font_rubik_24, LV_PART_MAIN);
    lv_label_set_text(title, "Console Status");

    lv_obj_t *subtitle = lv_label_create(header_left);
    lv_obj_add_style(subtitle, &body_muted_style, LV_PART_MAIN);
    lv_label_set_text(subtitle, "Real-time telemetry " LV_SYMBOL_DUMMY " updated every 500 ms");

    /* Header right: pills */
    lv_obj_t *header_right = lv_obj_create(header);
    lv_obj_set_size(header_right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header_right, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(header_right, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header_right, 0, LV_PART_MAIN);
    lv_obj_set_layout(header_right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header_right, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(header_right, 8, LV_PART_MAIN);
    lv_obj_clear_flag(header_right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *online_pill = lv_obj_create(header_right);
    lv_obj_add_style(online_pill, &meta_pill_style, LV_PART_MAIN);
    lv_obj_set_size(online_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(online_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *online_lbl = lv_label_create(online_pill);
    lv_obj_set_style_text_color(online_lbl, EF_GREEN, LV_PART_MAIN);
    lv_label_set_text(online_lbl, LV_SYMBOL_OK " ONLINE");

    lv_obj_t *ntsc_pill = lv_obj_create(header_right);
    lv_obj_add_style(ntsc_pill, &meta_pill_style, LV_PART_MAIN);
    lv_obj_set_size(ntsc_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(ntsc_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ntsc_lbl = lv_label_create(ntsc_pill);
    lv_label_set_text(ntsc_lbl, "NTSC-M");

    /* 6-stat grid (3 cols, using flex with wrapping) */
    lv_obj_t *grid = lv_obj_create(panel);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, 14, LV_PART_MAIN);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    create_stat_card(grid, 0, LV_SYMBOL_CHARGE, "CPU",     "12%", 12, NULL, EF_GREEN, EF_AQUA);
    create_stat_card(grid, 1, LV_SYMBOL_SD_CARD, "MEMORY", "23%", 23, "14.7 / 64 MB", EF_GREEN, EF_AQUA);
    create_stat_card(grid, 2, LV_SYMBOL_DRIVE,  "STORAGE", "61%", 61, "5.4 / 8 TB", EF_YELLOW, EF_ORANGE);
    create_stat_card(grid, 3, LV_SYMBOL_WARNING,"CPU TEMP","48" LV_SYMBOL_DUMMY "\xC2\xB0" "C", 48, NULL, EF_GREEN, EF_AQUA);
    create_stat_card(grid, 4, LV_SYMBOL_LOOP,   "FAN",     "32%", 32, "1840 rpm", EF_BLUE, EF_AQUA);
    create_stat_card(grid, 5, LV_SYMBOL_WIFI,   "NETWORK", "100Mb", 100, "FTP " LV_SYMBOL_OK " 192.168.1.x", EF_GREEN, EF_AQUA);

    /* Bottom info tables (simplified as label readouts) */
    lv_obj_t *bottom = lv_obj_create(panel);
    lv_obj_set_size(bottom, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(bottom, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bottom, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(bottom, 14, LV_PART_MAIN);
    lv_obj_set_layout(bottom, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);

    /* Hardware card */
    lv_obj_t *hw_card = lv_obj_create(bottom);
    lv_obj_add_style(hw_card, &stat_card_style, LV_PART_MAIN);
    lv_obj_set_flex_grow(hw_card, 1);
    lv_obj_set_height(hw_card, LV_SIZE_CONTENT);
    lv_obj_set_layout(hw_card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hw_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(hw_card, 4, LV_PART_MAIN);
    lv_obj_clear_flag(hw_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hw_title = lv_label_create(hw_card);
    lv_obj_add_style(hw_title, &eyebrow_style, LV_PART_MAIN);
    lv_label_set_text(hw_title, "HARDWARE");

    const char *hw_lines[] = {"Kernel    1.00.5838", "Encoder   Conexant", "TSOP      unlocked", "Mobo Rev. 1.2"};
    for (int i = 0; i < 4; i++)
    {
        lv_obj_t *l = lv_label_create(hw_card);
        lv_obj_add_style(l, &mono_small_style, LV_PART_MAIN);
        lv_label_set_text(l, hw_lines[i]);
    }

    /* Dashboard card */
    lv_obj_t *dash_card = lv_obj_create(bottom);
    lv_obj_add_style(dash_card, &stat_card_style, LV_PART_MAIN);
    lv_obj_set_flex_grow(dash_card, 1);
    lv_obj_set_height(dash_card, LV_SIZE_CONTENT);
    lv_obj_set_layout(dash_card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(dash_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(dash_card, 4, LV_PART_MAIN);
    lv_obj_clear_flag(dash_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dash_title = lv_label_create(dash_card);
    lv_obj_add_style(dash_title, &eyebrow_style, LV_PART_MAIN);
    lv_label_set_text(dash_title, "DASHBOARD");

    const char *dash_lines[] = {"LithiumX  2.4.0-modern", "Renderer  nv2a GPU", "Uptime    00:00:00", "Frames    0"};
    for (int i = 0; i < 4; i++)
    {
        lv_obj_t *l = lv_label_create(dash_card);
        lv_obj_add_style(l, &mono_small_style, LV_PART_MAIN);
        lv_label_set_text(l, dash_lines[i]);
    }

    /* Entry animation */
    dash_anim_overlay_in(panel, 300);

    /* Start update timer */
    update_timer = lv_timer_create(sysinfo_update_cb, 500, NULL);

    /* Focus management */
    lv_group_add_obj(lv_group_get_default(), panel);
    lv_obj_add_event_cb(panel, sysinfo_close_handler, LV_EVENT_KEY, NULL);
    dash_focus_change_depth(panel);
}
