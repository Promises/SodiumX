// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2022 Ryzee119

#include "lithiumx.h"
#include "dash_anim.h"

/* ============================================================
 *  Settings persistence (read/write via SQLite blob)
 * ============================================================ */
static void dash_settings_set_v3_defaults(void)
{
    dash_settings.accent_index = ACCENT_GREEN;
    dash_settings.show_fps_overlay = false;
    dash_settings.show_controller_hints = true;
    dash_settings.show_clock_chip = true;
    dash_settings.show_network_chip = true;
    dash_settings.show_temp_chip = true;
    dash_settings.animated_background = true;
    dash_settings.backdrop_blur = true;
    dash_settings.tile_parallax = true;
    dash_settings.film_grain = true;
    dash_settings.resolution_mode = 1;
    dash_settings.audio_output = 0;
    dash_settings.ui_sounds = true;
    dash_settings.ftp_enabled = true;
    lv_memset(dash_settings._padding, 0, sizeof(dash_settings._padding));
}

static int dash_settings_read_callback(void *param, int argc, char **argv, char **azColName)
{
    (void)param; (void)azColName;
    assert(argc == 1);
    if (argc == 0) return 0;

    unsigned int magic = *(unsigned int *)argv[0];

    if (magic == DASH_SETTINGS_MAGIC)
    {
        lv_memcpy(&dash_settings, argv[0], sizeof(dash_settings));
    }
    else if (magic == DASH_SETTINGS_MAGIC_V2)
    {
        dash_settings_v2_t old;
        lv_memcpy(&old, argv[0], sizeof(dash_settings_v2_t));

        dash_settings.magic = DASH_SETTINGS_MAGIC;
        dash_settings.use_fahrenheit = old.use_fahrenheit;
        dash_settings.auto_launch_dvd = old.auto_launch_dvd;
        dash_settings.show_debug_info = old.show_debug_info;
        dash_settings.startup_page_index = old.startup_page_index;
        dash_settings.theme_colour = old.theme_colour;
        dash_settings.max_recent_items = old.max_recent_items;
        dash_settings.items_per_row = old.items_per_row;
        lv_memcpy(dash_settings.earliest_recent_date, old.earliest_recent_date,
                   sizeof(dash_settings.earliest_recent_date));
        lv_memcpy(dash_settings.sort_strings, old.sort_strings,
                   sizeof(dash_settings.sort_strings));

        dash_settings_set_v3_defaults();
        dash_settings.show_fps_overlay = old.show_debug_info;
        dash_settings_apply(false);
    }
    return 0;
}

void dash_settings_read()
{
    db_command_with_callback(SQL_SETTINGS_READ, dash_settings_read_callback, NULL);
}

void dash_settings_apply(bool confirm_box)
{
    db_command_with_callback(SQL_SETTINGS_DELETE_ENTRIES, NULL, NULL);
    db_insert_blob(SQL_SETTINGS_INSERT, &dash_settings, sizeof(dash_settings));

    if (confirm_box)
    {
        lv_obj_t *obj = container_open();
        lv_obj_t *label = lv_label_create(obj);
        lv_label_set_text(label, "Setting applied\nReboot to apply some changes");
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
}

/* ============================================================
 *  Two-pane settings panel
 * ============================================================ */
static lv_obj_t *settings_overlay;
static lv_obj_t *panel_body;
static int active_section = 0;
static lv_obj_t *nav_items[6];

typedef enum {
    SECT_DISPLAY = 0,
    SECT_NETWORK,
    SECT_AUDIO,
    SECT_SYSTEM,
    SECT_EEPROM,
    SECT_ABOUT,
    SECT_COUNT
} settings_section_t;

static const char *section_names[] = {"Display", "Network", "Audio", "System", "EEPROM", "About"};
static const char *section_icons[] = {LV_SYMBOL_IMAGE, LV_SYMBOL_WIFI, LV_SYMBOL_VOLUME_MAX,
                                       LV_SYMBOL_SETTINGS, LV_SYMBOL_SD_CARD, LV_SYMBOL_EYE_OPEN};

/* ── Toggle widget ── */
static lv_obj_t *create_toggle(lv_obj_t *parent, bool *value)
{
    lv_obj_t *track = lv_obj_create(parent);
    lv_obj_set_size(track, 42, 24);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);

    if (*value)
        lv_obj_add_style(track, &toggle_on_style, LV_PART_MAIN);
    else
        lv_obj_add_style(track, &toggle_off_style, LV_PART_MAIN);

    lv_obj_t *thumb = lv_obj_create(track);
    lv_obj_set_size(thumb, 18, 18);
    lv_obj_add_style(thumb, &toggle_thumb_style, LV_PART_MAIN);
    lv_obj_clear_flag(thumb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_translate_x(thumb, *value ? 18 : 0, LV_PART_MAIN);

    track->user_data = value;
    return track;
}

/* ── Setting row ── */
static lv_obj_t *create_setting_row(lv_obj_t *parent, const char *label_text,
                                     const char *desc_text, bool first)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_add_style(row, &setting_row_style, LV_PART_MAIN);
    if (first)
    {
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    }
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* Left: label + description */
    lv_obj_t *left = lv_obj_create(row);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(left, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(left, 0, LV_PART_MAIN);
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, 2, LV_PART_MAIN);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(left);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, EF_FG, LV_PART_MAIN);
    lv_label_set_text(lbl, label_text);

    if (desc_text)
    {
        lv_obj_t *desc = lv_label_create(left);
        lv_obj_set_style_text_font(desc, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(desc, EF_FG_MUTED, LV_PART_MAIN);
        lv_obj_set_width(desc, 360);
        lv_label_set_text(desc, desc_text);
        lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    }

    return row;
}

/* ── Readout value ── */
static void create_readout(lv_obj_t *parent, const char *label_text, const char *value_text, bool first)
{
    lv_obj_t *row = create_setting_row(parent, label_text, NULL, first);

    lv_obj_t *val = lv_label_create(row);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(val, EF_FG, LV_PART_MAIN);
    lv_label_set_text(val, value_text);
}

/* ── Section body builders ── */
static void build_display_section(lv_obj_t *body)
{
    lv_obj_t *title = lv_label_create(body);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_label_set_text(title, "Display");
    lv_obj_t *sub = lv_label_create(body);
    lv_obj_add_style(sub, &body_muted_style, LV_PART_MAIN);
    lv_label_set_text(sub, "How LithiumX renders to your TV.");
    lv_obj_set_style_pad_bottom(sub, 16, LV_PART_MAIN);

    /* Animated background */
    lv_obj_t *r1 = create_setting_row(body, "Animated background", "GPU-rendered ambient gradient under the rail.", true);
    create_toggle(r1, &dash_settings.animated_background);

    lv_obj_t *r2 = create_setting_row(body, "Backdrop blur from selection", "Use selected game's boxart as a blurred backdrop.", false);
    create_toggle(r2, &dash_settings.backdrop_blur);

    lv_obj_t *r3 = create_setting_row(body, "Tile parallax", "Subtle depth on the focused tile.", false);
    create_toggle(r3, &dash_settings.tile_parallax);

    lv_obj_t *r4 = create_setting_row(body, "Film grain", "Overlay noise for texture.", false);
    create_toggle(r4, &dash_settings.film_grain);
}

static void build_network_section(lv_obj_t *body)
{
    lv_obj_t *title = lv_label_create(body);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_label_set_text(title, "Network");
    lv_obj_t *sub = lv_label_create(body);
    lv_obj_add_style(sub, &body_muted_style, LV_PART_MAIN);
    lv_label_set_text(sub, "FTP server and IP configuration.");
    lv_obj_set_style_pad_bottom(sub, 16, LV_PART_MAIN);

    lv_obj_t *r1 = create_setting_row(body, "FTP Server", "Port 21 " LV_SYMBOL_DUMMY " user: xbox " LV_SYMBOL_DUMMY " pass: xbox", true);
    create_toggle(r1, &dash_settings.ftp_enabled);

    create_readout(body, "IP Address", "192.168.1.x", false);
    create_readout(body, "Link Speed", "100 Mbps", false);
}

static void build_audio_section(lv_obj_t *body)
{
    lv_obj_t *title = lv_label_create(body);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_label_set_text(title, "Audio");
    lv_obj_t *sub = lv_label_create(body);
    lv_obj_add_style(sub, &body_muted_style, LV_PART_MAIN);
    lv_label_set_text(sub, "UI sounds.");
    lv_obj_set_style_pad_bottom(sub, 16, LV_PART_MAIN);

    lv_obj_t *r1 = create_setting_row(body, "UI sound effects", NULL, true);
    create_toggle(r1, &dash_settings.ui_sounds);
}

static void build_system_section(lv_obj_t *body)
{
    lv_obj_t *title = lv_label_create(body);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_label_set_text(title, "System");
    lv_obj_t *sub = lv_label_create(body);
    lv_obj_add_style(sub, &body_muted_style, LV_PART_MAIN);
    lv_label_set_text(sub, "Dashboard behavior.");
    lv_obj_set_style_pad_bottom(sub, 16, LV_PART_MAIN);

    lv_obj_t *r1 = create_setting_row(body, "Show FPS overlay", "Developer readout in status bar.", true);
    create_toggle(r1, &dash_settings.show_fps_overlay);

    lv_obj_t *r2 = create_setting_row(body, "Controller hints", "Bottom bar A/B/X/Y prompts.", false);
    create_toggle(r2, &dash_settings.show_controller_hints);

    lv_obj_t *r3 = create_setting_row(body, "Autolaunch DVD", NULL, false);
    create_toggle(r3, &dash_settings.auto_launch_dvd);

    lv_obj_t *r4 = create_setting_row(body, "Fahrenheit display", NULL, false);
    create_toggle(r4, &dash_settings.use_fahrenheit);
}

static void build_eeprom_section(lv_obj_t *body)
{
    lv_obj_t *title = lv_label_create(body);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_label_set_text(title, "EEPROM");
    lv_obj_t *sub = lv_label_create(body);
    lv_obj_add_style(sub, &body_muted_style, LV_PART_MAIN);
    lv_label_set_text(sub, "Console hardware identity.");
    lv_obj_set_style_pad_bottom(sub, 16, LV_PART_MAIN);

    create_readout(body, "Serial", "LX-0000000-0000", true);
    create_readout(body, "Region", "NTSC-U", false);
    create_readout(body, "Video Standard", "NTSC-M", false);
}

static void build_about_section(lv_obj_t *body)
{
    lv_obj_t *title = lv_label_create(body);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_label_set_text(title, "About LithiumX");
    lv_obj_t *sub = lv_label_create(body);
    lv_obj_add_style(sub, &body_muted_style, LV_PART_MAIN);
    lv_label_set_text(sub, "A minimal, GPU-accelerated dashboard.");
    lv_obj_set_style_pad_bottom(sub, 16, LV_PART_MAIN);

    create_readout(body, "Version", "2.4.0-modern", true);
    create_readout(body, "Build", "apr-2026 " LV_SYMBOL_DUMMY " nxdk", false);
    create_readout(body, "License", "MIT", false);
}

typedef void (*section_builder_t)(lv_obj_t *body);
static const section_builder_t section_builders[] = {
    build_display_section,
    build_network_section,
    build_audio_section,
    build_system_section,
    build_eeprom_section,
    build_about_section,
};

static void rebuild_body(void)
{
    if (!panel_body) return;
    lv_obj_clean(panel_body);
    if (active_section >= 0 && active_section < SECT_COUNT)
    {
        section_builders[active_section](panel_body);
    }
}

static void update_nav_highlight(void)
{
    for (int i = 0; i < SECT_COUNT; i++)
    {
        lv_obj_remove_style(nav_items[i], &panel_nav_item_active_style, LV_PART_MAIN);
        lv_obj_add_style(nav_items[i], &panel_nav_item_style, LV_PART_MAIN);
    }
    if (active_section >= 0 && active_section < SECT_COUNT)
    {
        lv_obj_remove_style(nav_items[active_section], &panel_nav_item_style, LV_PART_MAIN);
        lv_obj_add_style(nav_items[active_section], &panel_nav_item_active_style, LV_PART_MAIN);
    }
}

/* ── Key handler for the settings panel ── */
static void settings_key_handler(lv_event_t *event)
{
    lv_key_t key = *((lv_key_t *)lv_event_get_param(event));

    if (key == LV_KEY_ESC)
    {
        /* Save settings and close */
        dash_settings_apply(false);
        dash_statusbar_refresh();

        if (settings_overlay)
        {
            lv_obj_del(settings_overlay);
            settings_overlay = NULL;
            panel_body = NULL;
        }
        dash_focus_pop_depth();
        return;
    }

    if (key == LV_KEY_UP)
    {
        active_section = (active_section - 1 + SECT_COUNT) % SECT_COUNT;
        update_nav_highlight();
        rebuild_body();
    }
    else if (key == LV_KEY_DOWN)
    {
        active_section = (active_section + 1) % SECT_COUNT;
        update_nav_highlight();
        rebuild_body();
    }
}

/* ── Open settings panel ── */
void dash_settings_open(void)
{
    active_section = SECT_DISPLAY;

    lv_coord_t scr_w = lv_obj_get_width(lv_scr_act());
    lv_coord_t scr_h = lv_obj_get_height(lv_scr_act());

    /* Fullscreen overlay */
    settings_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(settings_overlay, scr_w, scr_h);
    lv_obj_set_style_bg_opa(settings_overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(settings_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(settings_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(settings_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(settings_overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Scrim */
    lv_obj_t *scrim = lv_obj_create(settings_overlay);
    lv_obj_set_size(scrim, scr_w, scr_h);
    lv_obj_add_style(scrim, &overlay_scrim_style, LV_PART_MAIN);
    lv_obj_clear_flag(scrim, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Panel: inset 44px top, 64px bottom, 40px sides */
    lv_obj_t *panel = lv_obj_create(settings_overlay);
    lv_obj_set_pos(panel, 40, 44);
    lv_obj_set_size(panel, scr_w - 80, scr_h - 44 - 64);
    lv_obj_add_style(panel, &panel_style, LV_PART_MAIN);
    lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Left nav (240px) */
    lv_obj_t *nav = lv_obj_create(panel);
    lv_obj_set_size(nav, 240, lv_pct(100));
    lv_obj_add_style(nav, &panel_nav_style, LV_PART_MAIN);
    lv_obj_set_style_border_width(nav, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(nav, LV_BORDER_SIDE_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_border_color(nav, EF_FG, LV_PART_MAIN);
    lv_obj_set_style_border_opa(nav, 20, LV_PART_MAIN);
    lv_obj_set_layout(nav, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(nav, 2, LV_PART_MAIN);
    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

    /* Nav title */
    lv_obj_t *nav_title = lv_label_create(nav);
    lv_obj_add_style(nav_title, &eyebrow_style, LV_PART_MAIN);
    lv_label_set_text(nav_title, "SETTINGS");
    lv_obj_set_style_pad_bottom(nav_title, 12, LV_PART_MAIN);

    /* Nav items */
    for (int i = 0; i < SECT_COUNT; i++)
    {
        lv_obj_t *item = lv_obj_create(nav);
        lv_obj_set_size(item, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_add_style(item, &panel_nav_item_style, LV_PART_MAIN);
        lv_obj_set_layout(item, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *icon = lv_label_create(item);
        lv_label_set_text(icon, section_icons[i]);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, LV_PART_MAIN);

        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, section_names[i]);

        nav_items[i] = item;
    }
    update_nav_highlight();

    /* Right body (scrollable) */
    panel_body = lv_obj_create(panel);
    lv_obj_set_size(panel_body, LV_SIZE_CONTENT, lv_pct(100));
    lv_obj_set_flex_grow(panel_body, 1);
    lv_obj_set_style_bg_opa(panel_body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel_body, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(panel_body, 30, LV_PART_MAIN);
    lv_obj_set_style_pad_right(panel_body, 30, LV_PART_MAIN);
    lv_obj_set_style_pad_top(panel_body, 26, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(panel_body, 26, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel_body, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(panel_body, 0, LV_PART_MAIN);
    lv_obj_set_layout(panel_body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panel_body, LV_FLEX_FLOW_COLUMN);

    rebuild_body();

    /* Entry animation */
    dash_anim_overlay_in(panel, 300);

    /* Focus management */
    lv_group_add_obj(lv_group_get_default(), panel);
    lv_obj_add_event_cb(panel, settings_key_handler, LV_EVENT_KEY, NULL);
    dash_focus_change_depth(panel);
}
