// SPDX-License-Identifier: MIT
// Remote debug/test interface for LithiumX
// Line-based TCP server for input injection, screenshots, log streaming.
// Works on SDL2 (desktop) and nxdk (Xbox) builds.
//
// Protocol (text lines, \n terminated):
//   → key <name>         Inject keypress (right, left, up, down, enter, esc, s, y, b, pageup, pagedown, q, e)
//   → screenshot         Save BMP and send binary: "OK <size>\n<raw bmp bytes>"
//   → log on             Start streaming logs to this client
//   → log off            Stop streaming logs
//   → status             Query state: "OK tab=N page=NAME sel=N/N\n"
//   → quit               Shutdown the app
//   ← OK ...             Success response
//   ← ERR ...            Error response
//   ← [LOG] ...          Log line (when streaming enabled)

#ifndef _DASH_REMOTE_H
#define _DASH_REMOTE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DASH_REMOTE_PORT
#define DASH_REMOTE_PORT 9876
#endif

#define DASH_REMOTE_MAX_CLIENTS 4

void dash_remote_init(void);
void dash_remote_deinit(void);
void dash_remote_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
