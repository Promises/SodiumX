// SPDX-License-Identifier: MIT
// Performance profiling for SodiumX
// Lightweight always-on counters aggregated over 1-second windows.
// Zero overhead when not queried — just a few SDL_GetTicks() calls per frame.

#ifndef _DASH_PERF_H
#define _DASH_PERF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Timing sections for dash_perf_mark() — call twice to start/stop */
typedef enum {
    PERF_TASK_HANDLER,
    PERF_GPU_RENDER,
    PERF_GPU_WAIT,
    PERF_SECTION_COUNT
} perf_section_t;

/* Aggregated stats over the last 1-second window */
typedef struct {
    /* Frame timing (ms) */
    uint32_t fps;
    uint32_t frame_avg_ms;
    uint32_t frame_max_ms;
    uint32_t frame_min_ms;

    /* CPU breakdown — average ms per frame */
    uint32_t section_avg_ms[PERF_SECTION_COUNT];
    uint32_t section_max_ms[PERF_SECTION_COUNT];

    /* Memory */
    uint32_t gui_heap_used;
    uint32_t gui_heap_capacity;
    uint32_t sys_ram_used_mb;
    uint32_t sys_ram_total_mb;
    uint32_t thumb_cache_count;
    uint32_t thumb_cache_bytes;

    /* LVGL internals */
    uint32_t anim_count;
    uint32_t obj_count;

    /* Draw stats (per frame, averaged over window) */
    uint32_t draw_calls;
    uint32_t rounded_rects;
    uint32_t texture_binds;
} dash_perf_t;

void dash_perf_init(void);
void dash_perf_frame_begin(void);
void dash_perf_frame_end(void);
void dash_perf_mark(perf_section_t section);
const dash_perf_t *dash_perf_get(void);

/* Called from XGU driver to count draw operations */
void dash_perf_inc_draw_calls(void);
void dash_perf_inc_rounded_rects(void);
void dash_perf_inc_texture_binds(void);

/* Called from dash_scroller to report cache stats */
void dash_perf_set_thumb_cache(uint32_t count, uint32_t bytes);

#ifdef __cplusplus
}
#endif

#endif
