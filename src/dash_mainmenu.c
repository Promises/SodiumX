// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2022 Ryzee119

#include "sodiumx.h"
#include "dash_anim.h"

/* ============================================================
 *  Menu item callbacks (unchanged from original)
 * ============================================================ */
static void dash_system_info(void *param);
static void dash_utilities(void *param);
static void dash_settings_page(void *param);
static void dash_settings_new(void *param);
static void dash_rescan_library(void *param);
static void dash_launch_msdash(void *param);
static void dash_launch_dvd(void *param);
static void dash_open_about(void *param);
static void dash_reboot(void *param);
static void dash_shutdown(void *param);

/* ============================================================
 *  Menu state
 * ============================================================ */
typedef struct {
    const char *label;
    const char *icon_symbol;
    void (*cb)(void *param);
    const char *confirm_text;
} mainmenu_item_t;

static const mainmenu_item_t menu_items[] = {
    {"System Information", LV_SYMBOL_SETTINGS,  dash_system_info, NULL},
    {"Rescan Library",     LV_SYMBOL_REFRESH,   dash_rescan_library, NULL},
    {"Launch MS Dashboard", LV_SYMBOL_RIGHT,    dash_launch_msdash, "Accept \"Launch MS Dashboard\""},
    {"Launch DVD",          LV_SYMBOL_AUDIO,    dash_launch_dvd, "Accept \"Launch DVD\""},
    {"Utilities",           LV_SYMBOL_LIST,     dash_utilities, NULL},
    {"Settings",            LV_SYMBOL_SETTINGS, dash_settings_new, NULL},
    {"Settings (Legacy)",   LV_SYMBOL_SETTINGS, dash_settings_page, NULL},
    {"About SodiumX",       LV_SYMBOL_EYE_OPEN, dash_open_about, NULL},
    {"Reboot",              LV_SYMBOL_REFRESH,  dash_reboot, "Accept \"Reboot\""},
    {"Shutdown",            LV_SYMBOL_POWER,    dash_shutdown, "Accept \"Shutdown\""},
};
#define MENU_ITEM_COUNT (sizeof(menu_items) / sizeof(menu_items[0]))

static bool start_menu_open = false;
static lv_obj_t *overlay;
static lv_obj_t *menu_card;
static lv_obj_t *item_objs[12];
static int menu_selected = 0;

/* ============================================================
 *  Item highlight
 * ============================================================ */
static void highlight_item(int index)
{
    for (int i = 0; i < (int)MENU_ITEM_COUNT; i++)
    {
        lv_obj_remove_style(item_objs[i], &overlay_item_focused_style, LV_PART_MAIN);
        lv_obj_add_style(item_objs[i], &overlay_item_style, LV_PART_MAIN);
        lv_obj_set_style_translate_x(item_objs[i], 0, LV_PART_MAIN);
    }

    if (index >= 0 && index < (int)MENU_ITEM_COUNT)
    {
        lv_obj_remove_style(item_objs[index], &overlay_item_style, LV_PART_MAIN);
        lv_obj_add_style(item_objs[index], &overlay_item_focused_style, LV_PART_MAIN);
        /* Animate translateX(4px) */
        dash_anim_x(item_objs[index],
                     lv_obj_get_style_translate_x(item_objs[index], LV_PART_MAIN), 4, 200);
    }
}

/* ============================================================
 *  Close the menu
 * ============================================================ */
static void mainmenu_close(void)
{
    if (!start_menu_open || !overlay) return;
    start_menu_open = false;

    /* Animate out, then destroy */
    dash_anim_overlay_out(menu_card, 200, NULL);

    /* Delete after animation (use timer to delay destruction) */
    lv_obj_del_delayed(overlay, 220);
    overlay = NULL;
    menu_card = NULL;

    dash_focus_pop_depth();
}

/* ============================================================
 *  Key handler on the menu card
 * ============================================================ */
static void menu_key_handler(lv_event_t *event)
{
    lv_key_t key = *((lv_key_t *)lv_event_get_param(event));

    if (key == LV_KEY_ESC || key == DASH_SETTINGS_PAGE)
    {
        mainmenu_close();
        return;
    }

    if (key == LV_KEY_DOWN)
    {
        menu_selected = (menu_selected + 1) % (int)MENU_ITEM_COUNT;
        highlight_item(menu_selected);
    }
    else if (key == LV_KEY_UP)
    {
        menu_selected = (menu_selected - 1 + (int)MENU_ITEM_COUNT) % (int)MENU_ITEM_COUNT;
        highlight_item(menu_selected);
    }
    else if (key == LV_KEY_ENTER)
    {
        const mainmenu_item_t *item = &menu_items[menu_selected];
        if (item->confirm_text)
        {
            confirmbox_open(item->confirm_text, item->cb, NULL);
        }
        else if (item->cb)
        {
            item->cb(NULL);
        }
    }
}

/* ============================================================
 *  Open the Start menu
 * ============================================================ */
void dash_mainmenu_open()
{
    if (start_menu_open)
    {
        mainmenu_close();
        return;
    }
    start_menu_open = true;
    menu_selected = 0;

    /* Fullscreen overlay container — acts as the scrim */
    overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(overlay, lv_obj_get_width(lv_scr_act()), lv_obj_get_height(lv_scr_act()));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x0b0d0e), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, 140, LV_PART_MAIN); /* 55% scrim */
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Centered card */
    menu_card = lv_obj_create(overlay);
    lv_obj_add_style(menu_card, &overlay_card_style, LV_PART_MAIN);
    lv_obj_set_width(menu_card, 420);
    lv_obj_set_height(menu_card, LV_SIZE_CONTENT);
    lv_obj_center(menu_card);
    lv_obj_set_layout(menu_card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(menu_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(menu_card, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(menu_card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(menu_card, LV_OBJ_FLAG_SCROLLABLE);

    /* Card header */
    lv_obj_t *header = lv_obj_create(menu_card);
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

    /* Header left: title column */
    lv_obj_t *title_col = lv_obj_create(header);
    lv_obj_set_size(title_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(title_col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(title_col, 0, LV_PART_MAIN);
    lv_obj_set_layout(title_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(title_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(title_col, 2, LV_PART_MAIN);
    lv_obj_clear_flag(title_col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *eyebrow = lv_label_create(title_col);
    lv_obj_add_style(eyebrow, &eyebrow_style, LV_PART_MAIN);
    lv_obj_set_style_text_color(eyebrow, dash_accent_color, LV_PART_MAIN);
    lv_label_set_text(eyebrow, "SODIUMX");

    lv_obj_t *title = lv_label_create(title_col);
    lv_obj_set_style_text_font(title, &dash_font_ui_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(title, "Main Menu");

    /* Header right: hint */
    lv_obj_t *hint = lv_label_create(header);
    lv_obj_add_style(hint, &mono_small_style, LV_PART_MAIN);
    lv_label_set_text(hint, "START " LV_SYMBOL_RIGHT " close");

    /* Item list */
    lv_obj_t *list = lv_obj_create(menu_card);
    lv_obj_set_size(list, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(list, 0, LV_PART_MAIN);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < (int)MENU_ITEM_COUNT; i++)
    {
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
        lv_obj_set_style_bg_opa(icon_tile, 15, LV_PART_MAIN); /* ~6% */
        lv_obj_set_style_border_width(icon_tile, 0, LV_PART_MAIN);
        lv_obj_clear_flag(icon_tile, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *icon_lbl = lv_label_create(icon_tile);
        lv_label_set_text(icon_lbl, menu_items[i].icon_symbol);
        lv_obj_set_style_text_color(icon_lbl, EF_FG_MUTED, LV_PART_MAIN);
        lv_obj_set_style_text_font(icon_lbl, &dash_font_ui_14, LV_PART_MAIN);
        lv_obj_center(icon_lbl);

        /* Label */
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, menu_items[i].label);
        lv_obj_set_style_text_font(lbl, &dash_font_ui_16, LV_PART_MAIN);
        lv_obj_set_flex_grow(lbl, 1);

        /* Chevron */
        lv_obj_t *chevron = lv_label_create(row);
        lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chevron, EF_FG_MUTED, LV_PART_MAIN);
        lv_obj_set_style_opa(chevron, 128, LV_PART_MAIN);

        item_objs[i] = row;
    }

    /* Highlight first item */
    highlight_item(0);

    /* Entry animation */
    dash_anim_overlay_in(menu_card, 300);

    /* Focus management */
    lv_group_add_obj(lv_group_get_default(), menu_card);
    lv_obj_add_event_cb(menu_card, menu_key_handler, LV_EVENT_KEY, NULL);
    dash_focus_change_depth(menu_card);
}

/* ============================================================
 *  Callback implementations (mostly unchanged)
 * ============================================================ */
static void dash_system_info(void *param)
{
    (void)param;
    lv_obj_t *window = container_open();
    platform_system_info(window);
}

static void dash_launch_msdash(void *param)
{
    (void)param;
    strcpy(dash_launch_path, "__MSDASH__");
    lv_set_quit(LV_QUIT_OTHER);
}

static void dash_launch_dvd(void *param)
{
    #ifdef NXDK
    ULONG tray_state = 0x70;
    NTSTATUS status = HalReadSMCTrayState(&tray_state, NULL);
    if (!NT_SUCCESS(status) || tray_state != 0x60) return;
    #endif
    (void)param;
    strcpy(dash_launch_path, "__DVD__");
    lv_set_quit(LV_QUIT_OTHER);
}

static void dash_flush_cache(void *param)
{
    (void)param;
    platform_flush_cache();
}

static void item_launch_abort(lv_event_t *event)
{
    lv_mem_free(lv_event_get_user_data(event));
}

static void item_launch(void *param)
{
    dash_launcher_go(param);
}

static bool dash_browser_item_selection_cb(const char *selected_path)
{
    char cb_text[DASH_MAX_PATH];
    if (dash_launcher_is_launchable(selected_path))
    {
        lv_snprintf(cb_text, DASH_MAX_PATH, "Launch \"%s\"", selected_path);
        char *t = lv_mem_alloc(DASH_MAX_PATH);
        strncpy(t, selected_path, DASH_MAX_PATH - 1);
        lv_obj_t *cb = confirmbox_open(cb_text, item_launch, t);
        lv_obj_add_event_cb(cb, item_launch_abort, LV_EVENT_DELETE, t);
    }
    return false;
}

static void dash_open_xbe_launcher(void *param)
{
    (void)param;
    dash_browser_open(DASH_ROOT_PATH, dash_browser_item_selection_cb);
}

static void dash_open_eeprom_config(void *param)
{
    (void)param;
    dash_eeprom_settings_open();
}

static void dash_rebuild_database(void *param)
{
    (void)param;
    db_command_with_callback(SQL_TITLE_DELETE_ENTRIES, NULL, NULL);
}

/* ── Rescan library ── */
static void rescan_page_cb(void *param)
{
    const char *page_name = (const char *)param;
    dash_scroller_rescan_page(page_name);
}

static void rescan_all_cb(void *param)
{
    (void)param;
    int cnt = dash_scroller_get_page_count();
    for (int i = 0; i < cnt; i++)
    {
        const char *name = dash_scroller_get_title(i);
        if (name && strcmp(name, "Recent") != 0)
        {
            dash_scroller_rescan_page(name);
        }
    }
}

static void dash_rescan_library(void *param)
{
    (void)param;
    int page_cnt = dash_scroller_get_page_count();

    /* Build submenu: "All" + each non-Recent page */
    int item_cnt = 0;
    menu_items_t items[DASH_MAX_PAGES + 1];

    items[item_cnt].str = "Rescan All";
    items[item_cnt].cb = rescan_all_cb;
    items[item_cnt].callback_param = NULL;
    items[item_cnt].confirm_box = "Accept \"Rescan All\"";
    item_cnt++;

    for (int i = 0; i < page_cnt; i++)
    {
        const char *name = dash_scroller_get_title(i);
        if (!name || strcmp(name, "Recent") == 0) continue;
        items[item_cnt].str = name;
        items[item_cnt].cb = rescan_page_cb;
        items[item_cnt].callback_param = (void *)name;
        items[item_cnt].confirm_box = NULL;
        item_cnt++;
    }

    menu_open(items, item_cnt);
}

static void dash_clear_recent(void *param)
{
    (void)param;
    platform_get_iso8601_time(dash_settings.earliest_recent_date);
    dash_settings_apply(false);
    static const char *cmd = "DELETE FROM " SQL_TITLES_NAME " WHERE page = \"__RECENT__\"";
    db_command_with_callback(cmd, NULL, NULL);
    dash_scroller_clear_page("Recent");
}

static void dash_open_about(void *param)
{
    (void)param;
    const char *url = "https://github.com/Promises/SodiumX";

    lv_obj_t *window = container_open();
    lv_obj_t *qr = lv_qrcode_create(window, 256, lv_color_black(), lv_color_white());
    lv_qrcode_update(qr, url, strlen(url));
    lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_update_layout(window);

    lv_obj_t *label = lv_label_create(window);
    lv_label_set_text(label, "See github.com/Promises/SodiumX");
    lv_obj_update_layout(label);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_height(window, lv_obj_get_height(qr) + lv_obj_get_height(label) + 2);
    lv_obj_update_layout(window);
}

static void dash_reboot(void *param)
{
    (void)param;
    lv_set_quit(LV_REBOOT);
}

static void dash_shutdown(void *param)
{
    (void)param;
    lv_set_quit(LV_SHUTDOWN);
}

static void dash_settings_page(void *param)
{
    (void)param;
    dash_settings_open();
}

static void dash_settings_new(void *param)
{
    (void)param;
    extern void dash_settings_new_open(void);
    dash_settings_new_open();
}

static void dash_utilities(void *param)
{
    (void)param;
    static const menu_items_t items[] = {
        {"XBE Launcher",               dash_open_xbe_launcher,   NULL, NULL},
        {"EEPROM Config",              dash_open_eeprom_config,  NULL, NULL},
        {"Clear Recent Titles",        dash_clear_recent,        NULL, "Accept \"Clear Recent Titles\""},
        {"Flush Cache Partitions",     dash_flush_cache,         NULL, "Accept \"Flush Cache Partitions\""},
        {"Mark Database Reset at Reboot", dash_rebuild_database, NULL, "Accept \"Database Reset\""},
    };
    menu_open_static(items, DASH_ARRAY_SIZE(items));
}

bool dash_mainmenu_is_open(void)
{
    return start_menu_open;
}

/* ── Snapshot for remote status ── */
static int mainmenu_snapshot(char *buf, int size)
{
    if (!start_menu_open) return 0;

    int pos = 0;
    pos += snprintf(buf + pos, size - pos, "[menu]\n");
    pos += snprintf(buf + pos, size - pos, "title=\"Main Menu\"\n");
    pos += snprintf(buf + pos, size - pos, "items=");
    for (int i = 0; i < (int)MENU_ITEM_COUNT && pos < size - 1; i++) {
        pos += snprintf(buf + pos, size - pos, "%s\"%s\"",
                        i > 0 ? ", " : "", menu_items[i].label);
        if (i == menu_selected)
            pos += snprintf(buf + pos, size - pos, " [SELECTED]");
    }
    pos += snprintf(buf + pos, size - pos, "\n");
    return pos;
}

void dash_mainmenu_snapshot_register(void)
{
    dash_snapshot_register(mainmenu_snapshot);
}
