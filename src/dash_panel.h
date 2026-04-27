// SPDX-License-Identifier: MIT
// Reusable two-pane panel (left nav + right content body).
//
// Focus model:
//   - Opens with left nav focused. UP/DOWN navigates sections.
//   - RIGHT or A switches focus to right content pane.
//   - In right pane: B or LEFT returns to left nav (unless content
//     consumes LEFT via its key handler).
//   - Left nav shows "focused" style when it has input, "active" style
//     when right pane has focus.
//   - ESC closes the panel.

#ifndef _DASH_PANEL_H
#define _DASH_PANEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define DASH_PANEL_MAX_SECTIONS 12

typedef struct {
    const char *label;
    const char *icon;
    void (*build)(lv_obj_t *body);
    /* Called when right pane has focus. Return true if key was consumed.
     * If NULL or returns false, panel handles the key (LEFT/B → back to nav). */
    bool (*on_key)(lv_key_t key);
    /* If true, right pane cannot be focused (view-only, no interactive content). */
    bool viewonly;
} dash_panel_section_t;

typedef struct {
    const char *nav_title;       /* eyebrow text in left nav */
    const char *nav_subtitle;    /* smaller text under eyebrow (e.g. game name) */
    const dash_panel_section_t *sections;
    int section_count;
    /* Called when panel closes (optional) */
    void (*on_close)(void);
} dash_panel_config_t;

/* Open a two-pane panel. Returns the panel object. */
void dash_panel_open(const dash_panel_config_t *config);

/* Close the currently open panel. */
void dash_panel_close(void);

/* Get the currently active section index. */
int dash_panel_get_section(void);

/* Force rebuild the right body (e.g. after async data arrives). */
void dash_panel_rebuild_body(void);

/* Check if a panel is currently open. */
bool dash_panel_is_open(void);

/* Check if the left nav has focus (vs right content). */
bool dash_panel_is_nav_focused(void);

#ifdef __cplusplus
}
#endif

#endif
