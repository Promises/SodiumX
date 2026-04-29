#define NANOPRINTF_IMPLEMENTATION
#define NANOPRINTF_SNPRINTF_SAFE_TRIM_STRING_ON_OVERFLOW
#include <lvgl.h>
#include "sodiumx.h"
#ifndef NXDK
#include <signal.h>
static void signal_handler(int sig) { (void)sig; lv_set_quit(LV_QUIT_OTHER); }
#endif

static CRITICAL_SECTION tlsf_crit_sec;
static tlsf_t mem_pool;
static uint8_t mem_pool_data[3U * 1024U * 1024U];

static SDL_mutex *lvgl_mutex;

keyboard_map_t lvgl_keyboard_map[] =
{
    {.sdl_map = SDLK_ESCAPE, .lvgl_map = DASH_SETTINGS_PAGE},  /* Esc = START (menu) */
    {.sdl_map = SDLK_BACKSPACE, .lvgl_map = LV_KEY_ESC},       /* Backspace = B (back) */
    {.sdl_map = SDLK_RETURN, .lvgl_map = LV_KEY_ENTER},        /* Enter = A (launch/select) */
    {.sdl_map = SDLK_PAGEDOWN, .lvgl_map = DASH_PREV_PAGE},    /* PageDown = LB (prev tab) */
    {.sdl_map = SDLK_PAGEUP, .lvgl_map = DASH_NEXT_PAGE},      /* PageUp = RB (next tab) */
    {.sdl_map = SDLK_UP, .lvgl_map = LV_KEY_UP},
    {.sdl_map = SDLK_DOWN, .lvgl_map = LV_KEY_DOWN},
    {.sdl_map = SDLK_LEFT, .lvgl_map = LV_KEY_LEFT},
    {.sdl_map = SDLK_RIGHT, .lvgl_map = LV_KEY_RIGHT},
    {.sdl_map = SDLK_x, .lvgl_map = DASH_CONTEXT_PAGE},        /* X = context menu */
    {.sdl_map = SDLK_y, .lvgl_map = DASH_INFO_PAGE},           /* Y = synopsis/details */
    {.sdl_map = SDLK_s, .lvgl_map = DASH_SETTINGS_PAGE},       /* S = START (menu) */
    {.sdl_map = SDLK_b, .lvgl_map = LV_KEY_ESC},               /* B = back */
    {.sdl_map = SDLK_k, .lvgl_map = DASH_KEY_BACK},              /* K = BACK */
    {.sdl_map = SDLK_q, .lvgl_map = 'L'},                      /* Q = LT (fast scroll left) */
    {.sdl_map = SDLK_e, .lvgl_map = 'R'},                      /* E = RT (fast scroll right) */
    {.sdl_map = SDLK_TAB, .lvgl_map = DASH_NEXT_PAGE},         /* Tab = next tab */
    {.sdl_map = 0, .lvgl_map = 0}
};

gamecontroller_map_t lvgl_gamecontroller_map[] =
{
    {.sdl_map = SDL_CONTROLLER_BUTTON_A, .lvgl_map = LV_KEY_ENTER},
    {.sdl_map = SDL_CONTROLLER_BUTTON_B, .lvgl_map = LV_KEY_ESC},
    {.sdl_map = SDL_CONTROLLER_BUTTON_X, .lvgl_map = DASH_CONTEXT_PAGE},
    {.sdl_map = SDL_CONTROLLER_BUTTON_Y, .lvgl_map = DASH_INFO_PAGE},
    {.sdl_map = SDL_CONTROLLER_BUTTON_BACK, .lvgl_map = DASH_KEY_BACK},
    {.sdl_map = SDL_CONTROLLER_BUTTON_GUIDE, .lvgl_map = 0},
    {.sdl_map = SDL_CONTROLLER_BUTTON_START, .lvgl_map = DASH_SETTINGS_PAGE},
    {.sdl_map = SDL_CONTROLLER_BUTTON_LEFTSTICK, .lvgl_map = 0},
    {.sdl_map = SDL_CONTROLLER_BUTTON_RIGHTSTICK, .lvgl_map = 0},
    {.sdl_map = SDL_CONTROLLER_BUTTON_LEFTSHOULDER, .lvgl_map = DASH_PREV_PAGE},
    {.sdl_map = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, .lvgl_map = DASH_NEXT_PAGE},
    {.sdl_map = SDL_CONTROLLER_BUTTON_DPAD_UP, .lvgl_map = LV_KEY_UP},
    {.sdl_map = SDL_CONTROLLER_BUTTON_DPAD_DOWN, .lvgl_map = LV_KEY_DOWN},
    {.sdl_map = SDL_CONTROLLER_BUTTON_DPAD_LEFT, .lvgl_map = LV_KEY_LEFT},
    {.sdl_map = SDL_CONTROLLER_BUTTON_DPAD_RIGHT, .lvgl_map = LV_KEY_RIGHT},
    {.sdl_map = 0, .lvgl_map = 0}
};

// lvgl isn't thread safe, but we can somewhat make it
// by wrapping task handler and any other interactions with these locks
void lvgl_getlock(void)
{
    if (SDL_LockMutex(lvgl_mutex))
    {
        assert(0);
    }
}

void lvgl_removelock(void)
{
    if (SDL_UnlockMutex(lvgl_mutex))
    {
        assert(0);
    }
}

// Output handler for lvgl
void lvgl_putstring(const char *buf)
{
    printf("%s", buf);
}

size_t tlsf_usage = 0;
// Replace lvgls internal allocator with basically the same thing
// but wrapped in crit sec for thread safety.
void *lx_mem_alloc(size_t size)
{
    EnterCriticalSection(&tlsf_crit_sec);
    void *ptr = tlsf_malloc(mem_pool, size);
    tlsf_usage += tlsf_block_size(ptr);
    LeaveCriticalSection(&tlsf_crit_sec);
    return ptr;
}

void *lx_mem_realloc(void *data, size_t new_size)
{
    EnterCriticalSection(&tlsf_crit_sec);
    tlsf_usage -= tlsf_block_size(data);
    void *ptr = tlsf_realloc(mem_pool, data, new_size);
    tlsf_usage += tlsf_block_size(ptr);
    LeaveCriticalSection(&tlsf_crit_sec);
    return ptr;
}

void lx_mem_free(void *data)
{
    EnterCriticalSection(&tlsf_crit_sec);
    tlsf_usage -= tlsf_block_size(data);
    tlsf_free(mem_pool, data);
    LeaveCriticalSection(&tlsf_crit_sec);
}

void lx_mem_usage(uint32_t *used, uint32_t *capacity)
{
    if (used)
    {
        EnterCriticalSection(&tlsf_crit_sec);
        *used = tlsf_usage;
        LeaveCriticalSection(&tlsf_crit_sec);
    }
    if (capacity)
    {
        *capacity = sizeof(mem_pool_data);
    }
}

static void npf_putchar(int c, void *ctx)
{
    (void)ctx;
    #ifdef NXDK
    DbgPrint("%c", c);
    #else
    printf("%c", c);
    #endif
}

void dash_printf(dash_debug_level_t level, const char *format, ...)
{
    if (level < NANO_DEBUG_LEVEL)
    {
        return;
    }
    va_list argList;
    va_start(argList, format);
    npf_vpprintf(npf_putchar, NULL, format, argList);
    va_end(argList);

    /* Forward to remote debug clients */
    va_list argList2;
    va_start(argList2, format);
    char remote_buf[512];
    vsnprintf(remote_buf, sizeof(remote_buf), format, argList2);
    va_end(argList2);
    dash_remote_log("%s", remote_buf);
}

int main(int argc, char* argv[]) {
    (void) argc;
    (void) argv;

    int w,h;
    InitializeCriticalSection(&tlsf_crit_sec);
    mem_pool = tlsf_create_with_pool(mem_pool_data, sizeof(mem_pool_data));

    toml_set_memutil(lx_mem_alloc, lx_mem_free);

    #ifndef NXDK
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    #endif

    dash_printf(LEVEL_TRACE, "Initialising Platform\n");
    platform_init(&w, &h);

    /* Check debug flag early — if set, start remote server before display init
     * so GPU errors during init can be captured via log streaming. */
    {
        FILE *dbg_flag = fopen(DASH_DEBUG_WAIT_FLAG, "r");
        if (dbg_flag)
        {
            fclose(dbg_flag);
            dash_printf(LEVEL_TRACE, "Debug flag detected — starting early remote server\n");
            dash_remote_init_early();
            dash_debug_install_gpu_hook();
        }
    }

    lvgl_mutex = SDL_CreateMutex();
    assert(lvgl_mutex);

    dash_printf(LEVEL_TRACE, "Initialising LVGL\n");
    lv_init();
    lv_log_register_print_cb(lvgl_putstring);
    dash_printf(LEVEL_TRACE, "Initialising Display at w: %d, h: %d\n", w, h);
    lv_port_disp_init(w, h);
    lv_port_indev_init(false);

    dash_printf(LEVEL_TRACE, "Starting remote debug server\n");
    dash_remote_init();

    /* Wait-for-debugger gate: triggered by flag file from "reload-debug" command.
     * Shows message, waits for log client or B press, then proceeds. */
    {
        bool wait_for_debug = false;
        FILE *flag = fopen(DASH_DEBUG_WAIT_FLAG, "r");
        if (flag)
        {
            fclose(flag);
            remove(DASH_DEBUG_WAIT_FLAG);
            wait_for_debug = true;
        }

        if (wait_for_debug)
        {
            lv_obj_t *wait_label = lv_label_create(lv_scr_act());
            lv_label_set_text(wait_label, "Waiting for debug client...\n"
                                           "Connect and send: log on\n\n"
                                           "Or press B / Backspace to skip");
            lv_obj_set_style_text_color(wait_label, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_text_font(wait_label, &dash_font_ui_16, LV_PART_MAIN);
            lv_obj_set_style_text_align(wait_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_center(wait_label);

            dash_printf(LEVEL_TRACE, "Debug wait: listening on port %d...\n", DASH_REMOTE_PORT);

            while (!dash_remote_has_log_client())
            {
                lvgl_getlock();
                lv_task_handler();
                lvgl_removelock();
            #ifdef NXDK
                _lv_disp_refr_timer(NULL);
                pb_wait_for_vbl();
            #else
                SDL_Delay(LV_DISP_DEF_REFR_PERIOD);
            #endif

                SDL_Event e;
                while (SDL_PollEvent(&e))
                {
                    if ((e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_B) ||
                        (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_BACKSPACE)))
                        goto skip_wait;
                }
            }
            dash_printf(LEVEL_TRACE, "Debug client connected! Proceeding...\n");
            skip_wait:
            lv_obj_del(wait_label);
            lv_obj_clean(lv_scr_act());
        }
    }

    /* Check rebuild-db flag — delete DB before init so it gets rebuilt */
    {
        FILE *flag = fopen(DASH_REBUILD_DB_FLAG, "r");
        if (flag)
        {
            fclose(flag);
            remove(DASH_REBUILD_DB_FLAG);
            dash_printf(LEVEL_TRACE, "Rebuild DB flag found — deleting database\n");
            remove(DASH_DATABASE_PATH);
        }
    }

    dash_printf(LEVEL_TRACE, "Creating dash\n");
    dash_perf_init();
    dash_init();

    dash_printf(LEVEL_TRACE, "Enter dash busy loop\n");

    #ifdef NXDK
    lv_disp_t *disp = lv_obj_get_disp(lv_scr_act());
    lv_timer_del(disp->refr_timer);
    disp->refr_timer = NULL;
    #endif

    while (lv_get_quit() == LV_QUIT_NONE)
    {
        int frame_start = SDL_GetTicks();
        dash_perf_frame_begin();

        dash_perf_mark(PERF_TASK_HANDLER);
        lvgl_getlock();
        lv_task_handler();
        lvgl_removelock();
        dash_perf_mark(PERF_TASK_HANDLER);

        #ifdef NXDK
        dash_perf_mark(PERF_GPU_RENDER);
        lvgl_getlock();
        _lv_disp_refr_timer(NULL);
        lvgl_removelock();
        dash_perf_mark(PERF_GPU_RENDER);

        dash_perf_mark(PERF_GPU_WAIT);
        if (!dash_settings.disable_vsync)
            pb_wait_for_vbl();
        dash_perf_mark(PERF_GPU_WAIT);
        #else
        {
            int elapsed = SDL_GetTicks() - frame_start;
            if (elapsed < LV_DISP_DEF_REFR_PERIOD)
                SDL_Delay(LV_DISP_DEF_REFR_PERIOD - elapsed);
        }
        #endif

        dash_perf_frame_end();
    }
    dash_printf(LEVEL_TRACE, "Quitting dash with quit event %d\n", lv_get_quit());
    dash_backup_abort();
    dash_backup_deinit();
    dash_remote_deinit();
    lv_port_disp_deinit();
    lv_port_indev_deinit();
    platform_quit(lv_get_quit());
    return 0;
}
