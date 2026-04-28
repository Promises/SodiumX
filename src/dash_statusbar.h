// SPDX-License-Identifier: MIT

#ifndef _DASH_STATUSBAR_H
#define _DASH_STATUSBAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sodiumx.h"

/* Create the status bar on screen (top 44px). Returns the container. */
lv_obj_t *dash_statusbar_create(lv_obj_t *parent);

/* Refresh chips based on current settings visibility flags */
void dash_statusbar_refresh(void);

#ifdef __cplusplus
}
#endif

#endif
