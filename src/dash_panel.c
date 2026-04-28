// SPDX-License-Identifier: MIT
// Reusable two-pane panel with left nav + right content body.

#include "sodiumx.h"
#include "dash_anim.h"
#include "dash_panel.h"

/* ── State ── */
static bool panel_open = false;
static bool right_focused = false;
static int active_section = 0;
static const dash_panel_config_t *cfg = NULL;

static lv_obj_t *panel_overlay = NULL;
static lv_obj_t *panel_card = NULL;
static lv_obj_t *panel_body = NULL;
static lv_obj_t *nav_items[DASH_PANEL_MAX_SECTIONS];

/* ── Nav highlight ── */
static void update_nav(void)
{
    for (int i = 0; i < cfg->section_count; i++) {
        lv_obj_remove_style(nav_items[i], &panel_nav_item_active_style, LV_PART_MAIN);
        lv_obj_add_style(nav_items[i], &panel_nav_item_style, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(nav_items[i], LV_OPA_TRANSP, LV_PART_MAIN);

        lv_obj_t *ic = lv_obj_get_child(nav_items[i], 0);
        lv_obj_t *lb = lv_obj_get_child(nav_items[i], 1);
        if (ic) lv_obj_set_style_text_color(ic, EF_FG_MUTED, LV_PART_MAIN);
        if (lb) lv_obj_set_style_text_color(lb, EF_FG_MUTED, LV_PART_MAIN);
    }

    if (active_section < 0 || active_section >= cfg->section_count) return;

    lv_obj_t *sel = nav_items[active_section];
    lv_obj_remove_style(sel, &panel_nav_item_style, LV_PART_MAIN);
    lv_obj_add_style(sel, &panel_nav_item_active_style, LV_PART_MAIN);

    lv_obj_t *icon = lv_obj_get_child(sel, 0);
    lv_obj_t *lbl = lv_obj_get_child(sel, 1);

    if (right_focused) {
        /* Active but not focused — dimmed */
        lv_obj_set_style_bg_opa(sel, 20, LV_PART_MAIN);
        if (icon) lv_obj_set_style_text_color(icon, EF_FG_MUTED, LV_PART_MAIN);
        if (lbl) lv_obj_set_style_text_color(lbl, EF_FG, LV_PART_MAIN);
    } else {
        /* Focused — bright accent */
        lv_obj_set_style_bg_opa(sel, 50, LV_PART_MAIN);
        if (icon) lv_obj_set_style_text_color(icon, dash_accent_color, LV_PART_MAIN);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
    }
}

/* ── Body rebuild ── */
void dash_panel_rebuild_body(void)
{
    if (!panel_body || !cfg) return;
    lv_obj_clean(panel_body);
    if (active_section >= 0 && active_section < cfg->section_count) {
        if (cfg->sections[active_section].build)
            cfg->sections[active_section].build(panel_body);
    }
}

/* ── Close ── */
void dash_panel_close(void)
{
    if (!panel_open || !panel_overlay) return;
    panel_open = false;
    right_focused = false;

    if (cfg && cfg->on_close)
        cfg->on_close();

    dash_anim_overlay_out(panel_card, 200, NULL);
    lv_obj_del_delayed(panel_overlay, 220);
    panel_overlay = NULL;
    panel_card = NULL;
    panel_body = NULL;
    cfg = NULL;

    dash_focus_pop_depth();
}

/* ── Key handler ── */
static void panel_key_handler(lv_event_t *event)
{
    lv_key_t key = *((lv_key_t *)lv_event_get_param(event));

    if (key == LV_KEY_ESC) {
        if (right_focused) {
            /* B in right pane → back to left nav */
            const dash_panel_section_t *sec = &cfg->sections[active_section];
            right_focused = false;
            update_nav();
            /* Notify section of focus change */
            if (sec->on_key) sec->on_key(key);
        } else {
            /* B in left nav → close panel */
            dash_panel_close();
        }
        return;
    }

    if (!right_focused) {
        /* ── Left nav mode ── */
        if (key == LV_KEY_UP) {
            active_section = (active_section - 1 + cfg->section_count) % cfg->section_count;
            update_nav();
            dash_panel_rebuild_body();
        } else if (key == LV_KEY_DOWN) {
            active_section = (active_section + 1) % cfg->section_count;
            update_nav();
            dash_panel_rebuild_body();
        } else if (key == LV_KEY_RIGHT || key == LV_KEY_ENTER) {
            const dash_panel_section_t *sec = &cfg->sections[active_section];
            if (sec->viewonly) return; /* Can't focus view-only sections */
            /* Switch focus to right content — notify with RIGHT, never ENTER,
             * so sections don't accidentally trigger an action on entry. */
            right_focused = true;
            update_nav();
            if (sec->on_key) sec->on_key(LV_KEY_RIGHT);
        }
    } else {
        /* ── Right content mode ── */
        const dash_panel_section_t *sec = &cfg->sections[active_section];

        /* Let section handle the key first */
        if (sec->on_key && sec->on_key(key))
            return; /* consumed */

        /* Default handling for unconsumed keys */
        if (key == LV_KEY_LEFT) {
            right_focused = false;
            update_nav();
            /* Notify section of focus change */
            if (sec->on_key) sec->on_key(LV_KEY_LEFT);
        }
        /* UP/DOWN/ENTER not consumed by section → do nothing */
    }
}

/* ── Open ── */
void dash_panel_open(const dash_panel_config_t *config)
{
    if (panel_open) {
        dash_panel_close();
        return;
    }

    cfg = config;
    panel_open = true;
    right_focused = false;
    active_section = 0;

    lv_coord_t scr_w = lv_obj_get_width(lv_scr_act());
    lv_coord_t scr_h = lv_obj_get_height(lv_scr_act());

    /* Fullscreen overlay */
    panel_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(panel_overlay, scr_w, scr_h);
    lv_obj_set_style_bg_opa(panel_overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(panel_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(panel_overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Scrim */
    lv_obj_t *scrim = lv_obj_create(panel_overlay);
    lv_obj_set_size(scrim, scr_w, scr_h);
    lv_obj_add_style(scrim, &overlay_scrim_style, LV_PART_MAIN);
    lv_obj_clear_flag(scrim, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Panel card */
    panel_card = lv_obj_create(panel_overlay);
    lv_obj_set_pos(panel_card, 40, 44);
    lv_obj_set_size(panel_card, scr_w - 80, scr_h - 44 - 64);
    lv_obj_add_style(panel_card, &panel_style, LV_PART_MAIN);
    lv_obj_set_layout(panel_card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panel_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(panel_card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(panel_card, LV_OBJ_FLAG_SCROLLABLE);

    /* Left nav (240px) */
    lv_obj_t *nav = lv_obj_create(panel_card);
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
    lv_label_set_text(nav_title, cfg->nav_title ? cfg->nav_title : "MENU");
    lv_obj_set_style_pad_bottom(nav_title, 4, LV_PART_MAIN);

    /* Optional subtitle */
    if (cfg->nav_subtitle && cfg->nav_subtitle[0]) {
        lv_obj_t *sub = lv_label_create(nav);
        lv_obj_set_style_text_font(sub, &dash_font_ui_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(sub, EF_FG_MUTED, LV_PART_MAIN);
        lv_obj_set_width(sub, 210);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
        lv_label_set_text(sub, cfg->nav_subtitle);
    }
    lv_obj_set_style_pad_bottom(lv_obj_get_child(nav, lv_obj_get_child_cnt(nav) - 1),
                                 12, LV_PART_MAIN);

    /* Nav items */
    for (int i = 0; i < cfg->section_count && i < DASH_PANEL_MAX_SECTIONS; i++) {
        lv_obj_t *item = lv_obj_create(nav);
        lv_obj_set_size(item, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_add_style(item, &panel_nav_item_style, LV_PART_MAIN);
        lv_obj_set_layout(item, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *icon = lv_label_create(item);
        lv_label_set_text(icon, cfg->sections[i].icon ? cfg->sections[i].icon : LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_font(icon, &dash_font_ui_14, LV_PART_MAIN);

        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, cfg->sections[i].label ? cfg->sections[i].label : "");

        nav_items[i] = item;
    }
    update_nav();

    /* Right body (scrollable) */
    panel_body = lv_obj_create(panel_card);
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

    dash_panel_rebuild_body();

    /* Entry animation */
    dash_anim_overlay_in(panel_card, 300);

    /* Focus management */
    lv_group_add_obj(lv_group_get_default(), panel_card);
    lv_obj_add_event_cb(panel_card, panel_key_handler, LV_EVENT_KEY, NULL);
    dash_focus_change_depth(panel_card);
}

int dash_panel_get_section(void)
{
    return active_section;
}

bool dash_panel_is_open(void)
{
    return panel_open;
}

bool dash_panel_is_nav_focused(void)
{
    return !right_focused;
}

/* ── Snapshot for remote status ── */
static int panel_snapshot(char *buf, int size)
{
    if (!panel_open || !cfg) return 0;

    int pos = 0;
    pos += snprintf(buf + pos, size - pos, "[panel]\n");
    pos += snprintf(buf + pos, size - pos, "title=\"%s\"\n",
                    cfg->nav_title ? cfg->nav_title : "?");
    pos += snprintf(buf + pos, size - pos, "focus=%s\n",
                    right_focused ? "content" : "nav");
    pos += snprintf(buf + pos, size - pos, "sections=");
    for (int i = 0; i < cfg->section_count && pos < size - 1; i++) {
        pos += snprintf(buf + pos, size - pos, "%s\"%s\"",
                        i > 0 ? ", " : "", cfg->sections[i].label);
        if (i == active_section)
            pos += snprintf(buf + pos, size - pos, " [ACTIVE]");
    }
    pos += snprintf(buf + pos, size - pos, "\n");

    /* Active section content snapshot */
    if (active_section >= 0 && active_section < cfg->section_count) {
        const dash_panel_section_t *sec = &cfg->sections[active_section];
        if (sec->snapshot) {
            int wrote = sec->snapshot(buf + pos, size - pos);
            if (wrote > 0) pos += wrote;
        }
    }

    return pos;
}

void dash_panel_snapshot_register(void)
{
    dash_snapshot_register(panel_snapshot);
}
