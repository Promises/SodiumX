// SPDX-License-Identifier: MIT
// Backdrop blur: fanart displayed behind the rail, GPU-scaled

#ifndef _DASH_BACKDROP_H
#define _DASH_BACKDROP_H

#ifdef __cplusplus
extern "C" {
#endif

struct _lv_obj_t;

/* Create the backdrop layer (call once during dash_create, before other layers) */
void dash_backdrop_create(struct _lv_obj_t *parent);

/* Update the backdrop with fanart from the focused tile's directory.
 * Pass the thumbnail path (default.tbn) — the function looks for fanart.jpg
 * in the same directory and falls back to the poster if not found.
 * Pass NULL to show only the color wash. */
void dash_backdrop_update(const char *thumb_path);

#ifdef __cplusplus
}
#endif

#endif
