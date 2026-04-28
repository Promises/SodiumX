// Custom fonts for SodiumX
// UI font: Rubik or Space Grotesk (selected by DASH_FONT_SPACE_GROTESK)
// Mono font: JetBrains Mono (always)
#ifndef LX_FONTS_H
#define LX_FONTS_H

#include "lvgl.h"

/* ── Rubik (default UI font) ── */
LV_FONT_DECLARE(lv_font_rubik_10)
LV_FONT_DECLARE(lv_font_rubik_12)
LV_FONT_DECLARE(lv_font_rubik_14)
LV_FONT_DECLARE(lv_font_rubik_16)
LV_FONT_DECLARE(lv_font_rubik_20)
LV_FONT_DECLARE(lv_font_rubik_22)
LV_FONT_DECLARE(lv_font_rubik_24)
LV_FONT_DECLARE(lv_font_rubik_26)
LV_FONT_DECLARE(lv_font_rubik_32)

/* ── Space Grotesk (alternate UI font) ── */
LV_FONT_DECLARE(lv_font_sg_10)
LV_FONT_DECLARE(lv_font_sg_12)
LV_FONT_DECLARE(lv_font_sg_14)
LV_FONT_DECLARE(lv_font_sg_16)
LV_FONT_DECLARE(lv_font_sg_20)
LV_FONT_DECLARE(lv_font_sg_22)
LV_FONT_DECLARE(lv_font_sg_24)
LV_FONT_DECLARE(lv_font_sg_26)
LV_FONT_DECLARE(lv_font_sg_32)

/* ── JetBrains Mono (mono font, always used) ── */
LV_FONT_DECLARE(lv_font_jetbrains_mono_10)
LV_FONT_DECLARE(lv_font_jetbrains_mono_12)
LV_FONT_DECLARE(lv_font_jetbrains_mono_14)
LV_FONT_DECLARE(lv_font_jetbrains_mono_16)

/* ── UI font aliases — set DASH_FONT_SPACE_GROTESK=1 to use Space Grotesk ── */
#ifdef DASH_FONT_SPACE_GROTESK
#define dash_font_ui_10 lv_font_sg_10
#define dash_font_ui_12 lv_font_sg_12
#define dash_font_ui_14 lv_font_sg_14
#define dash_font_ui_16 lv_font_sg_16
#define dash_font_ui_20 lv_font_sg_20
#define dash_font_ui_22 lv_font_sg_22
#define dash_font_ui_24 lv_font_sg_24
#define dash_font_ui_26 lv_font_sg_26
#define dash_font_ui_32 lv_font_sg_32
#else
#define dash_font_ui_10 lv_font_rubik_10
#define dash_font_ui_12 lv_font_rubik_12
#define dash_font_ui_14 lv_font_rubik_14
#define dash_font_ui_16 lv_font_rubik_16
#define dash_font_ui_20 lv_font_rubik_20
#define dash_font_ui_22 lv_font_rubik_22
#define dash_font_ui_24 lv_font_rubik_24
#define dash_font_ui_26 lv_font_rubik_26
#define dash_font_ui_32 lv_font_rubik_32
#endif

#endif
