// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2022 Ryzee119

#ifndef _DASH_SETTINGS_H
#define _DASH_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sodiumx.h"

#define DASH_SETTINGS_VERSION_V2 0x02
#define DASH_SETTINGS_VERSION_V3 0x03
#define DASH_SETTINGS_VERSION    0x04
#define DASH_SETTINGS_MAGIC      (0xBEEF0000 + DASH_SETTINGS_VERSION)
#define DASH_SETTINGS_MAGIC_V2   (0xBEEF0000 + DASH_SETTINGS_VERSION_V2)
#define DASH_SETTINGS_MAGIC_V3   (0xBEEF0000 + DASH_SETTINGS_VERSION_V3)

/* Legacy v2 struct for migration */
typedef struct dash_settings_v2 {
    unsigned int magic;
    bool use_fahrenheit;
    bool auto_launch_dvd;
    bool show_debug_info;
    int startup_page_index;
    int theme_colour;
    int max_recent_items;
    int items_per_row;
    char earliest_recent_date[20];
    char sort_strings[4096];
} dash_settings_v2_t;

/* Legacy v3 struct for migration */
typedef struct dash_settings_v3 {
    unsigned int magic;
    bool use_fahrenheit;
    bool auto_launch_dvd;
    bool show_debug_info;
    int startup_page_index;
    int theme_colour;
    int max_recent_items;
    int items_per_row;
    char earliest_recent_date[20];
    char sort_strings[4096];
    uint8_t accent_index;
    bool show_fps_overlay;
    bool show_controller_hints;
    bool show_clock_chip;
    bool show_network_chip;
    bool show_temp_chip;
    bool animated_background;
    bool backdrop_blur;
    bool tile_parallax;
    bool film_grain;
    uint8_t resolution_mode;
    uint8_t audio_output;
    bool ui_sounds;
    bool ftp_enabled;
    uint8_t _padding[16];
} dash_settings_v3_t;

/* Current v4 struct */
typedef struct dash_settings {
    unsigned int magic;
    bool use_fahrenheit;
    bool auto_launch_dvd;
    bool show_debug_info;
    int startup_page_index;
    int theme_colour;
    int max_recent_items;
    int items_per_row;
    char earliest_recent_date[20];
    char sort_strings[4096];
    /* ── v3 fields ── */
    uint8_t accent_index;          /* ACCENT_GREEN..ACCENT_PURPLE */
    bool show_fps_overlay;
    bool show_controller_hints;
    bool show_clock_chip;
    bool show_network_chip;
    bool show_temp_chip;
    bool animated_background;
    bool backdrop_blur;
    bool tile_parallax;
    bool film_grain;
    uint8_t resolution_mode;       /* 0=480p, 1=720p, 2=auto */
    uint8_t audio_output;          /* 0=stereo, 1=mono, 2=5.1 */
    bool ui_sounds;
    bool ftp_enabled;
    /* ── v4 fields: save game backup ── */
    char backup_server[64];        /* IP or hostname of backup server */
    uint16_t backup_port;          /* default 9877 */
    bool backup_on_start;          /* auto-backup on dashboard boot */
    bool backup_before_launch;     /* backup before launching a game */
    uint8_t _padding[16];          /* future expansion */
} dash_settings_t;

void dash_settings_open(void);
void dash_settings_apply(bool confirm_box);
void dash_settings_read(void);

#ifdef __cplusplus
}
#endif

#endif
