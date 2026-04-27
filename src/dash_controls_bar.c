// SPDX-License-Identifier: MIT

#include "lithiumx.h"
#include "dash_pill_data.h"

static lv_obj_t *cb_container;
static lv_obj_t *lbl_a;
static lv_obj_t *lbl_b;
static lv_obj_t *lbl_x;
static lv_obj_t *lbl_y;

/* FA circle glyph U+F111 as UTF-8 */
#define FA_CIRCLE "\xEF\x84\x91"

static lv_obj_t *create_btn_glyph(lv_obj_t *parent, const char *letter, lv_color_t color)
{
    /* Container sized to the circle glyph */
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    /* FA circle glyph as anti-aliased background */
    lv_obj_t *circle_lbl = lv_label_create(btn);
    lv_label_set_text(circle_lbl, FA_CIRCLE);
    lv_obj_set_style_text_font(circle_lbl, &lv_font_rubik_26, LV_PART_MAIN);
    lv_obj_set_style_text_color(circle_lbl, color, LV_PART_MAIN);
    lv_obj_center(circle_lbl);

    /* Letter label centered on top */
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, letter);
    lv_obj_set_style_text_font(lbl, &lv_font_rubik_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x0b0d0e), LV_PART_MAIN);
    lv_obj_center(lbl);

    return btn;
}

static lv_obj_t *create_hint(lv_obj_t *parent, const char *letter, lv_color_t color,
                             const char *label_text, lv_obj_t **label_out)
{
    lv_obj_t *hint = lv_obj_create(parent);
    lv_obj_set_size(hint, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hint, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(hint, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hint, 0, LV_PART_MAIN);
    lv_obj_set_layout(hint, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hint, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hint, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hint, 8, LV_PART_MAIN);
    lv_obj_clear_flag(hint, LV_OBJ_FLAG_SCROLLABLE);

    create_btn_glyph(hint, letter, color);

    lv_obj_t *lbl = lv_label_create(hint);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_rubik_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, EF_FG, LV_PART_MAIN);

    if (label_out) *label_out = lbl;
    return hint;
}

lv_obj_t *dash_controls_bar_create(lv_obj_t *parent)
{
    lv_coord_t w = lv_obj_get_width(parent);

    cb_container = lv_obj_create(parent);
    lv_obj_set_size(cb_container, w, 52);
    lv_obj_align(cb_container, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_style(cb_container, &controls_bar_style, LV_PART_MAIN);
    lv_obj_set_layout(cb_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cb_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cb_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cb_container, LV_OBJ_FLAG_SCROLLABLE);
    /* Ensure children aren't clipped */

    /* Left: button hints */
    lv_obj_t *hints_left = lv_obj_create(cb_container);
    lv_obj_remove_style_all(hints_left);
    lv_obj_set_size(hints_left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hints_left, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(hints_left, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hints_left, 0, LV_PART_MAIN);
    lv_obj_set_layout(hints_left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hints_left, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(hints_left, 18, LV_PART_MAIN);
    lv_obj_set_flex_align(hints_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hints_left, LV_OBJ_FLAG_SCROLLABLE);

    create_hint(hints_left, "A", EF_GREEN,  "Launch", &lbl_a);
    create_hint(hints_left, "B", EF_RED,    "Back",   &lbl_b);
    create_hint(hints_left, "X", EF_BLUE,   "Manage", &lbl_x);
    create_hint(hints_left, "Y", EF_YELLOW, "Sort",   &lbl_y);

    /* Right: START pill */
    lv_obj_t *start_hint = lv_obj_create(cb_container);
    lv_obj_remove_style_all(start_hint);
    lv_obj_set_size(start_hint, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(start_hint, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(start_hint, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(start_hint, 0, LV_PART_MAIN);
    lv_obj_set_layout(start_hint, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(start_hint, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(start_hint, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(start_hint, 8, LV_PART_MAIN);
    lv_obj_clear_flag(start_hint, LV_OBJ_FLAG_SCROLLABLE);

    /* START label + Menu label — pill removed for now, was always clipped */
    lv_obj_t *start_lbl = lv_label_create(start_hint);
    lv_label_set_text(start_lbl, "START");
    lv_obj_set_style_text_font(start_lbl, &lv_font_rubik_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(start_lbl, EF_FG, LV_PART_MAIN);

    /* DEBUG: red border on start_hint */
    lv_obj_set_style_border_width(start_hint, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(start_hint, lv_color_hex(0xff0000), LV_PART_MAIN);
    lv_obj_set_style_border_opa(start_hint, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(start_hint, lv_color_hex(0xff0000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(start_hint, 100, LV_PART_MAIN);

    lv_obj_t *menu_lbl = lv_label_create(start_hint);
    lv_label_set_text(menu_lbl, "Menu");
    lv_obj_set_style_text_font(menu_lbl, &lv_font_rubik_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(menu_lbl, EF_FG, LV_PART_MAIN);

    return cb_container;
}

void dash_controls_bar_set_context(const char *a_label, const char *b_label,
                                   const char *x_label, const char *y_label)
{
    if (lbl_a) lv_label_set_text(lbl_a, a_label);
    if (lbl_b) lv_label_set_text(lbl_b, b_label);
    if (lbl_x) lv_label_set_text(lbl_x, x_label);
    if (lbl_y) lv_label_set_text(lbl_y, y_label);
}
