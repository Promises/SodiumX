// SPDX-License-Identifier: MIT
// Performance profiling for SodiumX

#include "sodiumx.h"
#include "dash_perf.h"
#include "dash_remote.h"

/* ── Per-frame accumulators (reset each frame) ── */
static uint32_t frame_start_tick;
static uint32_t section_start[PERF_SECTION_COUNT];
static uint32_t section_accum[PERF_SECTION_COUNT];
static bool     section_active[PERF_SECTION_COUNT];
static uint32_t frame_draw_calls;
static uint32_t frame_rounded_rects;
static uint32_t frame_texture_binds;

/* ── Window accumulators (reset each second) ── */
static uint32_t window_start_tick;
static uint32_t window_frame_count;
static uint32_t window_frame_total_ms;
static uint32_t window_frame_max_ms;
static uint32_t window_frame_min_ms;
static uint32_t window_section_total[PERF_SECTION_COUNT];
static uint32_t window_section_max[PERF_SECTION_COUNT];
static uint32_t window_draw_calls_total;
static uint32_t window_rounded_rects_total;
static uint32_t window_texture_binds_total;

/* ── Published snapshot (read by perf command) ── */
static dash_perf_t perf_snapshot;

/* ── Thumbnail cache stats (set externally) ── */
static uint32_t thumb_count;
static uint32_t thumb_bytes;

/* ── Snapshot callback for status command ── */
static int perf_snapshot_cb(char *buf, int size)
{
    const dash_perf_t *p = &perf_snapshot;

    return lv_snprintf(buf, size,
        "[perf]\n"
        "fps=%u\n"
        "frame_avg_ms=%u\n"
        "frame_max_ms=%u\n"
        "frame_min_ms=%u\n"
        "task_handler_avg_ms=%u\n"
        "task_handler_max_ms=%u\n"
        "gpu_render_avg_ms=%u\n"
        "gpu_render_max_ms=%u\n"
        "gpu_wait_avg_ms=%u\n"
        "gpu_wait_max_ms=%u\n"
        "gui_heap_kb=%u/%u\n"
        "sys_ram_mb=%u/%u\n"
        "thumb_cache=%u/%u\n"
        "anim_count=%u\n"
        "obj_count=%u\n"
        "draw_calls=%u\n"
        "rounded_rects=%u\n"
        "texture_binds=%u\n",
        p->fps,
        p->frame_avg_ms, p->frame_max_ms, p->frame_min_ms,
        p->section_avg_ms[PERF_TASK_HANDLER], p->section_max_ms[PERF_TASK_HANDLER],
        p->section_avg_ms[PERF_GPU_RENDER], p->section_max_ms[PERF_GPU_RENDER],
        p->section_avg_ms[PERF_GPU_WAIT], p->section_max_ms[PERF_GPU_WAIT],
        p->gui_heap_used / 1024, p->gui_heap_capacity / 1024,
        p->sys_ram_used_mb, p->sys_ram_total_mb,
        p->thumb_cache_count, p->thumb_cache_bytes,
        p->anim_count, p->obj_count,
        p->draw_calls, p->rounded_rects, p->texture_binds);
}

static uint32_t count_obj_recursive(lv_obj_t *obj)
{
    uint32_t count = 1;
    uint32_t child_cnt = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < child_cnt; i++)
    {
        count += count_obj_recursive(lv_obj_get_child(obj, i));
    }
    return count;
}

static void finalize_window(void)
{
    uint32_t fc = window_frame_count;
    if (fc == 0) fc = 1;

    perf_snapshot.fps = window_frame_count;
    perf_snapshot.frame_avg_ms = window_frame_total_ms / fc;
    perf_snapshot.frame_max_ms = window_frame_max_ms;
    perf_snapshot.frame_min_ms = window_frame_min_ms;

    for (int i = 0; i < PERF_SECTION_COUNT; i++)
    {
        perf_snapshot.section_avg_ms[i] = window_section_total[i] / fc;
        perf_snapshot.section_max_ms[i] = window_section_max[i];
    }

    /* Memory */
    lx_mem_usage(&perf_snapshot.gui_heap_used, &perf_snapshot.gui_heap_capacity);

#ifdef NXDK
    extern void get_ram_usage(uint32_t *mem_size, uint32_t *mem_used);
    get_ram_usage(&perf_snapshot.sys_ram_total_mb, &perf_snapshot.sys_ram_used_mb);
#endif

    perf_snapshot.thumb_cache_count = thumb_count;
    perf_snapshot.thumb_cache_bytes = thumb_bytes;

    /* LVGL internals */
    perf_snapshot.anim_count = lv_anim_count_running();

    /* Object count — only count occasionally, it's expensive */
    lv_obj_t *scr = lv_scr_act();
    if (scr)
        perf_snapshot.obj_count = count_obj_recursive(scr);

    /* Draw stats (average per frame) */
    perf_snapshot.draw_calls = window_draw_calls_total / fc;
    perf_snapshot.rounded_rects = window_rounded_rects_total / fc;
    perf_snapshot.texture_binds = window_texture_binds_total / fc;
}

static void reset_window(void)
{
    window_start_tick = SDL_GetTicks();
    window_frame_count = 0;
    window_frame_total_ms = 0;
    window_frame_max_ms = 0;
    window_frame_min_ms = 0xFFFFFFFF;
    memset(window_section_total, 0, sizeof(window_section_total));
    memset(window_section_max, 0, sizeof(window_section_max));
    window_draw_calls_total = 0;
    window_rounded_rects_total = 0;
    window_texture_binds_total = 0;
}

/* ── Public API ── */

void dash_perf_init(void)
{
    memset(&perf_snapshot, 0, sizeof(perf_snapshot));
    reset_window();
    dash_snapshot_register(perf_snapshot_cb);
}

void dash_perf_frame_begin(void)
{
    frame_start_tick = SDL_GetTicks();
    memset(section_accum, 0, sizeof(section_accum));
    memset(section_active, 0, sizeof(section_active));
    frame_draw_calls = 0;
    frame_rounded_rects = 0;
    frame_texture_binds = 0;
}

void dash_perf_frame_end(void)
{
    uint32_t now = SDL_GetTicks();
    uint32_t frame_ms = now - frame_start_tick;

    window_frame_count++;
    window_frame_total_ms += frame_ms;
    if (frame_ms > window_frame_max_ms) window_frame_max_ms = frame_ms;
    if (frame_ms < window_frame_min_ms) window_frame_min_ms = frame_ms;

    for (int i = 0; i < PERF_SECTION_COUNT; i++)
    {
        window_section_total[i] += section_accum[i];
        if (section_accum[i] > window_section_max[i])
            window_section_max[i] = section_accum[i];
    }

    window_draw_calls_total += frame_draw_calls;
    window_rounded_rects_total += frame_rounded_rects;
    window_texture_binds_total += frame_texture_binds;

    /* Finalize and reset window every second */
    if (now - window_start_tick >= 1000)
    {
        finalize_window();
        reset_window();
    }
}

void dash_perf_mark(perf_section_t section)
{
    if (section >= PERF_SECTION_COUNT) return;

    if (!section_active[section])
    {
        section_start[section] = SDL_GetTicks();
        section_active[section] = true;
    }
    else
    {
        section_accum[section] += SDL_GetTicks() - section_start[section];
        section_active[section] = false;
    }
}

const dash_perf_t *dash_perf_get(void)
{
    return &perf_snapshot;
}

void dash_perf_inc_draw_calls(void)    { frame_draw_calls++; }
void dash_perf_inc_rounded_rects(void) { frame_rounded_rects++; }
void dash_perf_inc_texture_binds(void) { frame_texture_binds++; }

void dash_perf_set_thumb_cache(uint32_t count, uint32_t bytes)
{
    thumb_count = count;
    thumb_bytes = bytes;
}
