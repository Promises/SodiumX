// SPDX-License-Identifier: MIT
// New settings panel — built on dash_panel for proper two-pane focus.
// Migrates sections from dash_settings.c one at a time.

#include "sodiumx.h"
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
    lv_obj_set_style_text_font(lbl, &dash_font_ui_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, EF_FG, LV_PART_MAIN);
    lv_label_set_text(lbl, label_text);

    if (desc_text) {
        lv_obj_t *desc = lv_label_create(left);
        lv_obj_set_style_text_font(desc, &dash_font_ui_12, LV_PART_MAIN);
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
    lv_obj_set_style_text_font(val, &dash_font_ui_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(val, EF_FG, LV_PART_MAIN);
    lv_label_set_text(val, value_text);
}

static void section_header(lv_obj_t *body, const char *title_text, const char *sub_text)
{
    lv_obj_t *title = lv_label_create(body);
    lv_obj_set_style_text_font(title, &dash_font_ui_24, LV_PART_MAIN);
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
#define MAX_TOGGLE_ROWS 16

typedef enum {
    ROW_TOGGLE,
    ROW_TEXT,
    ROW_ACTION,
} setting_row_type_t;

typedef struct {
    setting_row_type_t type;
    const char *label;
    lv_obj_t *row;
    /* Toggle fields */
    bool *value;
    lv_obj_t *track_img;
    lv_obj_t *thumb_img;
    /* Text field */
    char *text_buf;
    int text_buf_size;
    lv_obj_t *text_label;
    /* Callbacks */
    void (*on_change)(void);   /* called after toggle or action */
    int keyboard_mode;         /* DASH_KB_MODE_FULL or _NUMERIC for text rows */
} setting_row_t;

static setting_row_t toggle_rows[MAX_TOGGLE_ROWS];
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

    toggle_rows[toggle_row_count].type = ROW_TOGGLE;
    toggle_rows[toggle_row_count].label = label_text;
    toggle_rows[toggle_row_count].value = value;
    toggle_rows[toggle_row_count].track_img = track;
    toggle_rows[toggle_row_count].thumb_img = thumb;
    toggle_rows[toggle_row_count].row = row;
    toggle_rows[toggle_row_count].text_buf = NULL;
    toggle_rows[toggle_row_count].text_label = NULL;
    toggle_rows[toggle_row_count].on_change = NULL;
    toggle_rows[toggle_row_count].keyboard_mode = DASH_KB_MODE_FULL;
    toggle_row_count++;
}

/* Toggle with on_change callback */
static void create_toggle_row_cb(lv_obj_t *parent, const char *label_text,
                                  const char *desc_text, bool *value,
                                  void (*on_change)(void), bool first)
{
    create_toggle_row(parent, label_text, desc_text, value, first);
    if (toggle_row_count > 0)
        toggle_rows[toggle_row_count - 1].on_change = on_change;
}

/* Action button row — styled as a highlighted pressable row */
static void create_action_row(lv_obj_t *parent, const char *label_text,
                               const char *desc_text, void (*action)(void), bool first)
{
    if (toggle_row_count >= MAX_TOGGLE_ROWS) return;

    lv_obj_t *row = create_setting_row_new(parent, label_text, desc_text, first);

    /* Style the row as an action button — accent colored */
    lv_obj_set_style_bg_color(row, lv_color_hex(0xa7c080), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_PART_MAIN);

    toggle_rows[toggle_row_count].type = ROW_ACTION;
    toggle_rows[toggle_row_count].row = row;
    toggle_rows[toggle_row_count].label = label_text;
    toggle_rows[toggle_row_count].value = NULL;
    toggle_rows[toggle_row_count].track_img = NULL;
    toggle_rows[toggle_row_count].thumb_img = NULL;
    toggle_rows[toggle_row_count].text_buf = NULL;
    toggle_rows[toggle_row_count].text_label = NULL;
    toggle_rows[toggle_row_count].on_change = action;
    toggle_row_count++;
}

static void create_text_row(lv_obj_t *parent, const char *label_text,
                             const char *desc_text, char *buf, int buf_size, bool first)
{
    if (toggle_row_count >= MAX_TOGGLE_ROWS) return;

    lv_obj_t *row = create_setting_row_new(parent, label_text, desc_text, first);

    /* Text field display (shows current value, styled as editable) */
    lv_obj_t *text_container = lv_obj_create(row);
    lv_obj_set_size(text_container, LV_SIZE_CONTENT, 27);
    lv_obj_set_style_bg_color(text_container, lv_color_hex(0x2e383c), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(text_container, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(text_container, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(text_container, lv_color_hex(0x414b50), LV_PART_MAIN);
    lv_obj_set_style_border_opa(text_container, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(text_container, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_left(text_container, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(text_container, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(text_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(text_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(text_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *text_lbl = lv_label_create(text_container);
    lv_obj_set_style_text_font(text_lbl, &lv_font_jetbrains_mono_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(text_lbl, EF_FG, LV_PART_MAIN);
    lv_label_set_text(text_lbl, buf[0] ? buf : "---");
    lv_obj_center(text_lbl);

    toggle_rows[toggle_row_count].type = ROW_TEXT;
    toggle_rows[toggle_row_count].row = row;
    toggle_rows[toggle_row_count].text_buf = buf;
    toggle_rows[toggle_row_count].label = label_text;
    toggle_rows[toggle_row_count].text_buf_size = buf_size;
    toggle_rows[toggle_row_count].text_label = text_lbl;
    toggle_rows[toggle_row_count].value = NULL;
    toggle_rows[toggle_row_count].track_img = text_container;  /* reuse for focus border */
    toggle_rows[toggle_row_count].thumb_img = NULL;
    toggle_rows[toggle_row_count].on_change = NULL;
    toggle_rows[toggle_row_count].keyboard_mode = DASH_KB_MODE_FULL;
    toggle_row_count++;
}

/* Text row that uses numeric keyboard */
static void create_numeric_text_row(lv_obj_t *parent, const char *label_text,
                                     const char *desc_text, char *buf, int buf_size, bool first)
{
    create_text_row(parent, label_text, desc_text, buf, buf_size, first);
    if (toggle_row_count > 0)
        toggle_rows[toggle_row_count - 1].keyboard_mode = DASH_KB_MODE_NUMERIC;
}

static void update_toggle_visuals(void)
{
    bool right_active = dash_panel_is_open() && !dash_panel_is_nav_focused();

    for (int i = 0; i < toggle_row_count; i++) {
        setting_row_t *t = &toggle_rows[i];
        bool focused = right_active && (i == toggle_row_selected);

        if (t->type == ROW_TOGGLE) {
            bool on = *t->value;
            if (focused) {
                lv_img_set_src(t->track_img, on ? &pill_toggle_on_focus : &pill_toggle_off_focus);
            } else {
                lv_img_set_src(t->track_img, on ? &pill_toggle_on : &pill_toggle_off);
            }
            lv_img_set_src(t->thumb_img, on ? &pill_toggle_thumb_on : &pill_toggle_thumb_off);
            lv_obj_set_pos(t->thumb_img, on ? 20 : -1, -1);
        } else if (t->type == ROW_TEXT) {
            /* Highlight text field border on focus */
            if (focused) {
                lv_obj_set_style_border_color(t->track_img, lv_color_hex(0xa7c080), LV_PART_MAIN);
                lv_obj_set_style_border_width(t->track_img, 2, LV_PART_MAIN);
            } else {
                lv_obj_set_style_border_color(t->track_img, lv_color_hex(0x414b50), LV_PART_MAIN);
                lv_obj_set_style_border_width(t->track_img, 1, LV_PART_MAIN);
            }
        } else if (t->type == ROW_ACTION) {
            if (focused) {
                lv_obj_set_style_bg_opa(t->row, LV_OPA_30, LV_PART_MAIN);
                lv_obj_set_style_border_width(t->row, 1, LV_PART_MAIN);
                lv_obj_set_style_border_color(t->row, lv_color_hex(0xa7c080), LV_PART_MAIN);
            } else {
                lv_obj_set_style_bg_opa(t->row, LV_OPA_20, LV_PART_MAIN);
                lv_obj_set_style_border_width(t->row, 0, LV_PART_MAIN);
            }
        }
    }
}

static void kb_done_refresh_text(void)
{
    if (toggle_row_selected < 0 || toggle_row_selected >= toggle_row_count) return;
    setting_row_t *t = &toggle_rows[toggle_row_selected];
    if (t->type == ROW_TEXT && t->text_label && t->text_buf)
        lv_label_set_text(t->text_label, t->text_buf[0] ? t->text_buf : "---");
}

static void toggle_current(void)
{
    if (toggle_row_selected < 0 || toggle_row_selected >= toggle_row_count) return;
    setting_row_t *t = &toggle_rows[toggle_row_selected];

    if (t->type == ROW_TOGGLE) {
        *t->value = !*t->value;
        if (t->on_change) t->on_change();
    } else if (t->type == ROW_TEXT) {
        dash_keyboard_open(t->text_buf, t->text_buf_size, t->keyboard_mode,
                           kb_done_refresh_text);
    } else if (t->type == ROW_ACTION) {
        if (t->on_change) t->on_change();
    }
    update_toggle_visuals();
}

/* Forward-declare network row indices (defined in network section below) */
static int net_static_ip_row;
static int net_static_mask_row;
static int net_static_gw_row;
static int net_dns1_row;
static int net_dns2_row;

/* Check if a row is currently read-only (view-only, skip in navigation) */
static bool net_row_is_readonly(int idx)
{
    if (dash_settings.dhcp_enabled &&
        (idx == net_static_ip_row || idx == net_static_mask_row || idx == net_static_gw_row))
        return true;
    if (!dash_settings.custom_dns &&
        (idx == net_dns1_row || idx == net_dns2_row))
        return true;
    return false;
}

static bool toggles_on_key(lv_key_t key)
{
    /* Always update visuals on focus change */
    update_toggle_visuals();

    if (toggle_row_count == 0) return false;

    if (key == LV_KEY_UP) {
        for (int i = 0; i < toggle_row_count; i++) {
            toggle_row_selected = (toggle_row_selected - 1 + toggle_row_count) % toggle_row_count;
            if (!lv_obj_has_flag(toggle_rows[toggle_row_selected].row, LV_OBJ_FLAG_HIDDEN)
                && !net_row_is_readonly(toggle_row_selected))
                break;
        }
        update_toggle_visuals();
        if (toggle_rows[toggle_row_selected].row)
            lv_obj_scroll_to_view(toggle_rows[toggle_row_selected].row, LV_ANIM_ON);
        return true;
    }
    if (key == LV_KEY_DOWN) {
        for (int i = 0; i < toggle_row_count; i++) {
            toggle_row_selected = (toggle_row_selected + 1) % toggle_row_count;
            if (!lv_obj_has_flag(toggle_rows[toggle_row_selected].row, LV_OBJ_FLAG_HIDDEN)
                && !net_row_is_readonly(toggle_row_selected))
                break;
        }
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

/* ── Snapshot: describes toggle/text rows for remote status ── */
static int toggles_snapshot(char *buf, int size)
{
    if (toggle_row_count == 0) return 0;

    bool right_active = dash_panel_is_open() && !dash_panel_is_nav_focused();
    int pos = 0;
    pos += snprintf(buf + pos, size - pos, "content=\n");
    for (int i = 0; i < toggle_row_count && pos < size - 1; i++) {
        setting_row_t *t = &toggle_rows[i];
        bool focused = right_active && (i == toggle_row_selected);

        if (t->type == ROW_TOGGLE) {
            pos += snprintf(buf + pos, size - pos, "  \"%s\" = %s",
                            t->label, *t->value ? "on" : "off");
        } else if (t->type == ROW_TEXT) {
            pos += snprintf(buf + pos, size - pos, "  \"%s\" = \"%s\"",
                            t->label, t->text_buf ? t->text_buf : "");
        }
        if (focused)
            pos += snprintf(buf + pos, size - pos, " [FOCUSED]");
        pos += snprintf(buf + pos, size - pos, "\n");
    }
    return pos;
}

/* ══════════════════════════════════════════════════════════════════
 *  Section: Display
 * ══════════════════════════════════════════════════════════════════ */
static char test_text_buf[64] = "Hello SodiumX";

static void build_display(lv_obj_t *body)
{
    section_header(body, "Display", "How SodiumX renders to your TV.");
    reset_toggle_rows();

    create_text_row(body, "Test field",
        "Editable text input (press A to edit).",
        test_text_buf, sizeof(test_text_buf), true);

    create_toggle_row(body, "Animated background",
        "GPU-rendered ambient gradient under the rail.",
        &dash_settings.animated_background, false);
    create_toggle_row(body, "Backdrop blur",
        "Use selected game's boxart as a blurred backdrop.",
        &dash_settings.backdrop_blur, false);
    create_toggle_row(body, "Tile parallax",
        "Subtle depth on the focused tile.",
        &dash_settings.tile_parallax, false);
    create_toggle_row(body, "Film grain",
        "Overlay noise for texture.",
        &dash_settings.film_grain, false);
    create_toggle_row(body, "Disable VSync",
        "Unlocks frame rate. May cause tearing.",
        &dash_settings.disable_vsync, false);
}

/* ══════════════════════════════════════════════════════════════════
 *  Section: Network
 * ══════════════════════════════════════════════════════════════════ */

/* Switch a text row between readout style (plain text, no box) and editable
 * style (text inside bordered container). track_img is the text container. */
static void net_set_row_readonly(int idx, const char *value)
{
    if (idx < 0 || idx >= toggle_row_count) return;
    setting_row_t *t = &toggle_rows[idx];
    if (!t->track_img || !t->text_label) return;

    /* Hide the input container, re-parent label to the row for readout look */
    lv_obj_add_flag(t->track_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_parent(t->text_label, t->row);
    lv_obj_set_style_text_font(t->text_label, &dash_font_ui_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(t->text_label, EF_FG, LV_PART_MAIN);
    lv_label_set_text(t->text_label, (value && value[0]) ? value : "---");
}

static void net_set_row_editable(int idx)
{
    if (idx < 0 || idx >= toggle_row_count) return;
    setting_row_t *t = &toggle_rows[idx];
    if (!t->track_img || !t->text_label) return;

    /* Show the input container, re-parent label back inside it */
    lv_obj_clear_flag(t->track_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_parent(t->text_label, t->track_img);
    lv_obj_set_style_text_font(t->text_label, &lv_font_jetbrains_mono_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(t->text_label, EF_FG, LV_PART_MAIN);
    lv_obj_center(t->text_label);
    lv_label_set_text(t->text_label, (t->text_buf && t->text_buf[0]) ? t->text_buf : "---");
}

static void net_update_visibility(void)
{
    dash_net_info_t info;
    dash_network_get_info(&info);

    /* IP/Subnet/Gateway: readout when DHCP on, editable when off */
    if (dash_settings.dhcp_enabled) {
        net_set_row_readonly(net_static_ip_row, info.ip);
        net_set_row_readonly(net_static_mask_row, info.mask);
        net_set_row_readonly(net_static_gw_row, info.gateway);
    } else {
        net_set_row_editable(net_static_ip_row);
        net_set_row_editable(net_static_mask_row);
        net_set_row_editable(net_static_gw_row);
    }

    /* DNS: when custom off, show live values as readouts.
     * When custom on, show editable fields. */
    if (dash_settings.custom_dns) {
        lv_obj_clear_flag(toggle_rows[net_dns1_row].row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(toggle_rows[net_dns2_row].row, LV_OBJ_FLAG_HIDDEN);
        net_set_row_editable(net_dns1_row);
        net_set_row_editable(net_dns2_row);
    } else {
        lv_obj_clear_flag(toggle_rows[net_dns1_row].row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(toggle_rows[net_dns2_row].row, LV_OBJ_FLAG_HIDDEN);
        net_set_row_readonly(net_dns1_row, info.dns1);
        net_set_row_readonly(net_dns2_row, info.dns2);
    }
}

/* Snapshot of network settings — restored on close if not applied */
static struct {
    bool dhcp_enabled;
    char static_ip[16];
    char static_mask[16];
    char static_gateway[16];
    bool custom_dns;
    char dns1[16];
    char dns2[16];
} net_snapshot;
static bool net_applied = false;

static void net_save_snapshot(void)
{
    net_snapshot.dhcp_enabled = dash_settings.dhcp_enabled;
    memcpy(net_snapshot.static_ip, dash_settings.static_ip, 16);
    memcpy(net_snapshot.static_mask, dash_settings.static_mask, 16);
    memcpy(net_snapshot.static_gateway, dash_settings.static_gateway, 16);
    net_snapshot.custom_dns = dash_settings.custom_dns;
    memcpy(net_snapshot.dns1, dash_settings.dns1, 16);
    memcpy(net_snapshot.dns2, dash_settings.dns2, 16);
    net_applied = false;
}

static void net_restore_snapshot(void)
{
    if (net_applied) return;
    dash_settings.dhcp_enabled = net_snapshot.dhcp_enabled;
    memcpy(dash_settings.static_ip, net_snapshot.static_ip, 16);
    memcpy(dash_settings.static_mask, net_snapshot.static_mask, 16);
    memcpy(dash_settings.static_gateway, net_snapshot.static_gateway, 16);
    dash_settings.custom_dns = net_snapshot.custom_dns;
    memcpy(dash_settings.dns1, net_snapshot.dns1, 16);
    memcpy(dash_settings.dns2, net_snapshot.dns2, 16);
}

static void on_ftp_toggle(void)
{
    if (dash_settings.ftp_enabled)
        dash_ftp_start();
    else
        dash_ftp_stop();
}

static void on_dhcp_toggle(void)
{
    /* When switching to static, prefill from current DHCP-assigned values */
    if (!dash_settings.dhcp_enabled) {
        dash_net_info_t info;
        dash_network_get_info(&info);
        if (!dash_settings.static_ip[0] && info.ip[0])
            strncpy(dash_settings.static_ip, info.ip, sizeof(dash_settings.static_ip) - 1);
        if (!dash_settings.static_mask[0] && info.mask[0])
            strncpy(dash_settings.static_mask, info.mask, sizeof(dash_settings.static_mask) - 1);
        if (!dash_settings.static_gateway[0] && info.gateway[0])
            strncpy(dash_settings.static_gateway, info.gateway, sizeof(dash_settings.static_gateway) - 1);
        /* Refresh text labels */
        for (int i = 0; i < toggle_row_count; i++) {
            setting_row_t *t = &toggle_rows[i];
            if (t->type == ROW_TEXT && t->text_label && t->text_buf)
                lv_label_set_text(t->text_label, t->text_buf[0] ? t->text_buf : "---");
        }
    }
    net_update_visibility();
}

static void on_dns_toggle(void)
{
    /* When enabling custom DNS, prefill from current values */
    if (dash_settings.custom_dns) {
        dash_net_info_t info;
        dash_network_get_info(&info);
        if (!dash_settings.dns1[0] && info.dns1[0])
            strncpy(dash_settings.dns1, info.dns1, sizeof(dash_settings.dns1) - 1);
        if (!dash_settings.dns2[0] && info.dns2[0])
            strncpy(dash_settings.dns2, info.dns2, sizeof(dash_settings.dns2) - 1);
        for (int i = 0; i < toggle_row_count; i++) {
            setting_row_t *t = &toggle_rows[i];
            if (t->type == ROW_TEXT && t->text_label && t->text_buf)
                lv_label_set_text(t->text_label, t->text_buf[0] ? t->text_buf : "---");
        }
    }
    net_update_visibility();
}

static void on_apply_network(void)
{
    net_applied = true;
    dash_network_apply();
    dash_settings_apply(false);
}

static void build_network(lv_obj_t *body)
{
    section_header(body, "Network", "IP configuration and FTP server.");
    reset_toggle_rows();
    net_save_snapshot();

    /* FTP Server toggle — runtime start/stop */
    create_toggle_row_cb(body, "FTP Server",
        "Port 21 " LV_SYMBOL_DUMMY " user: xbox " LV_SYMBOL_DUMMY " pass: xbox",
        &dash_settings.ftp_enabled, on_ftp_toggle, true);

    /* Status */
    dash_net_info_t info;
    dash_network_get_info(&info);

    char status[48];
    if (info.link_up)
        lv_snprintf(status, sizeof(status), "Connected (%s)", info.dhcp_active ? "DHCP" : "Static");
    else
        lv_snprintf(status, sizeof(status), "Disconnected");
    create_readout_new(body, "Status", status, false);

    /* DHCP toggle */
    create_toggle_row_cb(body, "DHCP",
        "Automatic IP assignment from router.",
        &dash_settings.dhcp_enabled, on_dhcp_toggle, false);

    /* IP / Subnet / Gateway — always visible.
     * DHCP on: shows live values, read-only (skipped in navigation).
     * DHCP off: editable with numeric keyboard. */
    create_numeric_text_row(body, "IP Address", NULL,
        dash_settings.static_ip, sizeof(dash_settings.static_ip), false);
    net_static_ip_row = toggle_row_count - 1;

    create_numeric_text_row(body, "Subnet Mask", NULL,
        dash_settings.static_mask, sizeof(dash_settings.static_mask), false);
    net_static_mask_row = toggle_row_count - 1;

    create_numeric_text_row(body, "Gateway", NULL,
        dash_settings.static_gateway, sizeof(dash_settings.static_gateway), false);
    net_static_gw_row = toggle_row_count - 1;

    /* Custom DNS toggle */
    create_toggle_row_cb(body, "Custom DNS",
        "Override automatic DNS servers.",
        &dash_settings.custom_dns, on_dns_toggle, false);

    /* DNS fields — hidden when custom DNS off */
    create_numeric_text_row(body, "DNS 1", NULL,
        dash_settings.dns1, sizeof(dash_settings.dns1), false);
    net_dns1_row = toggle_row_count - 1;
    create_numeric_text_row(body, "DNS 2", NULL,
        dash_settings.dns2, sizeof(dash_settings.dns2), false);
    net_dns2_row = toggle_row_count - 1;

    /* Apply button */
    create_action_row(body, "Apply Network Settings",
        "Save and apply changes now.", on_apply_network, false);

    /* Link speed at the bottom */
    char speed[16];
    lv_snprintf(speed, sizeof(speed), "%d Mbps", info.link_speed_mbps);
    create_readout_new(body, "Link Speed", speed, false);

    /* Set initial visibility and read-only state */
    net_update_visibility();
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
    section_header(body, "About SodiumX", "A minimal, GPU-accelerated dashboard.");

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
    /* Restore unapplied network changes before saving */
    net_restore_snapshot();
    dash_settings_apply(false);
    dash_statusbar_refresh();
}

static const dash_panel_section_t settings_sections[] = {
    { "Display",   LV_SYMBOL_IMAGE,      build_display,  toggles_on_key, false, toggles_snapshot },
    { "Network",   LV_SYMBOL_WIFI,       build_network,  toggles_on_key, false, toggles_snapshot },
    { "Audio",     LV_SYMBOL_VOLUME_MAX, build_audio,    toggles_on_key, false, toggles_snapshot },
    { "System",    LV_SYMBOL_SETTINGS,   build_system,   toggles_on_key, false, toggles_snapshot },
    { "Backup",    LV_SYMBOL_UPLOAD,     build_backup,   toggles_on_key, false, toggles_snapshot },
#define LV_SYMBOL_MICROCHIP "\xEF\x8B\x9B" /* U+F2DB */
    { "Hardware",  LV_SYMBOL_MICROCHIP,  build_hardware, NULL, true,  NULL },
    { "About",     LV_SYMBOL_EYE_OPEN,   build_about,    NULL, true,  NULL },
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
