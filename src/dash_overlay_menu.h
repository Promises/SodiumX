// SPDX-License-Identifier: MIT
// Reusable overlay menu — animated card with icon rows, selection highlight,
// and automatic snapshot for remote status.
//
// Both the Start menu and context menu use this widget. Any future overlay
// menu (e.g. sort picker, page selector) can reuse it too.

#ifndef _DASH_OVERLAY_MENU_H
#define _DASH_OVERLAY_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#define OVERLAY_MENU_MAX_ITEMS 12

typedef struct {
    const char *label;
    const char *icon;           /* LV_SYMBOL_* */
    void (*cb)(void *param);
    void *cb_param;
    const char *confirm_text;   /* if non-NULL, wrap in confirmbox */
} overlay_menu_item_t;

typedef struct {
    const char *eyebrow;        /* small accent text above title */
    const char *title;          /* menu title */
    const char *close_hint;     /* e.g. "START > close" or "B > close" */
    int close_key;              /* extra key that closes (e.g. DASH_SETTINGS_PAGE) */
    const overlay_menu_item_t *items;
    int item_count;
} overlay_menu_config_t;

/* Open an overlay menu. Only one can be open at a time.
 * Calling while already open closes the current one first. */
void dash_overlay_menu_open(const overlay_menu_config_t *config);

/* Close the currently open overlay menu. */
void dash_overlay_menu_close(void);

/* Query state. */
bool dash_overlay_menu_is_open(void);

/* Register the snapshot callback (call once at init). */
void dash_overlay_menu_snapshot_register(void);

#ifdef __cplusplus
}
#endif

#endif
