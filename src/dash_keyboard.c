// SPDX-License-Identifier: MIT
// Daisywheel keyboard — ported from DaisywheelJS.
// Singleton overlay: created once, shown/hidden as needed.

#include "sodiumx.h"
#include "dash_keyboard.h"

#include "dash_pill_data.h"

#include <stdlib.h>
#include <string.h>

/* ══════════════════════════════════════════════════════════════════
 *  Constants
 * ══════════════════════════════════════════════════════════════════ */

#define NUM_PETALS       8
#define CHARS_PER_PETAL  4
#define NUM_CHARSETS     3

#define STICK_DEADZONE   8192   /* 25% of 32768 */

/* Flower geometry (320px diameter, 104px radius) */
#define FLOWER_SIZE      320
#define FLOWER_CX        320    /* screen center X */
#define FLOWER_CY        190    /* shifted up for text bar below */
#define PETAL_RADIUS     104
#define PETAL_SIZE        80
#define CENTER_SIZE       80

/* Text bar */
#define TEXT_BAR_W       560
#define TEXT_BAR_H        36
#define TEXT_BAR_Y       410

/* Numeric strip */
#define NUM_DIGITS       14     /* 0-9 . : / - */
#define DIGIT_CELL_SIZE   36
#define DIGIT_CELL_GAP     4

/* ══════════════════════════════════════════════════════════════════
 *  Character sets
 * ══════════════════════════════════════════════════════════════════ */

/* Each set: 32 chars, 4 per petal (X=0, Y=1, B=2, A=3).
 * NOT null-terminated — indexed by [petal * 4 + button]. */
static const char charset_lower[] =
    "abcd" "efgh" "ijkl" "mnop" "qrst" "uvwx" "yz?!" ";\\_-";
/*   N       NE     E      SE     S      SW     W      NW   */

static const char charset_upper[] =
    "ABCD" "EFGH" "IJKL" "MNOP" "QRST" "UVWX" "YZ+." "@#$%";

static const char charset_numsym[] =
    "0123" "4567" "89*," "_=\"'" "()[]" "{}:~" "^<>|" "/`!@";

static const char *charsets[NUM_CHARSETS] = {
    charset_lower, charset_upper, charset_numsym
};
static const char *charset_names[NUM_CHARSETS] = {
    "abc", "ABC", "123"
};

/* Numeric strip characters */
static const char numeric_chars[NUM_DIGITS] = {
    '0','1','2','3','4','5','6','7','8','9','.',':','/','-'
};

/* ══════════════════════════════════════════════════════════════════
 *  Petal positions — precomputed offsets from flower center
 * ══════════════════════════════════════════════════════════════════ */

static const struct { int x, y; } petal_offset[NUM_PETALS] = {
    {   0, -104},   /* 0: N   */
    {  74,  -74},   /* 1: NE  */
    { 104,    0},   /* 2: E   */
    {  74,   74},   /* 3: SE  */
    {   0,  104},   /* 4: S   */
    { -74,   74},   /* 5: SW  */
    {-104,    0},   /* 6: W   */
    { -74,  -74},   /* 7: NW  */
};

/* Face button colors: X=blue, Y=yellow, B=red, A=green */
static const uint32_t char_colors[CHARS_PER_PETAL] = {
    0x7fbbb3,   /* X — blue   */
    0xdbbc7f,   /* Y — yellow */
    0xe67e80,   /* B — red    */
    0xa7c080,   /* A — green  */
};

/* Circle size for button indicators inside petals */
#define CHAR_CIRCLE_SIZE  22


/* ══════════════════════════════════════════════════════════════════
 *  Singleton state
 * ══════════════════════════════════════════════════════════════════ */

static struct {
    bool open;
    int  mode;               /* DASH_KB_MODE_FULL or _NUMERIC */

    /* Caller's buffer */
    char *buf;
    int   buf_size;
    char  backup[256];       /* restore on cancel */
    void (*on_done)(void);

    /* Text editing */
    int cursor;              /* insertion point (0 = before first char) */

    /* Full mode */
    int current_petal;       /* -1 = none selected */
    int current_charset;     /* 0=lower, 1=upper, 2=numsym */
    int held_charset;        /* charset while trigger held (-1 = none) */

    /* Numeric mode */
    int digit_selected;      /* 0..NUM_DIGITS-1 */

    /* LVGL objects */
    lv_obj_t *scrim;
    lv_obj_t *focus_obj;     /* receives key events */
    lv_obj_t *text_bar;
    lv_obj_t *text_label;
    lv_obj_t *hint_bar;      /* controls hint bar */

    /* Full mode objects */
    lv_obj_t *flower_bg;
    lv_obj_t *center_label;
    lv_obj_t *petal_obj[NUM_PETALS];
    lv_obj_t *char_circle[NUM_PETALS][CHARS_PER_PETAL];  /* colored circle bg */
    lv_obj_t *char_label[NUM_PETALS][CHARS_PER_PETAL];

    /* Numeric mode objects */
    lv_obj_t *digit_strip;
    lv_obj_t *digit_cell[NUM_DIGITS];
    lv_obj_t *digit_label[NUM_DIGITS];

    /* Stick polling timer */
    lv_timer_t *stick_timer;

    /* Cursor blink timer */
    lv_timer_t *blink_timer;
    bool cursor_visible;
} kb;

/* ══════════════════════════════════════════════════════════════════
 *  Forward declarations
 * ══════════════════════════════════════════════════════════════════ */

static void kb_build_full(void);
static void kb_build_numeric(void);
static void kb_build_text_bar(void);
static void kb_build_hints(void);
static void kb_update_text(void);
static void kb_update_charset_labels(void);
static void kb_highlight_petal(int idx);
static void kb_unhighlight_petal(int idx);
static void kb_highlight_digit(int idx);
static void kb_insert_char(char c);
static void kb_backspace(void);
static void kb_key_handler(lv_event_t *event);
static void kb_stick_poll(lv_timer_t *t);
static void kb_blink_cb(lv_timer_t *t);
static int  kb_snapshot(char *buf, int size);

/* Helper: strip all default LVGL obj styling */
static void strip_obj_defaults(lv_obj_t *obj)
{
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

/* ══════════════════════════════════════════════════════════════════
 *  Stick direction detection (integer math)
 * ══════════════════════════════════════════════════════════════════ */

static int get_petal_from_stick(int x, int y)
{
    int ax = abs(x), ay = abs(y);
    if (ax < STICK_DEADZONE && ay < STICK_DEADZONE)
        return -1;

    /* Compare magnitudes to determine octant */
    if (ay > ax * 2) return y < 0 ? 0 : 4;   /* N or S */
    if (ax > ay * 2) return x > 0 ? 2 : 6;   /* E or W */
    if (x > 0 && y < 0) return 1;              /* NE */
    if (x > 0 && y > 0) return 3;              /* SE */
    if (x < 0 && y > 0) return 5;              /* SW */
    return 7;                                    /* NW */
}

/* ══════════════════════════════════════════════════════════════════
 *  Init (call once at startup)
 * ══════════════════════════════════════════════════════════════════ */

void dash_keyboard_init(void)
{
    memset(&kb, 0, sizeof(kb));
    kb.current_petal = -1;
    dash_snapshot_register(kb_snapshot);
}

/* ══════════════════════════════════════════════════════════════════
 *  Open / Close
 * ══════════════════════════════════════════════════════════════════ */

void dash_keyboard_open(char *buf, int buf_size, int mode, void (*on_done)(void))
{
    if (kb.open) return;

    kb.open = true;
    kb.mode = mode;
    kb.buf = buf;
    kb.buf_size = buf_size;
    kb.on_done = on_done;
    kb.cursor = (int)strlen(buf);
    kb.current_charset = 0;
    kb.held_charset = -1;
    kb.current_petal = -1;
    kb.digit_selected = 0;
    kb.cursor_visible = true;

    /* Save backup for cancel */
    int len = strlen(buf);
    if (len >= (int)sizeof(kb.backup)) len = (int)sizeof(kb.backup) - 1;
    memcpy(kb.backup, buf, len);
    kb.backup[len] = '\0';

    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    lv_coord_t sh = lv_disp_get_ver_res(NULL);

    /* Scrim */
    kb.scrim = lv_obj_create(lv_scr_act());
    lv_obj_set_size(kb.scrim, sw, sh);
    lv_obj_set_pos(kb.scrim, 0, 0);
    lv_obj_add_style(kb.scrim, &overlay_scrim_style, LV_PART_MAIN);
    lv_obj_clear_flag(kb.scrim, LV_OBJ_FLAG_SCROLLABLE);

    /* Text bar (shared by both modes) */
    kb_build_text_bar();

    /* Controls hints */
    kb_build_hints();

    /* Mode-specific UI */
    if (mode == DASH_KB_MODE_FULL) {
        kb_build_full();
    } else {
        kb_build_numeric();
    }

    /* Focus object — invisible, receives keys */
    kb.focus_obj = lv_obj_create(kb.scrim);
    lv_obj_set_size(kb.focus_obj, 0, 0);
    lv_obj_add_flag(kb.focus_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(kb.focus_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_group_add_obj(lv_group_get_default(), kb.focus_obj);
    lv_obj_add_event_cb(kb.focus_obj, kb_key_handler, LV_EVENT_KEY, NULL);
    dash_focus_change_depth(kb.focus_obj);

    /* Stick polling (full mode only) */
    if (mode == DASH_KB_MODE_FULL) {
        kb.stick_timer = lv_timer_create(kb_stick_poll, 16, NULL);
    }

    /* Cursor blink */
    kb.blink_timer = lv_timer_create(kb_blink_cb, 500, NULL);

    /* Animate in */
    if (mode == DASH_KB_MODE_FULL) {
        dash_anim_overlay_in(kb.flower_bg, 220);
    } else {
        dash_anim_overlay_in(kb.digit_strip, 220);
    }
    dash_anim_overlay_in(kb.text_bar, 220);

    kb_update_text();
}

void dash_keyboard_close(bool confirm)
{
    if (!kb.open) return;
    kb.open = false;

    if (!confirm) {
        /* Restore original text */
        strncpy(kb.buf, kb.backup, kb.buf_size - 1);
        kb.buf[kb.buf_size - 1] = '\0';
    }

    /* Stop timers */
    if (kb.stick_timer) {
        lv_timer_del(kb.stick_timer);
        kb.stick_timer = NULL;
    }
    if (kb.blink_timer) {
        lv_timer_del(kb.blink_timer);
        kb.blink_timer = NULL;
    }

    /* Pop focus */
    dash_focus_pop_depth();

    /* Delete UI */
    if (kb.scrim) {
        lv_obj_del(kb.scrim);
        kb.scrim = NULL;
    }

    /* Notify caller on confirm */
    if (confirm && kb.on_done) {
        kb.on_done();
    }

    /* Clear object pointers */
    kb.focus_obj = NULL;
    kb.text_bar = NULL;
    kb.text_label = NULL;
    kb.hint_bar = NULL;
    kb.flower_bg = NULL;
    kb.center_label = NULL;
    kb.digit_strip = NULL;
    memset(kb.petal_obj, 0, sizeof(kb.petal_obj));
    memset(kb.char_circle, 0, sizeof(kb.char_circle));
    memset(kb.char_label, 0, sizeof(kb.char_label));
    memset(kb.digit_cell, 0, sizeof(kb.digit_cell));
    memset(kb.digit_label, 0, sizeof(kb.digit_label));
}

bool dash_keyboard_is_open(void)
{
    return kb.open;
}

/* ══════════════════════════════════════════════════════════════════
 *  Build: Text bar (shared)
 * ══════════════════════════════════════════════════════════════════ */

static void kb_build_text_bar(void)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    int bar_x = (sw - TEXT_BAR_W) / 2;

    kb.text_bar = lv_obj_create(kb.scrim);
    lv_obj_set_size(kb.text_bar, TEXT_BAR_W, TEXT_BAR_H);
    lv_obj_set_pos(kb.text_bar, bar_x, TEXT_BAR_Y);
    lv_obj_set_style_bg_color(kb.text_bar, lv_color_hex(0x1e2326), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(kb.text_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb.text_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(kb.text_bar, lv_color_hex(0x414b50), LV_PART_MAIN);
    lv_obj_set_style_border_opa(kb.text_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(kb.text_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_left(kb.text_bar, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(kb.text_bar, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(kb.text_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(kb.text_bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(kb.text_bar, LV_OBJ_FLAG_SCROLLABLE);

    kb.text_label = lv_label_create(kb.text_bar);
    lv_obj_set_style_text_font(kb.text_label, &lv_font_jetbrains_mono_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(kb.text_label, EF_FG, LV_PART_MAIN);
    lv_obj_center(kb.text_label);
}

/* ══════════════════════════════════════════════════════════════════
 *  Build: Controls hint bar
 * ══════════════════════════════════════════════════════════════════ */

/* Create a hint: pre-rendered icon image + label text */
static lv_obj_t *kb_hint_img(lv_obj_t *parent, const lv_img_dsc_t *icon,
                              const char *overlay_text, const char *label_text)
{
    lv_obj_t *hint = lv_obj_create(parent);
    strip_obj_defaults(hint);
    lv_obj_set_size(hint, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(hint, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hint, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(hint, 6, LV_PART_MAIN);
    lv_obj_set_flex_align(hint, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Icon container (image + optional text overlay) */
    lv_obj_t *ic = lv_obj_create(hint);
    lv_obj_set_size(ic, icon->header.w, icon->header.h);
    strip_obj_defaults(ic);

    lv_obj_t *img = lv_img_create(ic);
    lv_img_set_src(img, icon);
    lv_obj_center(img);

    if (overlay_text) {
        lv_obj_t *otxt = lv_label_create(ic);
        lv_label_set_text(otxt, overlay_text);
        lv_obj_set_style_text_font(otxt, &dash_font_ui_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(otxt, lv_color_hex(0x0b0d0e), LV_PART_MAIN);
        lv_obj_center(otxt);
    }

    /* Label */
    lv_obj_t *lbl = lv_label_create(hint);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &dash_font_ui_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, EF_FG_MUTED, LV_PART_MAIN);

    return hint;
}

/* Pill hint: pre-rendered pill image + text overlay + label */
static lv_obj_t *kb_hint_pill(lv_obj_t *parent, const lv_img_dsc_t *icon,
                               const char *pill_text, const char *label_text)
{
    lv_obj_t *hint = lv_obj_create(parent);
    strip_obj_defaults(hint);
    lv_obj_set_size(hint, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(hint, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hint, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(hint, 6, LV_PART_MAIN);
    lv_obj_set_flex_align(hint, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *ic = lv_obj_create(hint);
    lv_obj_set_size(ic, icon->header.w, icon->header.h);
    strip_obj_defaults(ic);

    lv_obj_t *img = lv_img_create(ic);
    lv_img_set_src(img, icon);
    lv_obj_center(img);

    lv_obj_t *ptxt = lv_label_create(ic);
    lv_label_set_text(ptxt, pill_text);
    lv_obj_set_style_text_font(ptxt, &dash_font_ui_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(ptxt, EF_FG_MUTED, LV_PART_MAIN);
    lv_obj_center(ptxt);

    lv_obj_t *lbl = lv_label_create(hint);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &dash_font_ui_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, EF_FG_MUTED, LV_PART_MAIN);

    return hint;
}

/* Helper: create a flex row for hints */
static lv_obj_t *kb_hint_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    strip_obj_defaults(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 18, LV_PART_MAIN);
    return row;
}

static void kb_build_hints(void)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);

    /* Container: rows stacked vertically */
    kb.hint_bar = lv_obj_create(kb.scrim);
    lv_obj_set_size(kb.hint_bar, sw, LV_SIZE_CONTENT);
    strip_obj_defaults(kb.hint_bar);
    lv_obj_set_layout(kb.hint_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(kb.hint_bar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(kb.hint_bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(kb.hint_bar, 4, LV_PART_MAIN);
    lv_obj_align(kb.hint_bar, LV_ALIGN_TOP_MID, 0, TEXT_BAR_Y + TEXT_BAR_H + 6);

    if (kb.mode == DASH_KB_MODE_FULL) {
        /* Row 1: White, Black, LT, RT */
        lv_obj_t *r1 = kb_hint_row(kb.hint_bar);
        kb_hint_img(r1, &pill_kb_white, NULL, "Backspace");
        kb_hint_img(r1, &pill_kb_black, NULL, "Space");
        kb_hint_img(r1, &pill_kb_lt,    "LT", "Numbers");
        kb_hint_img(r1, &pill_kb_rt,    "RT", "Caps");
        /* Row 2: START, BACK */
        lv_obj_t *r2 = kb_hint_row(kb.hint_bar);
        kb_hint_pill(r2, &pill_kb_start, "START", "Done");
        kb_hint_pill(r2, &pill_kb_back,  "BACK",  "Cancel");
    } else {
        lv_obj_t *r1 = kb_hint_row(kb.hint_bar);
        kb_hint_img(r1, &pill_kb_white, NULL, "Backspace");
        lv_obj_t *r2 = kb_hint_row(kb.hint_bar);
        kb_hint_pill(r2, &pill_kb_start, "START", "Done");
        kb_hint_pill(r2, &pill_kb_back,  "BACK",  "Cancel");
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  Build: Full daisywheel mode
 * ══════════════════════════════════════════════════════════════════ */

static void kb_build_full(void)
{
    /* Flower background circle */
    int fx = FLOWER_CX - FLOWER_SIZE / 2;
    int fy = FLOWER_CY - FLOWER_SIZE / 2;
    kb.flower_bg = lv_obj_create(kb.scrim);
    lv_obj_set_size(kb.flower_bg, FLOWER_SIZE, FLOWER_SIZE);
    lv_obj_set_pos(kb.flower_bg, fx, fy);
    lv_obj_set_style_bg_color(kb.flower_bg, lv_color_hex(0x272e33), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(kb.flower_bg, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(kb.flower_bg, FLOWER_SIZE / 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb.flower_bg, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(kb.flower_bg, lv_color_hex(0x414b50), LV_PART_MAIN);
    lv_obj_set_style_border_opa(kb.flower_bg, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb.flower_bg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(kb.flower_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* Center label (charset indicator) */
    kb.center_label = lv_label_create(kb.flower_bg);
    lv_obj_set_style_text_font(kb.center_label, &dash_font_ui_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(kb.center_label, lv_color_hex(0x9da9a0), LV_PART_MAIN);
    lv_label_set_text(kb.center_label, charset_names[0]);
    lv_obj_center(kb.center_label);

    /* 8 petals */
    for (int i = 0; i < NUM_PETALS; i++) {
        int px = FLOWER_SIZE / 2 + petal_offset[i].x - PETAL_SIZE / 2;
        int py = FLOWER_SIZE / 2 + petal_offset[i].y - PETAL_SIZE / 2;

        lv_obj_t *petal = lv_obj_create(kb.flower_bg);
        lv_obj_set_size(petal, PETAL_SIZE, PETAL_SIZE);
        lv_obj_set_pos(petal, px, py);
        lv_obj_set_style_bg_color(petal, lv_color_hex(0x2e383c), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(petal, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(petal, 12, LV_PART_MAIN);
        lv_obj_set_style_border_width(petal, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(petal, 0, LV_PART_MAIN);
        lv_obj_clear_flag(petal, LV_OBJ_FLAG_SCROLLABLE);
        kb.petal_obj[i] = petal;

        /* 4 character positions: X=left, Y=top, B=right, A=bottom.
         * Each has a colored circle (hidden until petal active) + label. */
        static const lv_align_t align_map[4] = {
            LV_ALIGN_LEFT_MID, LV_ALIGN_TOP_MID,
            LV_ALIGN_RIGHT_MID, LV_ALIGN_BOTTOM_MID
        };
        static const int ofs_x[4] = { 10,  0, -10,  0 };
        static const int ofs_y[4] = {  0,  8,   0, -8 };

        for (int c = 0; c < CHARS_PER_PETAL; c++) {
            /* Container for circle + label pair */
            lv_obj_t *wrap = lv_obj_create(petal);
            lv_obj_set_size(wrap, CHAR_CIRCLE_SIZE, CHAR_CIRCLE_SIZE);
            strip_obj_defaults(wrap);
            lv_obj_align(wrap, align_map[c], ofs_x[c], ofs_y[c]);

            /* Colored circle background — hidden by default */
            lv_obj_t *circ = lv_obj_create(wrap);
            lv_obj_set_size(circ, CHAR_CIRCLE_SIZE, CHAR_CIRCLE_SIZE);
            lv_obj_set_style_radius(circ, CHAR_CIRCLE_SIZE / 2, LV_PART_MAIN);
            lv_obj_set_style_bg_color(circ, lv_color_hex(char_colors[c]), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(circ, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(circ, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(circ, 0, LV_PART_MAIN);
            lv_obj_clear_flag(circ, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(circ, LV_OBJ_FLAG_HIDDEN);  /* hidden until active */
            lv_obj_center(circ);
            kb.char_circle[i][c] = circ;

            /* Character label on top */
            lv_obj_t *lbl = lv_label_create(wrap);
            lv_obj_set_style_text_font(lbl, &dash_font_ui_14, LV_PART_MAIN);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x9da9a0), LV_PART_MAIN);
            char txt[2] = { charsets[0][i * CHARS_PER_PETAL + c], '\0' };
            lv_label_set_text(lbl, txt);
            lv_obj_center(lbl);
            kb.char_label[i][c] = lbl;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  Build: Numeric strip mode
 * ══════════════════════════════════════════════════════════════════ */

static void kb_build_numeric(void)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    lv_coord_t sh = lv_disp_get_ver_res(NULL);
    int strip_w = NUM_DIGITS * (DIGIT_CELL_SIZE + DIGIT_CELL_GAP) - DIGIT_CELL_GAP;
    int strip_x = (sw - strip_w) / 2;
    int strip_y = sh / 2 - 60;

    kb.digit_strip = lv_obj_create(kb.scrim);
    lv_obj_set_size(kb.digit_strip, strip_w + 16, DIGIT_CELL_SIZE + 16);
    lv_obj_set_pos(kb.digit_strip, strip_x - 8, strip_y - 8);
    lv_obj_set_style_bg_color(kb.digit_strip, lv_color_hex(0x272e33), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(kb.digit_strip, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(kb.digit_strip, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb.digit_strip, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(kb.digit_strip, lv_color_hex(0x414b50), LV_PART_MAIN);
    lv_obj_set_style_border_opa(kb.digit_strip, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb.digit_strip, 0, LV_PART_MAIN);
    lv_obj_clear_flag(kb.digit_strip, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < NUM_DIGITS; i++) {
        int cx = 8 + i * (DIGIT_CELL_SIZE + DIGIT_CELL_GAP);

        lv_obj_t *cell = lv_obj_create(kb.digit_strip);
        lv_obj_set_size(cell, DIGIT_CELL_SIZE, DIGIT_CELL_SIZE);
        lv_obj_set_pos(cell, cx, 8);
        lv_obj_set_style_bg_color(cell, lv_color_hex(0x2e383c), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(cell, 8, LV_PART_MAIN);
        lv_obj_set_style_border_width(cell, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(cell, 0, LV_PART_MAIN);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        kb.digit_cell[i] = cell;

        lv_obj_t *lbl = lv_label_create(cell);
        lv_obj_set_style_text_font(lbl, &dash_font_ui_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x9da9a0), LV_PART_MAIN);
        char txt[2] = { numeric_chars[i], '\0' };
        lv_label_set_text(lbl, txt);
        lv_obj_center(lbl);
        kb.digit_label[i] = lbl;
    }

    /* Highlight initial selection */
    kb_highlight_digit(0);
}

/* ══════════════════════════════════════════════════════════════════
 *  Text display update
 * ══════════════════════════════════════════════════════════════════ */

static void kb_update_text(void)
{
    if (!kb.text_label) return;

    int len = (int)strlen(kb.buf);
    if (kb.cursor > len) kb.cursor = len;
    if (kb.cursor < 0) kb.cursor = 0;

    /* Build display string with cursor */
    static char display[320];
    int pos = 0;
    for (int i = 0; i < len && pos < (int)sizeof(display) - 4; i++) {
        if (i == kb.cursor && kb.cursor_visible) {
            display[pos++] = '|';
        }
        display[pos++] = kb.buf[i];
    }
    if (kb.cursor == len && kb.cursor_visible) {
        display[pos++] = '|';
    }
    display[pos] = '\0';

    if (pos == 0) {
        lv_label_set_text(kb.text_label, kb.cursor_visible ? "|" : "");
    } else {
        lv_label_set_text(kb.text_label, display);
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  Charset switching (full mode)
 * ══════════════════════════════════════════════════════════════════ */

static int kb_active_charset(void)
{
    return kb.held_charset >= 0 ? kb.held_charset : kb.current_charset;
}

static void kb_update_charset_labels(void)
{
    int cs = kb_active_charset();
    const char *chars = charsets[cs];

    if (kb.center_label)
        lv_label_set_text(kb.center_label, charset_names[cs]);

    for (int i = 0; i < NUM_PETALS; i++) {
        for (int c = 0; c < CHARS_PER_PETAL; c++) {
            if (kb.char_label[i][c]) {
                char txt[2] = { chars[i * CHARS_PER_PETAL + c], '\0' };
                lv_label_set_text(kb.char_label[i][c], txt);
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  Petal highlight / unhighlight
 * ══════════════════════════════════════════════════════════════════ */

static void kb_highlight_petal(int idx)
{
    if (idx < 0 || idx >= NUM_PETALS) return;

    /* Bring to front so it renders above overlapping neighbors */
    lv_obj_move_foreground(kb.petal_obj[idx]);

    lv_obj_set_style_bg_color(kb.petal_obj[idx], lv_color_hex(0x374145), LV_PART_MAIN);
    lv_obj_set_style_border_width(kb.petal_obj[idx], 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(kb.petal_obj[idx], lv_color_hex(0xa7c080), LV_PART_MAIN);
    lv_obj_set_style_border_opa(kb.petal_obj[idx], LV_OPA_60, LV_PART_MAIN);

    /* Show colored circles, black text on circles */
    for (int c = 0; c < CHARS_PER_PETAL; c++) {
        lv_obj_clear_flag(kb.char_circle[idx][c], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(kb.char_circle[idx][c], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(kb.char_label[idx][c],
                                    lv_color_hex(0x0b0d0e), LV_PART_MAIN);
    }
}

static void kb_unhighlight_petal(int idx)
{
    if (idx < 0 || idx >= NUM_PETALS) return;

    lv_obj_set_style_bg_color(kb.petal_obj[idx], lv_color_hex(0x2e383c), LV_PART_MAIN);
    lv_obj_set_style_border_width(kb.petal_obj[idx], 0, LV_PART_MAIN);

    /* Hide colored circles, muted text */
    for (int c = 0; c < CHARS_PER_PETAL; c++) {
        lv_obj_add_flag(kb.char_circle[idx][c], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(kb.char_label[idx][c],
                                    lv_color_hex(0x9da9a0), LV_PART_MAIN);
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  Digit highlight (numeric mode)
 * ══════════════════════════════════════════════════════════════════ */

static void kb_highlight_digit(int idx)
{
    if (idx < 0 || idx >= NUM_DIGITS) return;

    for (int i = 0; i < NUM_DIGITS; i++) {
        if (!kb.digit_cell[i]) continue;
        if (i == idx) {
            lv_obj_set_style_bg_color(kb.digit_cell[i], lv_color_hex(0x374145), LV_PART_MAIN);
            lv_obj_set_style_border_width(kb.digit_cell[i], 2, LV_PART_MAIN);
            lv_obj_set_style_border_color(kb.digit_cell[i], lv_color_hex(0xa7c080), LV_PART_MAIN);
            lv_obj_set_style_border_opa(kb.digit_cell[i], LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(kb.digit_label[i], EF_FG, LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(kb.digit_cell[i], lv_color_hex(0x2e383c), LV_PART_MAIN);
            lv_obj_set_style_border_width(kb.digit_cell[i], 0, LV_PART_MAIN);
            lv_obj_set_style_text_color(kb.digit_label[i], lv_color_hex(0x9da9a0), LV_PART_MAIN);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  Character insertion / deletion
 * ══════════════════════════════════════════════════════════════════ */

static void kb_insert_char(char c)
{
    int len = (int)strlen(kb.buf);
    if (len >= kb.buf_size - 1) return;    /* buffer full */

    /* Shift right from cursor */
    memmove(&kb.buf[kb.cursor + 1], &kb.buf[kb.cursor], len - kb.cursor + 1);
    kb.buf[kb.cursor] = c;
    kb.cursor++;
    kb_update_text();
}

static void kb_backspace(void)
{
    if (kb.cursor <= 0) return;
    int len = (int)strlen(kb.buf);
    memmove(&kb.buf[kb.cursor - 1], &kb.buf[kb.cursor], len - kb.cursor + 1);
    kb.cursor--;
    kb_update_text();
}

/* ══════════════════════════════════════════════════════════════════
 *  Stick polling timer (full mode)
 * ══════════════════════════════════════════════════════════════════ */

static void kb_stick_poll(lv_timer_t *t)
{
    (void)t;
    if (!kb.open || kb.mode != DASH_KB_MODE_FULL) return;

    /* Stick → petal selection */
    int sx, sy;
    lv_port_indev_get_stick(&sx, &sy);
    int new_petal = get_petal_from_stick(sx, sy);

    if (new_petal != kb.current_petal) {
        kb_unhighlight_petal(kb.current_petal);
        kb.current_petal = new_petal;
        kb_highlight_petal(kb.current_petal);
    }

    /* Triggers → hold-to-shift charset */
    int lt, rt;
    lv_port_indev_get_triggers(&lt, &rt);

    int new_held = -1;
    if (lt > 0x20)      new_held = 2;   /* LT held = numbers */
    else if (rt > 0x20) new_held = 1;   /* RT held = uppercase */

    if (new_held != kb.held_charset) {
        kb.held_charset = new_held;
        kb_update_charset_labels();
        kb_highlight_petal(kb.current_petal);   /* re-color active petal */
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  Cursor blink timer
 * ══════════════════════════════════════════════════════════════════ */

static void kb_blink_cb(lv_timer_t *t)
{
    (void)t;
    if (!kb.open) return;
    kb.cursor_visible = !kb.cursor_visible;
    kb_update_text();
}

/* ══════════════════════════════════════════════════════════════════
 *  Key event handler
 * ══════════════════════════════════════════════════════════════════ */

static void kb_select_char_in_petal(int char_idx)
{
    if (kb.current_petal < 0 || kb.current_petal >= NUM_PETALS) return;
    if (char_idx < 0 || char_idx >= CHARS_PER_PETAL) return;

    int cs = kb_active_charset();
    char c = charsets[cs][kb.current_petal * CHARS_PER_PETAL + char_idx];
    kb_insert_char(c);

    /* Flash feedback — briefly brighten the circle */
    lv_obj_t *circ = kb.char_circle[kb.current_petal][char_idx];
    if (circ) {
        lv_obj_set_style_bg_opa(circ, LV_OPA_80, LV_PART_MAIN);
        /* Restored on next highlight update */
    }
}

static void kb_key_handler(lv_event_t *event)
{
    if (!kb.open) return;

    lv_key_t key = *((lv_key_t *)lv_event_get_param(event));

    /* ── Shared keys (both modes) ── */

    /* START = confirm */
    if (key == DASH_KEY_START) {
        dash_keyboard_close(true);
        return;
    }

    /* BACK = cancel */
    if (key == DASH_KEY_BACK) {
        dash_keyboard_close(false);
        return;
    }

    /* White = backspace */
    if (key == DASH_KEY_WHITE) {
        kb_backspace();
        return;
    }

    /* Black = space */
    if (key == DASH_KEY_BLACK) {
        kb_insert_char(' ');
        return;
    }

    /* ── Full daisywheel mode ── */
    if (kb.mode == DASH_KB_MODE_FULL) {

        /* D-pad Left/Right = cursor move */
        if (key == LV_KEY_LEFT) {
            if (kb.cursor > 0) kb.cursor--;
            kb.cursor_visible = true;
            kb_update_text();
            return;
        }
        if (key == LV_KEY_RIGHT) {
            int len = (int)strlen(kb.buf);
            if (kb.cursor < len) kb.cursor++;
            kb.cursor_visible = true;
            kb_update_text();
            return;
        }

        /* Face buttons → character selection */
        if (key == DASH_KEY_A) { kb_select_char_in_petal(3); return; }  /* A = bottom */
        if (key == DASH_KEY_B) { kb_select_char_in_petal(2); return; }  /* B = right  */
        if (key == DASH_KEY_X) { kb_select_char_in_petal(0); return; }  /* X = left   */
        if (key == DASH_KEY_Y) { kb_select_char_in_petal(1); return; }  /* Y = top    */

        /* LT/RT charset switching handled by stick_poll timer */
        return;
    }

    /* ── Numeric mode ── */
    if (kb.mode == DASH_KB_MODE_NUMERIC) {

        if (key == LV_KEY_LEFT) {
            if (kb.digit_selected > 0)
                kb.digit_selected--;
            else
                kb.digit_selected = NUM_DIGITS - 1;
            kb_highlight_digit(kb.digit_selected);
            return;
        }
        if (key == LV_KEY_RIGHT) {
            if (kb.digit_selected < NUM_DIGITS - 1)
                kb.digit_selected++;
            else
                kb.digit_selected = 0;
            kb_highlight_digit(kb.digit_selected);
            return;
        }
        if (key == DASH_KEY_A) {
            kb_insert_char(numeric_chars[kb.digit_selected]);
            return;
        }
        return;
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  Remote debug snapshot
 * ══════════════════════════════════════════════════════════════════ */

static int kb_snapshot(char *buf, int size)
{
    if (!kb.open) return 0;

    if (kb.mode == DASH_KB_MODE_FULL) {
        return snprintf(buf, size,
            "keyboard=open mode=full charset=%s cursor=%d petal=%d text=\"%s\"\n",
            charset_names[kb_active_charset()], kb.cursor, kb.current_petal, kb.buf);
    } else {
        return snprintf(buf, size,
            "keyboard=open mode=numeric cursor=%d selected=%d text=\"%s\"\n",
            kb.cursor, kb.digit_selected, kb.buf);
    }
}
