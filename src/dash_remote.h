// SPDX-License-Identifier: MIT
// Remote debug/test interface for SodiumX
// Line-based TCP server for input injection, screenshots, log streaming.
// Works on SDL2 (desktop) and nxdk (Xbox) builds.
//
// Protocol (text lines, \n terminated):
//   → key <name>         Inject keypress (right, left, up, down, enter, esc, s, y, b, pageup, pagedown, q, e)
//   → screenshot         Save BMP and send binary: "OK <size>\n<raw bmp bytes>"
//   → log on             Start streaming logs to this client
//   → log off            Stop streaming logs
//   → status             Query structured UI snapshot (see dash_snapshot below)
//   → quit               Shutdown the app
//   ← OK ...             Success response
//   ← ERR ...            Error response
//   ← [LOG] ...          Log line (when streaming enabled)

#ifndef _DASH_REMOTE_H
#define _DASH_REMOTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#ifndef DASH_REMOTE_PORT
#define DASH_REMOTE_PORT 9876
#endif

#ifdef NXDK
#define DASH_DEBUG_WAIT_FLAG "E:\\UDATA\\SodiumX\\debug_wait"
#define DASH_REBUILD_DB_FLAG "E:\\UDATA\\SodiumX\\rebuild_db"
#else
#define DASH_DEBUG_WAIT_FLAG "/tmp/sodiumx_debug_wait"
#define DASH_REBUILD_DB_FLAG "/tmp/sodiumx_rebuild_db"
#endif

#define DASH_REMOTE_MAX_CLIENTS 4

void dash_remote_init(void);
void dash_remote_init_early(void); /* Retries bind until network ready — for debug flag */
void dash_remote_deinit(void);
void dash_remote_log(const char *fmt, ...);
bool dash_remote_has_log_client(void);

/* ── UI Snapshot system ──
 * Each UI module (menu, settings, scroller, etc.) registers a snapshot
 * callback. When the "status" command is received, all registered callbacks
 * are invoked to build a structured text description of the current screen.
 *
 * Callback should append to buf (respecting remaining size) and return the
 * number of chars written. Only called when the module's UI is active —
 * the callback itself decides whether to output anything.
 */
typedef int (*dash_snapshot_fn)(char *buf, int size);

#define DASH_SNAPSHOT_MAX 8
void dash_snapshot_register(dash_snapshot_fn fn);

#ifdef __cplusplus
}
#endif

#endif
