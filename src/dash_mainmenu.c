// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2022 Ryzee119

#include "sodiumx.h"
#include "dash_anim.h"
#include "dash_overlay_menu.h"

/* ============================================================
 *  Menu item callbacks (forward declarations)
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
 *  Menu items — drives both the UI and the snapshot
 * ============================================================ */
static const overlay_menu_item_t menu_items[] = {
    {"System Information",  LV_SYMBOL_SETTINGS,  dash_system_info,    NULL, NULL},
    {"Rescan Library",      LV_SYMBOL_REFRESH,   dash_rescan_library, NULL, NULL},
    {"Launch MS Dashboard", LV_SYMBOL_RIGHT,     dash_launch_msdash,  NULL, "Accept \"Launch MS Dashboard\""},
    {"Launch DVD",          LV_SYMBOL_AUDIO,     dash_launch_dvd,     NULL, "Accept \"Launch DVD\""},
    {"Utilities",           LV_SYMBOL_LIST,      dash_utilities,      NULL, NULL},
    {"Settings",            LV_SYMBOL_SETTINGS,  dash_settings_new,   NULL, NULL},
//     {"Settings (Legacy)",            LV_SYMBOL_SETTINGS,  dash_settings_page,   NULL, NULL},
    {"About SodiumX",       LV_SYMBOL_EYE_OPEN,  dash_open_about,     NULL, NULL},
    {"Reboot",              LV_SYMBOL_REFRESH,   dash_reboot,         NULL, "Accept \"Reboot\""},
    {"Shutdown",            LV_SYMBOL_POWER,     dash_shutdown,       NULL, "Accept \"Shutdown\""},
};
#define MENU_ITEM_COUNT (sizeof(menu_items) / sizeof(menu_items[0]))

/* ============================================================
 *  Open / close the Start menu
 * ============================================================ */
void dash_mainmenu_open(void)
{
    if (dash_overlay_menu_is_open()) {
        dash_overlay_menu_close();
        return;
    }

    static const overlay_menu_config_t config = {
        .eyebrow    = "SODIUMX",
        .title      = "Main Menu",
        .close_hint = "START " LV_SYMBOL_RIGHT " close",
        .close_key  = DASH_SETTINGS_PAGE,
        .items      = menu_items,
        .item_count = sizeof(menu_items) / sizeof(menu_items[0]),
    };
    dash_overlay_menu_open(&config);
}

bool dash_mainmenu_is_open(void)
{
    return dash_overlay_menu_is_open();
}

/* ============================================================
 *  Callback implementations
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
