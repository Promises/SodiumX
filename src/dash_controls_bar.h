// SPDX-License-Identifier: MIT

#ifndef _DASH_CONTROLS_BAR_H
#define _DASH_CONTROLS_BAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lithiumx.h"

/* Create the controls bar (bottom 52px). Returns the container. */
lv_obj_t *dash_controls_bar_create(lv_obj_t *parent);

/* Update context-aware button labels */
void dash_controls_bar_set_context(const char *a_label, const char *b_label,
                                   const char *x_label, const char *y_label);

#ifdef __cplusplus
}
#endif

#endif
