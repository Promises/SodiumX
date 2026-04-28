// SPDX-License-Identifier: MIT
// Daisywheel keyboard — gamepad-driven on-screen text input.
// Full mode: 8 petals with analog stick + face buttons.
// Numeric mode: horizontal digit strip with D-pad.

#ifndef _DASH_KEYBOARD_H
#define _DASH_KEYBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#define DASH_KB_MODE_FULL    0   /* Full daisywheel (letters, numbers, symbols) */
#define DASH_KB_MODE_NUMERIC 1   /* Numeric strip (0-9, . : / -)               */

/* Call once at app startup (after LVGL init). */
void dash_keyboard_init(void);

/* Open the keyboard overlay.
 * buf/buf_size: caller's text buffer (edited in-place).
 * mode: DASH_KB_MODE_FULL or DASH_KB_MODE_NUMERIC.
 * on_done: called when user confirms (START). NULL if not needed.
 *          NOT called on cancel (BACK) — buffer is restored. */
void dash_keyboard_open(char *buf, int buf_size, int mode, void (*on_done)(void));

/* Close the keyboard programmatically. */
void dash_keyboard_close(bool confirm);

/* Query state. */
bool dash_keyboard_is_open(void);

#ifdef __cplusplus
}
#endif

#endif
