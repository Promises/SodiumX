#!/usr/bin/env python3
"""Generate pre-rendered pill/roundrect textures as C arrays.

Usage: python3 tools/gen_pills.py > src/dash_pill_data.h

The generated header contains static const uint8_t arrays in BGRA format
that can be used directly as lv_img_dsc_t data on both Xbox and desktop.
"""
import math, sys

def bgra(r, g, b, a):
    return bytes([b, g, r, a])

SSAA = 4  # 4x4 supersampling for smooth AA

def render_pixel(dist, r_inner, r_outer, bg, bg_a, bc, bc_a):
    """Render a single pixel based on distance from circle center.
    Only anti-aliases the outer edge. Inner fill-to-border transition is hard."""
    def clamp(v, lo, hi):
        return max(lo, min(hi, v))

    if dist <= r_inner:
        return bgra(bg[0], bg[1], bg[2], bg_a)
    elif dist <= r_outer - 0.5:
        return bgra(bc[0], bc[1], bc[2], bc_a)
    elif dist <= r_outer + 0.5:
        cov = clamp(r_outer + 0.5 - dist, 0, 1)
        a = int(cov * bc_a)
        return bgra(bc[0], bc[1], bc[2], a)
    return bgra(0, 0, 0, 0)

def sample_pixel_ssaa(px, py, cx, cy, r_outer, r_inner, bg, bg_a, bc, bc_a):
    """4x4 supersampled pixel — averages 16 subpixels for smooth corners."""
    acc_r, acc_g, acc_b, acc_a = 0, 0, 0, 0
    n = SSAA * SSAA
    for sy in range(SSAA):
        for sx in range(SSAA):
            spx = px + (sx + 0.5) / SSAA
            spy = py + (sy + 0.5) / SSAA
            dx = spx - cx
            dy = spy - cy
            dist = math.sqrt(dx * dx + dy * dy)
            if dist <= r_inner:
                acc_r += bg[0]; acc_g += bg[1]; acc_b += bg[2]; acc_a += bg_a
            elif dist <= r_outer:
                acc_r += bc[0]; acc_g += bc[1]; acc_b += bc[2]; acc_a += bc_a
            else:
                pass  # transparent
    if acc_a == 0:
        return bgra(0, 0, 0, 0)
    return bgra(acc_r // n, acc_g // n, acc_b // n, acc_a // n)

def render_roundrect(w, h, radius, border_w, bg, bg_a, bc, bc_a):
    """Render a full rounded rectangle as BGRA pixel data."""
    pixels = bytearray(w * h * 4)

    cy_top = radius - 0.5
    cy_bot = (h - 1) - radius + 0.5
    cx_left = radius - 0.5
    cx_right = (w - 1) - radius + 0.5
    r_outer = float(radius)
    r_inner = r_outer - border_w

    for y in range(h):
        for x in range(w):
            cx_corner = cy_corner = 0.0
            in_corner = False

            if x < radius and y < radius:
                cx_corner, cy_corner, in_corner = cx_left, cy_top, True
            elif x >= w - radius and y < radius:
                cx_corner, cy_corner, in_corner = cx_right, cy_top, True
            elif x < radius and y >= h - radius:
                cx_corner, cy_corner, in_corner = cx_left, cy_bot, True
            elif x >= w - radius and y >= h - radius:
                cx_corner, cy_corner, in_corner = cx_right, cy_bot, True

            if in_corner:
                dx = x - cx_corner
                dy = y - cy_corner
                dist = math.sqrt(dx * dx + dy * dy)
                px = render_pixel(dist, r_inner, r_outer, bg, bg_a, bc, bc_a)
            else:
                in_border = (y < border_w or y >= h - border_w or
                             x < border_w or x >= w - border_w)
                if in_border:
                    px = bgra(bc[0], bc[1], bc[2], bc_a)
                else:
                    px = bgra(bg[0], bg[1], bg[2], bg_a)

            offset = (y * w + x) * 4
            pixels[offset:offset+4] = px

    return bytes(pixels)

def lerp_color(c1, c2, t):
    """Linearly interpolate between two RGB tuples."""
    return (
        int(c1[0] + (c2[0] - c1[0]) * t),
        int(c1[1] + (c2[1] - c1[1]) * t),
        int(c1[2] + (c2[2] - c1[2]) * t),
    )

def blend_over(base_r, base_g, base_b, base_a, over_r, over_g, over_b, over_a):
    """Alpha-composite 'over' on top of 'base'. Returns (r, g, b, a) ints."""
    oa = over_a / 255.0
    ba = base_a / 255.0
    out_a = oa + ba * (1 - oa)
    if out_a < 0.001:
        return (0, 0, 0, 0)
    out_r = int((over_r * oa + base_r * ba * (1 - oa)) / out_a)
    out_g = int((over_g * oa + base_g * ba * (1 - oa)) / out_a)
    out_b = int((over_b * oa + base_b * ba * (1 - oa)) / out_a)
    return (out_r, out_g, out_b, int(out_a * 255))

def render_roundrect_gradient(w, h, radius, border_w, bg_top, bg_bot, bg_a, bc, bc_a,
                               overlay_top_rgba=None, overlay_bot_rgba=None):
    """Render a rounded rectangle with vertical gradient fill, border,
    and optional inner overlay gradient (for subtle highlight/shadow)."""
    pixels = bytearray(w * h * 4)

    cy_top = radius - 0.5
    cy_bot = (h - 1) - radius + 0.5
    cx_left = radius - 0.5
    cx_right = (w - 1) - radius + 0.5
    r_outer = float(radius)
    r_inner = r_outer - border_w

    for y in range(h):
        t = y / max(h - 1, 1)
        fill = lerp_color(bg_top, bg_bot, t)

        # Compute overlay color for this row
        if overlay_top_rgba and overlay_bot_rgba:
            ot = lerp_color(overlay_top_rgba[:3], overlay_bot_rgba[:3], t)
            oa = int(overlay_top_rgba[3] + (overlay_bot_rgba[3] - overlay_top_rgba[3]) * t)
            fr, fg_, fb, fa = blend_over(fill[0], fill[1], fill[2], bg_a, ot[0], ot[1], ot[2], oa)
        else:
            fr, fg_, fb, fa = fill[0], fill[1], fill[2], bg_a

        for x in range(w):
            cx_corner = cy_corner = 0.0
            in_corner = False

            if x < radius and y < radius:
                cx_corner, cy_corner, in_corner = cx_left, cy_top, True
            elif x >= w - radius and y < radius:
                cx_corner, cy_corner, in_corner = cx_right, cy_top, True
            elif x < radius and y >= h - radius:
                cx_corner, cy_corner, in_corner = cx_left, cy_bot, True
            elif x >= w - radius and y >= h - radius:
                cx_corner, cy_corner, in_corner = cx_right, cy_bot, True

            if in_corner:
                dx = x - cx_corner
                dy = y - cy_corner
                dist = math.sqrt(dx * dx + dy * dy)
                px = render_pixel(dist, r_inner, r_outer, (fr, fg_, fb), fa, bc, bc_a)
            else:
                in_border = (y < border_w or y >= h - border_w or
                             x < border_w or x >= w - border_w)
                if in_border:
                    px = bgra(bc[0], bc[1], bc[2], bc_a)
                else:
                    px = bgra(fr, fg_, fb, fa)

            offset = (y * w + x) * 4
            pixels[offset:offset + 4] = px

    return bytes(pixels)

def render_circle_gradient(diameter, top_color, bot_color, opa,
                            shadow_offset=0, shadow_blur=0, shadow_alpha=0,
                            inset_top_alpha=0, inset_bot_alpha=0):
    """Render a circle with vertical gradient, optional inset highlights, and drop shadow."""
    # Expand canvas for shadow
    pad = shadow_blur + abs(shadow_offset)
    canvas = diameter + pad * 2
    pixels = bytearray(canvas * canvas * 4)
    center = (canvas - 1) / 2.0
    r = diameter / 2.0

    for y in range(canvas):
        for x in range(canvas):
            # Shadow pass
            sdx = x - center
            sdy = y - center - shadow_offset
            sdist = math.sqrt(sdx * sdx + sdy * sdy)
            s_a = 0
            if shadow_alpha > 0 and shadow_blur > 0:
                if sdist <= r + shadow_blur:
                    s_a = max(0, min(shadow_alpha, int(shadow_alpha * max(0, 1 - max(0, sdist - r) / shadow_blur))))

            # Main circle
            dx = x - center
            dy = y - center
            dist = math.sqrt(dx * dx + dy * dy)

            if dist <= r - 0.5:
                # Inside — gradient fill
                # Map y relative to circle top
                circle_top = center - r
                circle_h = diameter
                ct = max(0, min(1, (y - circle_top) / max(circle_h - 1, 1)))
                c = lerp_color(top_color, bot_color, ct)

                # Inset highlight: top = bright, bottom = dark
                inset_r, inset_g, inset_b = c
                if inset_top_alpha > 0 and ct < 0.15:
                    blend = (0.15 - ct) / 0.15
                    ia = int(inset_top_alpha * blend)
                    inset_r, inset_g, inset_b, _ = blend_over(c[0], c[1], c[2], 255, 255, 255, 255, ia)
                elif inset_bot_alpha > 0 and ct > 0.85:
                    blend = (ct - 0.85) / 0.15
                    ia = int(inset_bot_alpha * blend)
                    inset_r, inset_g, inset_b, _ = blend_over(c[0], c[1], c[2], 255, 0, 0, 0, ia)

                px = bgra(inset_r, inset_g, inset_b, opa)
            elif dist <= r + 0.5:
                # AA edge
                circle_top = center - r
                circle_h = diameter
                ct = max(0, min(1, (y - circle_top) / max(circle_h - 1, 1)))
                c = lerp_color(top_color, bot_color, ct)
                cov = max(0, min(1, r + 0.5 - dist))
                a = int(cov * opa)
                px = bgra(c[0], c[1], c[2], a)
            elif s_a > 0:
                # Shadow only
                px = bgra(0, 0, 0, s_a)
            else:
                px = bgra(0, 0, 0, 0)

            offset = (y * canvas + x) * 4
            pixels[offset:offset + 4] = px

    return bytes(pixels), canvas

def render_circle(diameter, color, opa):
    """Render a filled circle as BGRA pixel data."""
    pixels = bytearray(diameter * diameter * 4)
    center = (diameter - 1) / 2.0
    r = diameter / 2.0

    for y in range(diameter):
        for x in range(diameter):
            dx = x - center
            dy = y - center
            dist = math.sqrt(dx * dx + dy * dy)
            if dist <= r - 0.5:
                px = bgra(color[0], color[1], color[2], opa)
            elif dist <= r + 0.5:
                cov = max(0, min(1, r + 0.5 - dist))
                a = int(cov * opa)
                px = bgra(color[0], color[1], color[2], a)
            else:
                px = bgra(0, 0, 0, 0)
            offset = (y * diameter + x) * 4
            pixels[offset:offset+4] = px

    return bytes(pixels)

def emit_array(name, data, w, h):
    """Emit a C array + lv_img_dsc_t definition."""
    print(f"static const uint8_t {name}_data[] = {{")
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
        print(f"    {hex_vals},")
    print(f"}};")
    print(f"static const lv_img_dsc_t {name} = {{")
    print(f"    .header.always_zero = 0,")
    print(f"    .header.w = {w},")
    print(f"    .header.h = {h},")
    print(f"    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,")
    print(f"    .data_size = sizeof({name}_data),")
    print(f"    .data = {name}_data,")
    print(f"}};")
    print()

# ── Define all pills ──

# Colors (RGB tuples)
EF_FG       = (0xd3, 0xc6, 0xaa)
EF_FG_MUTED = (0x9d, 0xa9, 0xa0)
EF_GREEN    = (0xa7, 0xc0, 0x80)
EF_BG3      = (0x41, 0x4b, 0x50)

CHIP_BG     = (0x2e, 0x38, 0x3c)
META_BG     = (0x37, 0x41, 0x45)

print("/* Auto-generated by tools/gen_pills.py — do not edit */")
print("#pragma once")
print("#include <lvgl.h>")
print()

# ── Status bar chips (height 27, radius 13) ──
# Measured from lv_obj with status_chip_style: pad 10+10 lr, 6+6 tb, font JBM12 (line_height=15)
# height = 15 + 6 + 6 = 27
chip_h = 27
chip_r = chip_h // 2

# Generate chips at various widths for FPS, temp, network, clock
# We'll generate a few fixed-width pills that cover the needed sizes
chip_widths = {
    "chip_fps":    75,  # "60 FPS"
    "chip_cpu":    80,  # "100% CPU"
    "chip_mem":    90,  # "30/64MB"
    "chip_temp":   60,  # "48°"
    "chip_net":    55,  # "FTP"
    "chip_clock":  70,  # "00:00"
}

for name, w in chip_widths.items():
    data = render_roundrect(w, chip_h, chip_r, 1, CHIP_BG, 140, EF_FG, 20)
    emit_array(f"pill_{name}", data, w, chip_h)

# ── Meta pills (height ~25, radius 12) ──
# pad 12+12 lr, 6+6 tb, font rubik12 (line_height=15 approx)
# Actual height needs measuring, using 25 for now
meta_h = 25
meta_r = meta_h // 2

meta_widths = {
    "meta_launches": 120,  # "0 LAUNCHES"
    "meta_ready":     80,  # "✓ READY"
}

for name, w in meta_widths.items():
    data = render_roundrect(w, meta_h, meta_r, 1, META_BG, 179, EF_FG, 20)
    emit_array(f"pill_{name}", data, w, meta_h)

# ── Status dot (6×6 green circle) ──
dot_data = render_circle(6, EF_GREEN, 255)
emit_array("pill_status_dot", dot_data, 6, 6)

# ── Toggle — sized to match pill button height (27px) ──
toggle_w = 48
toggle_h = 27
toggle_r = toggle_h // 2
bw = 2

# Track colors — solid fills with SSAA for smooth corners
# OFF: bg2 fill, bg3 border
toggle_off_data = render_roundrect(toggle_w, toggle_h, toggle_r, bw,
    (0x37, 0x41, 0x45), 255, (0x41, 0x4b, 0x50), 255)
emit_array("pill_toggle_off", toggle_off_data, toggle_w, toggle_h)

# ON: mid-green fill, dark green border
toggle_on_data = render_roundrect(toggle_w, toggle_h, toggle_r, bw,
    (0x85, 0x9f, 0x62), 255, (0x5d, 0x7a, 0x44), 255)
emit_array("pill_toggle_on", toggle_on_data, toggle_w, toggle_h)

# Focused OFF: bg1 fill, green border
toggle_off_focus = render_roundrect(toggle_w, toggle_h, toggle_r, bw,
    (0x2e, 0x38, 0x3c), 255, (0xa7, 0xc0, 0x80), 255)
emit_array("pill_toggle_off_focus", toggle_off_focus, toggle_w, toggle_h)

# Focused ON: mid-green fill, lighter green border
toggle_on_focus = render_roundrect(toggle_w, toggle_h, toggle_r, bw,
    (0x85, 0x9f, 0x62), 255, (0xcf, 0xe3, 0xa8), 255)
emit_array("pill_toggle_on_focus", toggle_on_focus, toggle_w, toggle_h)

# ── Thumb: 19px circle, scaled proportionally ──
thumb_d = 19
thumb_shadow_blur = 4
thumb_shadow_offset = 1
thumb_pad = thumb_shadow_blur + abs(thumb_shadow_offset)  # = 5
thumb_canvas = thumb_d + thumb_pad * 2  # = 29

# Thumb OFF: gradient #cdd5c1 → #a6b09a
thumb_off_data, _ = render_circle_gradient(
    thumb_d,
    top_color=(0xcd, 0xd5, 0xc1), bot_color=(0xa6, 0xb0, 0x9a), opa=255,
    shadow_offset=thumb_shadow_offset, shadow_blur=thumb_shadow_blur, shadow_alpha=100,
    inset_top_alpha=40, inset_bot_alpha=45)
emit_array("pill_toggle_thumb_off", thumb_off_data, thumb_canvas, thumb_canvas)

# Thumb ON: gradient #f1ebd9 → #c8bfa3
thumb_on_data, _ = render_circle_gradient(
    thumb_d,
    top_color=(0xf1, 0xeb, 0xd9), bot_color=(0xc8, 0xbf, 0xa3), opa=255,
    shadow_offset=thumb_shadow_offset, shadow_blur=thumb_shadow_blur, shadow_alpha=100,
    inset_top_alpha=80, inset_bot_alpha=40)
emit_array("pill_toggle_thumb_on", thumb_on_data, thumb_canvas, thumb_canvas)

# ── START pill (height ~18, radius 6) ──
# pad 9+9 lr, 3+3 tb, font rubik12, radius=6
start_h = 21  # 15 + 3 + 3
start_w = 55  # "START"
start_data = render_roundrect(start_w, start_h, 6, 0, EF_FG, 20, EF_FG, 0)
emit_array("pill_start", start_data, start_w, start_h)

# ── Indicator dots ──
# Inactive: 5×5 circle, EF_FG @ 64
dot_inactive = render_circle(5, EF_FG, 64)
emit_array("pill_dot_inactive", dot_inactive, 5, 5)

# Active: 5×5 circle, accent @ 255
dot_active = render_circle(5, EF_GREEN, 255)
emit_array("pill_dot_active", dot_active, 5, 5)

# Active bar: 18×5, radius 3 (for animated expanded dot)
dot_bar = render_roundrect(18, 5, 3, 0, EF_GREEN, 255, EF_GREEN, 0)
emit_array("pill_dot_bar", dot_bar, 18, 5)

# ── "READY" pill with green border ──
ready_data = render_roundrect(80, meta_h, meta_r, 1, META_BG, 179, EF_GREEN, 77)
emit_array("pill_meta_ready_green", ready_data, 80, meta_h)

# ── Action button pills (for backup panel etc.) ──
# "Run Backup" ~100px wide, same height as chips
btn_w = 100
btn_h = chip_h  # 27
btn_r = btn_h // 2

# Active: accent green fill (like toggle ON)
btn_active = render_roundrect(btn_w, btn_h, btn_r, 0, EF_GREEN, 255, EF_GREEN, 0)
emit_array("pill_btn_active", btn_active, btn_w, btn_h)

# Inactive: chip_bg fill (like clock pill)
btn_inactive = render_roundrect(btn_w, btn_h, btn_r, 1, CHIP_BG, 140, EF_FG, 20)
emit_array("pill_btn_inactive", btn_inactive, btn_w, btn_h)

# Highlight/focused: accent fill + bright border (when selected in right pane)
btn_highlight = render_roundrect(btn_w, btn_h, btn_r, 2, EF_GREEN, 255, (0xff, 0xff, 0xff), 180)
emit_array("pill_btn_highlight", btn_highlight, btn_w, btn_h)

# Busy/running: muted accent border, transparent fill
btn_busy = render_roundrect(btn_w, btn_h, btn_r, 1, CHIP_BG, 100, EF_GREEN, 100)
emit_array("pill_btn_busy", btn_busy, btn_w, btn_h)

print("/* End of generated pill data */")
