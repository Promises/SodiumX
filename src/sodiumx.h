// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2022 Ryzee119

#ifndef _DASH_H
#define _DASH_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WIN32
#include <windows.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <lvgl.h>
#ifdef NXDK
#include <SDL.h>
#include <nxdk/mount.h>
int strcasecmp(const char *s1, const char *s2);
#else
#include <SDL2/SDL.h>
#endif

#include "lx_fonts.h"
#include "dash_database.h"
#include "dash_eeprom.h"
#include "dash_mainmenu.h"
#include "dash_scroller.h"
#include "dash_settings.h"
#include "dash_styles.h"
#include "dash_synop.h"
#include "dash_browser.h"
#include "dash_launcher.h"
#include "dash_debug.h"
#include "dash_anim.h"
#include "dash_statusbar.h"
#include "dash_controls_bar.h"
#include "dash_sysinfo.h"
#include "dash_backdrop.h"
#include "dash_remote.h"
#include "dash_backup.h"
#include "dash_panel.h"
#include "dash_context_menu.h"
#include "dash_overlay_menu.h"
#include "dash_keyboard.h"
#include "dash_perf.h"

#include "lvgl_drivers/lv_port_disp.h"
#include "lvgl_drivers/lv_port_indev.h"
#include "lvgl_widgets/menu.h"
#include "lvgl_widgets/generic_container.h"
#include "lvgl_widgets/confirmbox.h"
#include "lvgl_widgets/helpers.h"

#include "libs/toml/toml.h"
#include "libs/sqlite3/sqlite3.h"
#include "libs/jpg_decoder/jpg_decoder.h"
#include "libs/sxml/sxml.h"
#include "libs/toml/toml.h"
#include "libs/tlsf/tlsf.h"
#include "platform/platform.h"

/// Macro that returns the size of a static array
#define DASH_ARRAY_SIZE(_arr) (sizeof(_arr) / sizeof((_arr)[0]))

#ifdef WIN32
#define DASH_PATH_SEPARATOR '\\'
#else
#define DASH_PATH_SEPARATOR '/'
#endif

//All lvgl directories are prefixed with Q:
//nxdk local directory is also mounting to Q: so we get Q:Q:..
#ifndef DASH_SEARCH_PATH_CONFIG
#ifdef NXDK
#define DASH_SEARCH_PATH_CONFIG "E:\\UDATA\\SodiumX\\sodiumx.toml"
#else
#define DASH_SEARCH_PATH_CONFIG "sodiumx.toml"
#endif
#endif

#ifndef DASH_DATABASE_PATH
#ifdef NXDK
#define DASH_DATABASE_PATH "E:\\UDATA\\SodiumX\\sodiumx.db"
#else
#define DASH_DATABASE_PATH "sodiumx.db"
#endif
#endif

#ifndef DASH_ROOT_PATH
#ifdef NXDK
#define DASH_ROOT_PATH ""
#else
#define DASH_ROOT_PATH "."
#endif
#endif

#ifndef DASH_LAUNCH_EXE
#define DASH_LAUNCH_EXE "default.xbe"
#endif

#ifndef DASH_MAX_PAGES
#define DASH_MAX_PAGES 8
#endif

#ifndef DASH_MAX_PATHS_PER_PAGE
#define DASH_MAX_PATHS_PER_PAGE 16
#endif

#ifndef DASH_MAX_GAMES
#define DASH_MAX_GAMES 1024 //Per page
#endif

#ifndef DASH_MAX_PATHLEN
#define DASH_MAX_PATHLEN 256 //Per page
#endif

#ifndef DASH_THUMBNAIL_WIDTH
#define DASH_THUMBNAIL_WIDTH ((lv_obj_get_width(lv_scr_act()) - (2 * DASH_XMARGIN)) / dash_settings.items_per_row)
#endif

#ifndef DASH_THUMBNAIL_HEIGHT
#define DASH_THUMBNAIL_HEIGHT (DASH_THUMBNAIL_WIDTH * 1.4)
#endif

#ifndef DASH_DEFAULT_THUMBNAIL
#define DASH_DEFAULT_THUMBNAIL "default_tbn.jpg" //Root directory if not found in game directory
#endif

#ifndef DASH_GAME_THUMBNAIL
#define DASH_GAME_THUMBNAIL "default.tbn"
#endif

#ifndef DASH_MAX_PATH
#ifndef MAX_PATH
#define MAX_PATH 255
#endif
#define DASH_MAX_PATH MAX_PATH
#endif

/* Controller button → LVGL key codes.
 * Each physical button gets a unique code so overlays can distinguish them. */
#define DASH_KEY_A        LV_KEY_ENTER   /* A button */
#define DASH_KEY_B        LV_KEY_ESC     /* B button */
#define DASH_KEY_X        'x'            /* X button */
#define DASH_KEY_Y        'i'            /* Y button */
#define DASH_KEY_START    's'            /* START button */
#define DASH_KEY_BACK     'k'            /* BACK button */
#define DASH_KEY_WHITE    '<'            /* White (left shoulder) */
#define DASH_KEY_BLACK    '>'            /* Black (right shoulder) */
#define DASH_KEY_LT       'L'            /* Left trigger */
#define DASH_KEY_RT       'R'            /* Right trigger */

/* Semantic aliases used by existing code */
#define DASH_NEXT_PAGE     DASH_KEY_BLACK
#define DASH_PREV_PAGE     DASH_KEY_WHITE
#define DASH_SETTINGS_PAGE DASH_KEY_START
#define DASH_INFO_PAGE     DASH_KEY_Y
#define DASH_CONTEXT_PAGE  DASH_KEY_X
#define DASH_PREV_TAB 't'
#define DASH_NEXT_TAB 'T'

/* Tile dimensions for horizontal rail (720p / 480p) */
#define DASH_TILE_W   ((lv_obj_get_width(lv_scr_act()) <= 640) ? 153 : 230)
#define DASH_TILE_H   ((lv_obj_get_width(lv_scr_act()) <= 640) ? 213 : 320)
#define DASH_TILE_GAP ((lv_obj_get_width(lv_scr_act()) <= 640) ? 17 : 26)

// There is one 'parser' per 'tile'. The parser asynchronously parses all the path set my the xml and adds
// eatch item. Each parser contains a image scrolling container 'scroller' to show all the game art etc.
// Each 'tile' is a child of a tileview object 'pagetiles'. These are swiped left and right to change page.
typedef struct
{
    char page_title[32];
    void *db_scan_thread;
    lv_obj_t *tile;     // The tile in the tileview parent 'pagetiles'
    lv_obj_t *scroller; // The scroller contains image containers for each item
} parse_handle_t;

typedef struct
{
    char *thumb_path;
    lv_obj_t *canvas;
    void *decomp_handle;
    void *mem; //Memory is the allocated block from malloc
    void *image; //image is the decompressed image with mem (this may be byte aligned etc)
    int w;
    int h;
    bool prevent_abort;
} jpg_info_t;

typedef struct
{
    int db_id;
    int rail_index;  /* 1-based index in the rail (for O(1) lookup) */
    char title[MAX_META_LEN];
    char meta_subtitle[128]; /* cached "Developer · Year · Genre" — filled on first focus */
    bool meta_cached;
    jpg_info_t *jpg_info;
    lv_obj_t *shadow_img;
} title_t;

#ifndef NANO_DEBUG_LEVEL
#define NANO_DEBUG_LEVEL LEVEL_TRACE
#endif

typedef enum{

    LEVEL_TRACE,
    LEVEL_WARN,
    LEVEL_ERROR,
    LEVEL_NONE
} dash_debug_level_t;

void dash_printf(dash_debug_level_t level, const char *format, ...);

void dash_init(void);
void dash_create(void);
void dash_deinit(void);
void dash_set_tab(int tab_index);
int dash_get_tab(void);
void dash_tab_bar_enter_nav(int current_page);
void dash_tab_bar_exit_nav(bool cancel);
bool dash_tab_nav_is_active(void);
void dash_update_meta(const char *title, const char *subtitle);
void dash_update_hero_counter(int selected, int total);
void lvgl_getlock(void);
void lvgl_removelock(void);
void *lx_mem_alloc(size_t size);
void *lx_mem_realloc(void *data, size_t new_size);
void lx_mem_free(void *data);
void lx_mem_usage(uint32_t *used, uint32_t *capacity);

/* Network / FTP platform functions */
typedef struct {
    char ip[16];
    char mask[16];
    char gateway[16];
    char dns1[16];
    char dns2[16];
    bool dhcp_active;
    bool link_up;
    int  link_speed_mbps;
} dash_net_info_t;

void dash_network_apply(void);
void dash_network_get_info(dash_net_info_t *info);
void dash_ftp_start(void);
void dash_ftp_stop(void);

void dash_focus_set_final(lv_obj_t *focus);
void dash_focus_change_depth(lv_obj_t *new_focus);
lv_obj_t *dash_focus_pop_depth();
void dash_focus_change(lv_obj_t *new_obj);

extern toml_table_t *dash_search_paths;
extern dash_settings_t dash_settings;
extern char dash_launch_path[];

#ifdef __cplusplus
}
#endif

#endif