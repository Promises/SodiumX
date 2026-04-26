// SPDX-License-Identifier: MIT

#include "lithiumx.h"
#include "dash_prerender.h"
#include "dash_pill_data.h"

static lv_obj_t *sb_container;
static lv_obj_t *sb_clock_label;
static lv_obj_t *sb_temp_chip;
static lv_obj_t *sb_net_chip;
static lv_obj_t *sb_clock_chip;
static lv_obj_t *sb_fps_chip;
static lv_obj_t *sb_cpu_chip;
static lv_obj_t *sb_mem_chip;
static lv_obj_t *sb_fps_label;
static lv_obj_t *sb_cpu_label;
static lv_obj_t *sb_mem_label;
static lv_timer_t *sb_clock_timer;

static uint32_t sb_frame_counter;

static void clock_timer_cb(lv_timer_t *t)
{
    (void)t;
    char time_str[20];
    platform_get_iso8601_time(time_str);
    /* time_str is "YYYY-MM-DDTHH MM:SS" — extract HH:MM */
    if (sb_clock_label && strlen(time_str) >= 16)
    {
        char buf[6];
        buf[0] = time_str[11];
        buf[1] = time_str[12];
        buf[2] = ':';
        buf[3] = time_str[14];
        buf[4] = time_str[15];
        buf[5] = '\0';
        lv_label_set_text(sb_clock_label, buf);
    }

    /* Update FPS/CPU/MEM if visible */
    if (sb_fps_label)
    {
        uint32_t fps = sb_frame_counter;
        sb_frame_counter = 0;
        lv_label_set_text_fmt(sb_fps_label, "%d FPS", fps);
    }
    if (sb_cpu_label)
    {
        uint32_t cpu = 100 - lv_timer_get_idle();
        lv_label_set_text_fmt(sb_cpu_label, "%d%% CPU", cpu);
    }
    if (sb_mem_label)
    {
#ifdef NXDK
        extern void get_ram_usage(uint32_t *mem_size, uint32_t *mem_used);
        uint32_t ram_total, ram_used;
        get_ram_usage(&ram_total, &ram_used);
        lv_label_set_text_fmt(sb_mem_label, "%d/%dMB", ram_used, ram_total);
#else
        uint32_t mem_used, mem_cap;
        lx_mem_usage(&mem_used, &mem_cap);
        lv_label_set_text_fmt(sb_mem_label, "%dkB", mem_used / 1024);
#endif
    }
}

static void fps_frame_tick_cb(lv_timer_t *t)
{
    (void)t;
    sb_frame_counter++;
}

/* Create a status chip using a pre-compiled pill image from dash_pill_data.h.
 * Returns the chip container (add labels with FLOATING flag to overlay).
 * If outer_out != NULL, stores the chip for show/hide. */
static lv_obj_t *create_chip(lv_obj_t *parent, const lv_img_dsc_t *pill_img_dsc,
                              lv_obj_t **outer_out)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_set_size(chip, pill_img_dsc->header.w, pill_img_dsc->header.h);
    lv_obj_set_style_bg_opa(chip, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(chip, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(chip, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(chip, 0, LV_PART_MAIN);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *img = lv_img_create(chip);
    lv_img_set_src(img, pill_img_dsc);
    lv_obj_set_pos(img, 0, 0);

    if (outer_out) *outer_out = chip;
    return chip;
}

/* Create a small green status dot (6x6, pre-compiled) */
static lv_obj_t *create_status_dot(lv_obj_t *parent)
{
    lv_obj_t *dot = lv_img_create(parent);
    lv_img_set_src(dot, &pill_status_dot);
    return dot;
}

lv_obj_t *dash_statusbar_create(lv_obj_t *parent)
{
    lv_coord_t w = lv_obj_get_width(parent);

    /* Main status bar container */
    sb_container = lv_obj_create(parent);
    lv_obj_set_size(sb_container, w, 44);
    lv_obj_align(sb_container, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_style(sb_container, &status_bar_style, LV_PART_MAIN);
    lv_obj_set_layout(sb_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(sb_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sb_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(sb_container, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Left group ── */
    lv_obj_t *left = lv_obj_create(sb_container);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(left, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(left, 0, LV_PART_MAIN);
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left, 10, LV_PART_MAIN);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    /* Logo square (26x26, accent gradient) */
    lv_obj_t *logo = lv_obj_create(left);
    lv_obj_set_size(logo, 26, 26);
    lv_obj_set_style_radius(logo, 7, LV_PART_MAIN);
    lv_obj_set_style_bg_color(logo, EF_GREEN, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(logo, EF_AQUA, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(logo, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(logo, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(logo, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(logo, 14, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(logo, EF_GREEN, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(logo, 102, LV_PART_MAIN); /* ~40% */
    lv_obj_clear_flag(logo, LV_OBJ_FLAG_SCROLLABLE);

    /* Inner diamond cutout */
    lv_obj_t *diamond = lv_obj_create(logo);
    lv_obj_set_size(diamond, 10, 10);
    lv_obj_set_style_bg_color(diamond, lv_color_hex(0x0b0d0e), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(diamond, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(diamond, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(diamond, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_angle(diamond, 450, LV_PART_MAIN); /* 45 degrees */
    lv_obj_center(diamond);
    lv_obj_clear_flag(diamond, LV_OBJ_FLAG_SCROLLABLE);

    /* Logo text column */
    lv_obj_t *logo_text = lv_obj_create(left);
    lv_obj_set_size(logo_text, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(logo_text, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(logo_text, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(logo_text, 0, LV_PART_MAIN);
    lv_obj_set_layout(logo_text, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(logo_text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(logo_text, 0, LV_PART_MAIN);
    lv_obj_clear_flag(logo_text, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name_lbl = lv_label_create(logo_text);
    lv_label_set_text(name_lbl, "LithiumX");
    lv_obj_set_style_text_font(name_lbl, &lv_font_jetbrains_mono_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(name_lbl, EF_FG, LV_PART_MAIN);

    lv_obj_t *ver_lbl = lv_label_create(logo_text);
    lv_label_set_text(ver_lbl, "v2.4 " LV_SYMBOL_DUMMY " nv2a");
    lv_obj_set_style_text_font(ver_lbl, &lv_font_jetbrains_mono_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(ver_lbl, EF_FG_MUTED, LV_PART_MAIN);
    lv_obj_set_style_text_opa(ver_lbl, 179, LV_PART_MAIN); /* 70% */

    /* FPS chip */
    sb_fps_chip = create_chip(left, &pill_chip_fps, NULL);
    sb_fps_label = lv_label_create(sb_fps_chip);
    lv_label_set_text(sb_fps_label, "-- FPS");
    lv_obj_set_style_text_font(sb_fps_label, &lv_font_jetbrains_mono_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(sb_fps_label, EF_GREEN, LV_PART_MAIN);
    lv_obj_add_flag(sb_fps_label, LV_OBJ_FLAG_FLOATING);
    lv_obj_center(sb_fps_label);

    /* CPU chip */
    sb_cpu_chip = create_chip(left, &pill_chip_cpu, NULL);
    sb_cpu_label = lv_label_create(sb_cpu_chip);
    lv_label_set_text(sb_cpu_label, "0% CPU");
    lv_obj_set_style_text_font(sb_cpu_label, &lv_font_jetbrains_mono_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(sb_cpu_label, EF_GREEN, LV_PART_MAIN);
    lv_obj_add_flag(sb_cpu_label, LV_OBJ_FLAG_FLOATING);
    lv_obj_center(sb_cpu_label);

    /* MEM chip */
    sb_mem_chip = create_chip(left, &pill_chip_mem, NULL);
    sb_mem_label = lv_label_create(sb_mem_chip);
    lv_label_set_text(sb_mem_label, "0kB");
    lv_obj_set_style_text_font(sb_mem_label, &lv_font_jetbrains_mono_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(sb_mem_label, EF_GREEN, LV_PART_MAIN);
    lv_obj_add_flag(sb_mem_label, LV_OBJ_FLAG_FLOATING);
    lv_obj_center(sb_mem_label);

    /* ── Right group ── */
    lv_obj_t *right = lv_obj_create(sb_container);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(right, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(right, 0, LV_PART_MAIN);
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, 8, LV_PART_MAIN);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    /* Temp chip — TEMP HIDDEN for debugging */
    sb_temp_chip = create_chip(right, &pill_chip_temp, NULL);
    lv_obj_add_flag(sb_temp_chip, LV_OBJ_FLAG_HIDDEN);

    /* Network chip — TEMP HIDDEN for debugging */
    sb_net_chip = create_chip(right, &pill_chip_net, NULL);
    lv_obj_add_flag(sb_net_chip, LV_OBJ_FLAG_HIDDEN);

    /* Clock chip */
    sb_clock_chip = create_chip(right, &pill_chip_clock, NULL);
    sb_clock_label = lv_label_create(sb_clock_chip);
    lv_label_set_text(sb_clock_label, "00:00");
    lv_obj_set_style_text_font(sb_clock_label, &lv_font_jetbrains_mono_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(sb_clock_label, EF_FG, LV_PART_MAIN);
    lv_obj_add_flag(sb_clock_label, LV_OBJ_FLAG_FLOATING);
    lv_obj_center(sb_clock_label);

    /* Start timers */
    sb_clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    lv_timer_create(fps_frame_tick_cb, LV_DISP_DEF_REFR_PERIOD, NULL);

    /* Trigger immediate clock update */
    clock_timer_cb(NULL);

    /* Apply visibility settings */
    dash_statusbar_refresh();

    return sb_container;
}

void dash_statusbar_refresh(void)
{
    if (!sb_container) return;

    if (sb_temp_chip)
    {
        dash_settings.show_temp_chip ? lv_obj_clear_flag(sb_temp_chip, LV_OBJ_FLAG_HIDDEN)
                                     : lv_obj_add_flag(sb_temp_chip, LV_OBJ_FLAG_HIDDEN);
    }
    if (sb_net_chip)
    {
        dash_settings.show_network_chip ? lv_obj_clear_flag(sb_net_chip, LV_OBJ_FLAG_HIDDEN)
                                        : lv_obj_add_flag(sb_net_chip, LV_OBJ_FLAG_HIDDEN);
    }
    if (sb_clock_chip)
    {
        dash_settings.show_clock_chip ? lv_obj_clear_flag(sb_clock_chip, LV_OBJ_FLAG_HIDDEN)
                                      : lv_obj_add_flag(sb_clock_chip, LV_OBJ_FLAG_HIDDEN);
    }
    if (sb_fps_chip)
    {
        dash_settings.show_fps_overlay ? lv_obj_clear_flag(sb_fps_chip, LV_OBJ_FLAG_HIDDEN)
                                       : lv_obj_add_flag(sb_fps_chip, LV_OBJ_FLAG_HIDDEN);
    }
    if (sb_cpu_chip)
    {
        dash_settings.show_fps_overlay ? lv_obj_clear_flag(sb_cpu_chip, LV_OBJ_FLAG_HIDDEN)
                                       : lv_obj_add_flag(sb_cpu_chip, LV_OBJ_FLAG_HIDDEN);
    }
    if (sb_mem_chip)
    {
        dash_settings.show_fps_overlay ? lv_obj_clear_flag(sb_mem_chip, LV_OBJ_FLAG_HIDDEN)
                                       : lv_obj_add_flag(sb_mem_chip, LV_OBJ_FLAG_HIDDEN);
    }
}
