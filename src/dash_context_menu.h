// SPDX-License-Identifier: MIT
// Game context menu — opened with X button on a selected tile.
// Provides per-game actions like save backup management.

#ifndef _DASH_CONTEXT_MENU_H
#define _DASH_CONTEXT_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

/* Open the context menu for a game with the given database ID.
 * Queries the DB for title_id, title name, etc. */
void dash_context_menu_open(int db_id);

#ifdef __cplusplus
}
#endif

#endif
