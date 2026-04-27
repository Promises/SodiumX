// SPDX-License-Identifier: MIT
// New settings panel — built on dash_panel for proper two-pane focus.
// Migrates sections from dash_settings.c one at a time.

#include "lithiumx.h"
#include "dash_panel.h"
#include "dash_backup.h"
#include "dash_pill_data.h"

#ifdef NXDK
#include "platform/xbox/xbox_info.h"
#endif

/* ══════════════════════════════════════════════════════════════════
 *  Helpers (shared with old settings — same create_setting_row etc.)
 * ══════════════════════════════════════════════════════════════════ */
static lv_obj_t *create_setting_row_new(lv_obj_t *parent, const char *label_text,
                                         const char *desc_text, bool first)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_add_style(row, &setting_row_style, LV_PART_MAIN);
    if (first)
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

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
    lv_obj_set_style_text_font(lbl, &lv_font_rubik_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, EF_FG, LV_PART_MAIN);
    lv_label_set_text(lbl, label_text);

    if (desc_text) {
        lv_obj_t *desc = lv_label_create(left);
        lv_obj_set_style_text_font(desc, &lv_font_rubik_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(desc, EF_FG_MUTED, LV_PART_MAIN);
        lv_obj_set_width(desc, 360);
        lv_label_set_text(desc, desc_text);
        lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    }

    return row;
}

static void create_readout_new(lv_obj_t *parent, const char *label_text,
                                const char *value_text, bool first)
{
    lv_obj_t *row = create_setting_row_new(parent, label_text, NULL, first);
    lv_obj_t *val = lv_label_create(row);
    lv_obj_set_style_text_font(val, &lv_font_rubik_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(val, EF_FG, LV_PART_MAIN);
    lv_label_set_text(val, value_text);
}

static void section_header(lv_obj_t *body, const char *title_text, const char *sub_text)
{
    lv_obj_t *title = lv_label_create(body);
    lv_obj_set_style_text_font(title, &lv_font_rubik_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, EF_FG, LV_PART_MAIN);
    lv_label_set_text(title, title_text);

    lv_obj_t *sub = lv_label_create(body);
    lv_obj_add_style(sub, &body_muted_style, LV_PART_MAIN);
    lv_label_set_text(sub, sub_text);
    lv_obj_set_style_pad_bottom(sub, 16, LV_PART_MAIN);
}

/* ══════════════════════════════════════════════════════════════════
 *  Toggle row system — reusable for any section with bool toggles
 * ══════════════════════════════════════════════════════════════════ */
#define MAX_TOGGLE_ROWS 10

typedef struct {
    bool *value;
    lv_obj_t *track_img;
    lv_obj_t *thumb_img;
    lv_obj_t *row;
} toggle_row_t;

static toggle_row_t toggle_rows[MAX_TOGGLE_ROWS];
static int toggle_row_count = 0;
static int toggle_row_selected = 0;

static void create_toggle_row(lv_obj_t *parent, const char *label_text,
                               const char *desc_text, bool *value, bool first)
{
    if (toggle_row_count >= MAX_TOGGLE_ROWS) return;

    lv_obj_t *row = create_setting_row_new(parent, label_text, desc_text, first);

    /* Toggle widget: track image + thumb image */
    /* Toggle: 48x27 track, 19px thumb (29px canvas w/ shadow), 4px inset */
    lv_obj_t *track_container = lv_obj_create(row);
    lv_obj_set_size(track_container, 48, 27);
    lv_obj_set_style_bg_opa(track_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(track_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(track_container, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(track_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(track_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *track = lv_img_create(track_container);
    lv_img_set_src(track, *value ? &pill_toggle_on : &pill_toggle_off);
    lv_obj_set_pos(track, 0, 0);

    /* Thumb: 29px canvas (19px circle + 5px shadow pad), offset to center in 27px track */
    lv_obj_t *thumb = lv_img_create(track_container);
    lv_img_set_src(thumb, *value ? &pill_toggle_thumb_on : &pill_toggle_thumb_off);
    lv_obj_set_pos(thumb, *value ? 20 : -1, -1);

    toggle_rows[toggle_row_count].value = value;
    toggle_rows[toggle_row_count].track_img = track;
    toggle_rows[toggle_row_count].thumb_img = thumb;
    toggle_rows[toggle_row_count].row = row;
    toggle_row_count++;
}

static void update_toggle_visuals(void)
{
    bool right_active = dash_panel_is_open() && !dash_panel_is_nav_focused();

    for (int i = 0; i < toggle_row_count; i++) {
        toggle_row_t *t = &toggle_rows[i];
        bool focused = right_active && (i == toggle_row_selected);
        bool on = *t->value;

        if (focused) {
            lv_img_set_src(t->track_img, on ? &pill_toggle_on_focus : &pill_toggle_off_focus);
        } else {
            lv_img_set_src(t->track_img, on ? &pill_toggle_on : &pill_toggle_off);
        }
        lv_img_set_src(t->thumb_img, on ? &pill_toggle_thumb_on : &pill_toggle_thumb_off);
        lv_obj_set_pos(t->thumb_img, on ? 20 : -1, -1);
    }
}

static void toggle_current(void)
{
    if (toggle_row_selected < 0 || toggle_row_selected >= toggle_row_count) return;
    toggle_row_t *t = &toggle_rows[toggle_row_selected];
    *t->value = !*t->value;
    update_toggle_visuals();
}

static bool toggles_on_key(lv_key_t key)
{
    /* Always update visuals on focus change */
    update_toggle_visuals();

    if (toggle_row_count == 0) return false;

    if (key == LV_KEY_UP) {
        toggle_row_selected = (toggle_row_selected - 1 + toggle_row_count) % toggle_row_count;
        update_toggle_visuals();
        if (toggle_rows[toggle_row_selected].row)
            lv_obj_scroll_to_view(toggle_rows[toggle_row_selected].row, LV_ANIM_ON);
        return true;
    }
    if (key == LV_KEY_DOWN) {
        toggle_row_selected = (toggle_row_selected + 1) % toggle_row_count;
        update_toggle_visuals();
        if (toggle_rows[toggle_row_selected].row)
            lv_obj_scroll_to_view(toggle_rows[toggle_row_selected].row, LV_ANIM_ON);
        return true;
    }
    if (key == LV_KEY_ENTER) {
        toggle_current();
        return true;
    }
    return false;
}

static void reset_toggle_rows(void)
{
    toggle_row_count = 0;
    toggle_row_selected = 0;
}

/* ══════════════════════════════════════════════════════════════════
 *  Section: Display
 * ══════════════════════════════════════════════════════════════════ */
static void build_display(lv_obj_t *body)
{
    section_header(body, "Display", "How LithiumX renders to your TV.");
    reset_toggle_rows();

    create_toggle_row(body, "Animated background",
        "GPU-rendered ambient gradient under the rail.",
        &dash_settings.animated_background, true);
    create_toggle_row(body, "Backdrop blur",
        "Use selected game's boxart as a blurred backdrop.",
        &dash_settings.backdrop_blur, false);
    create_toggle_row(body, "Tile parallax",
        "Subtle depth on the focused tile.",
        &dash_settings.tile_parallax, false);
    create_toggle_row(body, "Film grain",
        "Overlay noise for texture.",
        &dash_settings.film_grain, false);
}

/* ══════════════════════════════════════════════════════════════════
 *  Section: Network (real data from platform)
 * ══════════════════════════════════════════════════════════════════ */
static void build_network(lv_obj_t *body)
{
    section_header(body, "Network", "FTP server and IP configuration.");
    reset_toggle_rows();

    create_toggle_row(body, "FTP Server",
        "Port 21 " LV_SYMBOL_DUMMY " user: xbox " LV_SYMBOL_DUMMY " pass: xbox",
        &dash_settings.ftp_enabled, true);

#ifdef NXDK
    create_readout_new(body, "IP Address", xbox_get_ip_address(), false);
#else
    create_readout_new(body, "IP Address", "127.0.0.1", false);
#endif

    create_readout_new(body, "Link Speed", "100 Mbps", false);
}

/* ══════════════════════════════════════════════════════════════════
 *  Section: Audio (view-only for now)
 * ══════════════════════════════════════════════════════════════════ */
static void build_audio(lv_obj_t *body)
{
    section_header(body, "Audio", "Sound configuration.");
    reset_toggle_rows();

    create_toggle_row(body, "UI sound effects", NULL,
        &dash_settings.ui_sounds, true);
}

/* ══════════════════════════════════════════════════════════════════
 *  Section: System (view-only for now)
 * ══════════════════════════════════════════════════════════════════ */
static void build_system(lv_obj_t *body)
{
    section_header(body, "System", "Dashboard behavior.");
    reset_toggle_rows();

    /* Default page is a readout for now (cycle needs more work) */
    const char *page_name = dash_scroller_get_title(dash_settings.startup_page_index);
    create_readout_new(body, "Default page", page_name ? page_name : "N/A", true);

    create_toggle_row(body, "FPS overlay",
        "Developer readout in status bar.",
        &dash_settings.show_fps_overlay, false);
    create_toggle_row(body, "Controller hints",
        "Bottom bar A/B/X/Y prompts.",
        &dash_settings.show_controller_hints, false);
    create_toggle_row(body, "Autolaunch DVD", NULL,
        &dash_settings.auto_launch_dvd, false);
    create_toggle_row(body, "Fahrenheit display", NULL,
        &dash_settings.use_fahrenheit, false);
}

/* ══════════════════════════════════════════════════════════════════
 *  Section: Backup
 * ══════════════════════════════════════════════════════════════════ */
static void build_backup(lv_obj_t *body)
{
    section_header(body, "Save Backup", "Back up game saves to a remote server.");
    reset_toggle_rows();

    if (dash_settings.backup_server[0])
        create_readout_new(body, "Server", dash_settings.backup_server, true);
    else
        create_readout_new(body, "Server", "Not configured", true);

    char port_str[8];
    lv_snprintf(port_str, sizeof(port_str), "%d", dash_settings.backup_port);
    create_readout_new(body, "Port", port_str, false);

    create_toggle_row(body, "Backup on startup",
        "Run backup when dashboard starts.",
        &dash_settings.backup_on_start, false);
    create_toggle_row(body, "Backup before launch",
        "Back up saves before launching a game.",
        &dash_settings.backup_before_launch, false);

    const char *status = dash_backup_get_status();
    create_readout_new(body, "Last backup", dash_backup_get_last_time(), false);
    create_readout_new(body, "Status", status ? status : "Never run", false);
}

/* ══════════════════════════════════════════════════════════════════
 *  Section: Hardware (real data — replaces dummy EEPROM section)
 * ══════════════════════════════════════════════════════════════════ */
static void build_hardware(lv_obj_t *body)
{
    section_header(body, "Hardware", "Console hardware information.");

#ifdef NXDK
    ULONG type = 0;

    /* Kernel version */
    char kernel_str[32];
    lv_snprintf(kernel_str, sizeof(kernel_str), "%d.%d.%d.%d",
                XboxKrnlVersion.Major, XboxKrnlVersion.Minor,
                XboxKrnlVersion.Build, XboxKrnlVersion.Qfe);
    create_readout_new(body, "Kernel", kernel_str, true);

    /* Console version */
    create_readout_new(body, "Console Version", xbox_get_verion(), false);

    /* Video encoder */
    create_readout_new(body, "Video Encoder", get_encoder_str(), false);

    /* Video region */
    ULONG av_region = 0;
    ExQueryNonVolatileSetting(XC_FACTORY_AV_REGION, &type, &av_region, sizeof(av_region), NULL);
    create_readout_new(body, "Video Region", video_region_str(av_region), false);

    /* Game region */
    ULONG game_region = 0;
    ExQueryNonVolatileSetting(XC_FACTORY_GAME_REGION, &type, &game_region, sizeof(game_region), NULL);
    create_readout_new(body, "Game Region", game_region_str(game_region), false);

    /* Serial */
    char serial[16] = {0};
    ExQueryNonVolatileSetting(XC_FACTORY_SERIAL_NUMBER, &type, serial, sizeof(serial), NULL);
    serial[sizeof(serial) - 1] = '\0';
    create_readout_new(body, "Serial", serial, false);

    /* MAC */
    UCHAR mac[6] = {0};
    ExQueryNonVolatileSetting(XC_FACTORY_ETHERNET_ADDR, &type, mac, sizeof(mac), NULL);
    char mac_str[20];
    lv_snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    create_readout_new(body, "MAC Address", mac_str, false);
#else
    create_readout_new(body, "Kernel", "N/A (desktop)", true);
    create_readout_new(body, "Console Version", "N/A", false);
    create_readout_new(body, "Video Encoder", "N/A", false);
    create_readout_new(body, "Video Region", "N/A", false);
    create_readout_new(body, "Game Region", "N/A", false);
    create_readout_new(body, "Serial", "N/A", false);
    create_readout_new(body, "MAC Address", "N/A", false);
#endif
}

/* ══════════════════════════════════════════════════════════════════
 *  Section: About
 * ══════════════════════════════════════════════════════════════════ */
static void build_about(lv_obj_t *body)
{
    section_header(body, "About LithiumX", "A minimal, GPU-accelerated dashboard.");

    create_readout_new(body, "Version", "2.4.0-modern", true);
#ifdef NXDK
    create_readout_new(body, "Renderer", "nv2a GPU (XGU)", false);
#else
    create_readout_new(body, "Renderer", "SDL2 Software", false);
#endif
    create_readout_new(body, "UI Framework", "LVGL v8", false);
    create_readout_new(body, "License", "MIT", false);
}

/* ══════════════════════════════════════════════════════════════════
 *  Panel configuration
 * ══════════════════════════════════════════════════════════════════ */
static void settings_on_close(void)
{
    /* Save settings when panel closes */
    dash_settings_apply(false);
    dash_statusbar_refresh();
}

static const dash_panel_section_t settings_sections[] = {
    { "Display",   LV_SYMBOL_IMAGE,      build_display,  toggles_on_key, false },
    { "Network",   LV_SYMBOL_WIFI,       build_network,  toggles_on_key, false },
    { "Audio",     LV_SYMBOL_VOLUME_MAX, build_audio,    toggles_on_key, false },
    { "System",    LV_SYMBOL_SETTINGS,   build_system,   toggles_on_key, false },
    { "Backup",    LV_SYMBOL_UPLOAD,     build_backup,   toggles_on_key, false },
    { "Hardware",  LV_SYMBOL_SD_CARD,    build_hardware, NULL, true },
    { "About",     LV_SYMBOL_EYE_OPEN,   build_about,    NULL, true },
};

void dash_settings_new_open(void)
{
    static dash_panel_config_t cfg = {
        .nav_title = "SETTINGS",
        .nav_subtitle = NULL,
        .sections = settings_sections,
        .section_count = 7,
        .on_close = settings_on_close,
    };
    dash_panel_open(&cfg);
}
