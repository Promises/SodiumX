// SPDX-License-Identifier: MIT
// Shared overlay menu widget — see dash_overlay_menu.h

#include "sodiumx.h"
#include "dash_overlay_menu.h"
#include "dash_anim.h"
#include "dash_remote.h"

/* ── State ── */
static bool ovl_open = false;
static overlay_menu_config_t cfg;
static lv_obj_t *overlay;
static lv_obj_t *card;
static lv_obj_t *item_objs[OVERLAY_MENU_MAX_ITEMS];
static int selected = 0;

/* ── Highlight ── */
static void highlight_item(int index)
{
    for (int i = 0; i < cfg.item_count; i++) {
        lv_obj_remove_style(item_objs[i], &overlay_item_focused_style, LV_PART_MAIN);
        lv_obj_add_style(item_objs[i], &overlay_item_style, LV_PART_MAIN);
        lv_obj_set_style_translate_x(item_objs[i], 0, LV_PART_MAIN);
    }

    if (index >= 0 && index < cfg.item_count) {
        lv_obj_remove_style(item_objs[index], &overlay_item_style, LV_PART_MAIN);
        lv_obj_add_style(item_objs[index], &overlay_item_focused_style, LV_PART_MAIN);
        dash_anim_x(item_objs[index],
                     lv_obj_get_style_translate_x(item_objs[index], LV_PART_MAIN), 4, 200);
    }
}

/* ── Close ── */
void dash_overlay_menu_close(void)
{
    if (!ovl_open || !overlay) return;
    ovl_open = false;

    dash_anim_overlay_out(card, 200, NULL);
    lv_obj_del_delayed(overlay, 220);
    overlay = NULL;
    card = NULL;

    dash_focus_pop_depth();
}

/* ── Key handler ── */
static void key_handler(lv_event_t *event)
{
    lv_key_t key = *((lv_key_t *)lv_event_get_param(event));

    if (key == LV_KEY_ESC || key == DASH_KEY_BACK || (cfg.close_key && key == (lv_key_t)cfg.close_key)) {
        dash_overlay_menu_close();
        return;
    }

    if (key == LV_KEY_DOWN) {
        selected = (selected + 1) % cfg.item_count;
        highlight_item(selected);
    } else if (key == LV_KEY_UP) {
        selected = (selected - 1 + cfg.item_count) % cfg.item_count;
        highlight_item(selected);
    } else if (key == LV_KEY_ENTER) {
        const overlay_menu_item_t *item = &cfg.items[selected];
        if (item->confirm_text) {
            confirmbox_open(item->confirm_text, item->cb, item->cb_param);
        } else if (item->cb) {
            item->cb(item->cb_param);
        }
    }
}

/* ── Open ── */
void dash_overlay_menu_open(const overlay_menu_config_t *config)
{
    if (ovl_open) {
        dash_overlay_menu_close();
    }

    cfg = *config;
    if (cfg.item_count > OVERLAY_MENU_MAX_ITEMS)
        cfg.item_count = OVERLAY_MENU_MAX_ITEMS;

    ovl_open = true;
    selected = 0;

    /* Fullscreen scrim */
    overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(overlay, lv_obj_get_width(lv_scr_act()), lv_obj_get_height(lv_scr_act()));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x0b0d0e), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, 140, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Centered card */
    card = lv_obj_create(overlay);
    lv_obj_add_style(card, &overlay_card_style, LV_PART_MAIN);
    lv_obj_set_width(card, 420);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Header ── */
    lv_obj_t *header = lv_obj_create(card);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(header, EF_FG, LV_PART_MAIN);
    lv_obj_set_style_border_opa(header, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_left(header, 22, LV_PART_MAIN);
    lv_obj_set_style_pad_right(header, 22, LV_PART_MAIN);
    lv_obj_set_style_pad_top(header, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(header, 18, LV_PART_MAIN);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    /* Header left: eyebrow + title */
    lv_obj_t *title_col = lv_obj_create(header);
    lv_obj_set_size(title_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(title_col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(title_col, 0, LV_PART_MAIN);
    lv_obj_set_layout(title_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(title_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(title_col, 2, LV_PART_MAIN);
    lv_obj_clear_flag(title_col, LV_OBJ_FLAG_SCROLLABLE);

    if (cfg.eyebrow) {
        lv_obj_t *eyebrow = lv_label_create(title_col);
        lv_obj_add_style(eyebrow, &eyebrow_style, LV_PART_MAIN);
        lv_obj_set_style_text_color(eyebrow, dash_accent_color, LV_PART_MAIN);
        lv_label_set_text(eyebrow, cfg.eyebrow);
    }

    lv_obj_t *title = lv_label_create(title_col);
    lv_obj_set_style_text_font(title, &dash_font_ui_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_width(title, 300);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_label_set_text(title, cfg.title ? cfg.title : "");

    /* Header right: close hint */
    if (cfg.close_hint) {
        lv_obj_t *hint = lv_label_create(header);
        lv_obj_add_style(hint, &mono_small_style, LV_PART_MAIN);
        lv_label_set_text(hint, cfg.close_hint);
    }

    /* ── Item list ── */
    lv_obj_t *list = lv_obj_create(card);
    lv_obj_set_size(list, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(list, 0, LV_PART_MAIN);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < cfg.item_count; i++) {
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_add_style(row, &overlay_item_style, LV_PART_MAIN);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* Icon tile (32x32) */
        lv_obj_t *icon_tile = lv_obj_create(row);
        lv_obj_set_size(icon_tile, 32, 32);
        lv_obj_set_style_radius(icon_tile, 8, LV_PART_MAIN);
        lv_obj_set_style_bg_color(icon_tile, EF_FG, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(icon_tile, 15, LV_PART_MAIN);
        lv_obj_set_style_border_width(icon_tile, 0, LV_PART_MAIN);
        lv_obj_clear_flag(icon_tile, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *icon_lbl = lv_label_create(icon_tile);
        lv_label_set_text(icon_lbl, cfg.items[i].icon ? cfg.items[i].icon : LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(icon_lbl, EF_FG_MUTED, LV_PART_MAIN);
        lv_obj_set_style_text_font(icon_lbl, &dash_font_ui_14, LV_PART_MAIN);
        lv_obj_center(icon_lbl);

        /* Label */
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, cfg.items[i].label);
        lv_obj_set_style_text_font(lbl, &dash_font_ui_16, LV_PART_MAIN);
        lv_obj_set_flex_grow(lbl, 1);

        /* Chevron */
        lv_obj_t *chevron = lv_label_create(row);
        lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chevron, EF_FG_MUTED, LV_PART_MAIN);
        lv_obj_set_style_opa(chevron, 128, LV_PART_MAIN);

        item_objs[i] = row;
    }

    highlight_item(0);

    /* Animation + focus */
    dash_anim_overlay_in(card, 300);
    lv_group_add_obj(lv_group_get_default(), card);
    lv_obj_add_event_cb(card, key_handler, LV_EVENT_KEY, NULL);
    dash_focus_change_depth(card);
}

/* ── Query ── */
bool dash_overlay_menu_is_open(void)
{
    return ovl_open;
}

/* ── Snapshot for remote status ── */
static int overlay_menu_snapshot(char *buf, int size)
{
    if (!ovl_open) return 0;

    int pos = 0;
    pos += snprintf(buf + pos, size - pos, "[menu]\n");
    if (cfg.eyebrow)
        pos += snprintf(buf + pos, size - pos, "eyebrow=\"%s\"\n", cfg.eyebrow);
    pos += snprintf(buf + pos, size - pos, "title=\"%s\"\n", cfg.title ? cfg.title : "");
    pos += snprintf(buf + pos, size - pos, "items=");
    for (int i = 0; i < cfg.item_count && pos < size - 1; i++) {
        pos += snprintf(buf + pos, size - pos, "%s\"%s\"",
                        i > 0 ? ", " : "", cfg.items[i].label);
        if (i == selected)
            pos += snprintf(buf + pos, size - pos, " [SELECTED]");
    }
    pos += snprintf(buf + pos, size - pos, "\n");
    return pos;
}

void dash_overlay_menu_snapshot_register(void)
{
    dash_snapshot_register(overlay_menu_snapshot);
}
