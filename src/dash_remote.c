// SPDX-License-Identifier: MIT
// Remote debug/test TCP server for SodiumX

#include "sodiumx.h"
#include "dash_remote.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef NXDK
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#define SOCKET_TYPE int
#define INVALID_SOCK -1
#define closesocket(s) lwip_close(s)
#define sock_send(s,b,l) lwip_send(s,b,l,0)
#define sock_recv(s,b,l) lwip_recv(s,b,l,0)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#define SOCKET_TYPE int
#define INVALID_SOCK -1
#define closesocket(s) close(s)
#define sock_send(s,b,l) send(s,b,l,0)
#define sock_recv(s,b,l) recv(s,b,l,0)
#endif

/* ── Client state ── */
typedef struct {
    SOCKET_TYPE fd;
    bool log_streaming;
} remote_client_t;

static SOCKET_TYPE listen_fd = INVALID_SOCK;
static remote_client_t clients[DASH_REMOTE_MAX_CLIENTS];
static SDL_mutex *clients_mutex;
static SDL_Thread *server_thread;
static volatile bool server_running;

/* ── Helpers ── */
static void send_str(SOCKET_TYPE fd, const char *s)
{
    if (fd == INVALID_SOCK) return;
    size_t len = strlen(s);
    sock_send(fd, s, (int)len);
}

static void set_nonblocking(SOCKET_TYPE fd)
{
#ifndef NXDK
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    int opt = 1;
    lwip_ioctl(fd, FIONBIO, &opt);
#endif
}

/* ── Key name → LVGL key mapping ── */
static int key_from_name(const char *name)
{
    /* D-pad */
    if (strcmp(name, "up") == 0)       return LV_KEY_UP;
    if (strcmp(name, "down") == 0)     return LV_KEY_DOWN;
    if (strcmp(name, "left") == 0)     return LV_KEY_LEFT;
    if (strcmp(name, "right") == 0)    return LV_KEY_RIGHT;
    /* Face buttons */
    if (strcmp(name, "a") == 0)        return DASH_KEY_A;
    if (strcmp(name, "b") == 0)        return DASH_KEY_B;
    if (strcmp(name, "x") == 0)        return DASH_KEY_X;
    if (strcmp(name, "y") == 0)        return DASH_KEY_Y;
    /* Menu buttons */
    if (strcmp(name, "start") == 0)    return DASH_KEY_START;
    if (strcmp(name, "back") == 0)     return DASH_KEY_BACK;
    /* Shoulder buttons (original Xbox: White/Black) */
    if (strcmp(name, "white") == 0)    return DASH_KEY_WHITE;
    if (strcmp(name, "black") == 0)    return DASH_KEY_BLACK;
    /* Triggers */
    if (strcmp(name, "lt") == 0)       return DASH_KEY_LT;
    if (strcmp(name, "rt") == 0)       return DASH_KEY_RT;
    /* Legacy aliases */
    if (strcmp(name, "enter") == 0)    return DASH_KEY_A;
    if (strcmp(name, "esc") == 0)      return DASH_KEY_B;
    if (strcmp(name, "s") == 0)        return DASH_KEY_START;
    if (strcmp(name, "pageup") == 0)   return DASH_KEY_BLACK;
    if (strcmp(name, "pagedown") == 0) return DASH_KEY_WHITE;
    if (strcmp(name, "q") == 0)        return DASH_KEY_LT;
    if (strcmp(name, "e") == 0)        return DASH_KEY_RT;
    return 0;
}

/* Inject a key event into LVGL's focused object */
static void inject_key(int key)
{
    if (key == 0) return;

    lvgl_getlock();
    lv_group_t *g = lv_group_get_default();
    lv_obj_t *focused = lv_group_get_focused(g);
    if (focused)
    {
        lv_key_t lv_key = (lv_key_t)key;
        lv_event_send(focused, LV_EVENT_KEY, &lv_key);
    }
    lvgl_removelock();
}

/* ── Screenshot: render to BMP in memory and send over socket ── */
#ifdef NXDK
#include <pbkit/pbkit.h>
#endif

static SDL_Surface *capture_framebuffer(void)
{
#ifdef NXDK
    /* Force LVGL to redraw and wait for GPU to finish rendering */
    _lv_disp_refr_timer(NULL);
    while (pb_busy()) { SDL_Delay(0); }
    while (pb_finished()) { SDL_Delay(0); }

    int w = (int)pb_back_buffer_width();
    int h = (int)pb_back_buffer_height();
    int pitch = (int)pb_back_buffer_pitch();
    DWORD *fb = pb_back_buffer();

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return NULL;

    for (int y = 0; y < h; y++)
    {
        memcpy((uint8_t *)surface->pixels + y * surface->pitch,
               (uint8_t *)fb + y * pitch,
               w * 4);
    }
    return surface;
#else
    extern SDL_Renderer *renderer;
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return NULL;

    SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch);
    return surface;
#endif
}

static void handle_screenshot(SOCKET_TYPE fd)
{
    lvgl_getlock();
    SDL_Surface *surface = capture_framebuffer();
    lvgl_removelock();

    if (!surface)
    {
        send_str(fd, "ERR screenshot failed\n");
        return;
    }

    /* Save to temp file then read and send */
#ifdef NXDK
    const char *path = "E:\\UDATA\\SodiumX\\screenshot.bmp";
#else
    const char *path = "/tmp/sodiumx_screenshot.bmp";
#endif
    SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);

    FILE *f = fopen(path, "rb");
    if (!f)
    {
        send_str(fd, "ERR cannot read screenshot file\n");
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char header[64];
    snprintf(header, sizeof(header), "OK %ld\n", size);
    send_str(fd, header);

    /* Stream the BMP data */
    char buf[4096];
    while (size > 0)
    {
        size_t chunk = (size > (long)sizeof(buf)) ? sizeof(buf) : (size_t)size;
        size_t read = fread(buf, 1, chunk, f);
        if (read == 0) break;
        sock_send(fd, buf, (int)read);
        size -= (long)read;
    }
    fclose(f);
}

/* ── Snapshot registry ── */
static dash_snapshot_fn snapshot_fns[DASH_SNAPSHOT_MAX];
static int snapshot_fn_count = 0;

void dash_snapshot_register(dash_snapshot_fn fn)
{
    if (snapshot_fn_count < DASH_SNAPSHOT_MAX)
        snapshot_fns[snapshot_fn_count++] = fn;
}

/* ── Status query ── */
static void handle_status(SOCKET_TYPE fd)
{
    char buf[2048];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "OK\n");

    for (int i = 0; i < snapshot_fn_count; i++) {
        int wrote = snapshot_fns[i](buf + pos, (int)(sizeof(buf) - pos));
        if (wrote > 0) pos += wrote;
    }

    send_str(fd, buf);
}

/* ── Benchmark runner ── */
static void handle_bench(remote_client_t *client, const char *args)
{
    int count = 20;
    char type[32] = "scroll";

    /* Parse: "scroll [N]" or "idle [N]" */
    sscanf(args, "%31s %d", type, &count);
    if (count < 1) count = 1;
    if (count > 200) count = 200;

    if (strcmp(type, "idle") == 0)
    {
        /* Measure idle frame times for N seconds */
        char resp[128];
        snprintf(resp, sizeof(resp), "OK bench_start idle %d\n", count);
        send_str(client->fd, resp);

        uint32_t start = SDL_GetTicks();
        uint32_t duration_ms = (uint32_t)count * 1000;
        uint32_t samples = 0;
        uint32_t total_ms = 0;
        uint32_t max_ms = 0;
        uint32_t min_ms = 0xFFFFFFFF;

        while (SDL_GetTicks() - start < duration_ms)
        {
            const dash_perf_t *p = dash_perf_get();
            if (p->fps > 0)
            {
                samples++;
                total_ms += p->frame_avg_ms;
                if (p->frame_max_ms > max_ms) max_ms = p->frame_max_ms;
                if (p->frame_min_ms < min_ms) min_ms = p->frame_min_ms;
            }
            SDL_Delay(1000); /* Sample once per perf window */
        }

        if (samples == 0) samples = 1;
        char result[256];
        snprintf(result, sizeof(result),
            "bench_done idle samples=%u avg_ms=%u max_ms=%u min_ms=%u\n",
            samples, total_ms / samples, max_ms, min_ms);
        send_str(client->fd, result);
    }
    else if (strcmp(type, "scroll") == 0)
    {
        /* Scroll right N times, collecting perf after each */
        char resp[128];
        snprintf(resp, sizeof(resp), "OK bench_start scroll %d\n", count);
        send_str(client->fd, resp);

        uint32_t total_max = 0;
        uint32_t total_avg_sum = 0;
        uint32_t drops = 0; /* frames > 20ms */

        for (int i = 0; i < count; i++)
        {
            /* Inject right arrow */
            inject_key(LV_KEY_RIGHT);

            /* Wait for animations to settle (RAIL_ANIM_MS = 550) + 1 perf window */
            SDL_Delay(700);

            const dash_perf_t *p = dash_perf_get();
            char line_buf[256];
            snprintf(line_buf, sizeof(line_buf),
                "scroll=%d fps=%u avg_ms=%u max_ms=%u anims=%u draws=%u rects=%u\n",
                i + 1, p->fps, p->frame_avg_ms, p->frame_max_ms,
                p->anim_count, p->draw_calls, p->rounded_rects);
            send_str(client->fd, line_buf);

            total_avg_sum += p->frame_avg_ms;
            if (p->frame_max_ms > total_max) total_max = p->frame_max_ms;
            if (p->frame_max_ms > 20) drops++;
        }

        char result[256];
        snprintf(result, sizeof(result),
            "bench_done scroll=%d avg_ms=%u max_ms=%u drops=%u\n",
            count, count > 0 ? total_avg_sum / count : 0, total_max, drops);
        send_str(client->fd, result);
    }
    else
    {
        send_str(client->fd, "ERR unknown bench type (scroll|idle)\n");
    }
}

/* ── Process one command line from a client ── */
static void process_command(remote_client_t *client, char *line)
{
    /* Trim trailing \r\n */
    char *end = line + strlen(line) - 1;
    while (end >= line && (*end == '\r' || *end == '\n')) *end-- = '\0';

    if (strlen(line) == 0) return;

    if (strcmp(line, "ping") != 0)
        dash_printf(LEVEL_TRACE, "[REMOTE] cmd: '%s'\n", line);

    if (strncmp(line, "key ", 4) == 0)
    {
        int k = key_from_name(line + 4);
        if (k)
        {
            inject_key(k);
            send_str(client->fd, "OK\n");
        }
        else
        {
            send_str(client->fd, "ERR unknown key\n");
        }
    }
    else if (strcmp(line, "screenshot") == 0)
    {
        handle_screenshot(client->fd);
    }
    else if (strcmp(line, "log on") == 0)
    {
        client->log_streaming = true;
        send_str(client->fd, "OK log streaming on\n");
    }
    else if (strcmp(line, "log off") == 0)
    {
        client->log_streaming = false;
        send_str(client->fd, "OK log streaming off\n");
    }
    else if (strcmp(line, "ping") == 0)
    {
        send_str(client->fd, "pong\n");
    }
    else if (strcmp(line, "status") == 0)
    {
        handle_status(client->fd);
    }
    else if (strcmp(line, "quit") == 0)
    {
        send_str(client->fd, "OK shutting down\n");
        lv_set_quit(LV_SHUTDOWN);
    }
    else if (strncmp(line, "launch ", 7) == 0)
    {
        const char *xbe_path = line + 7;
        send_str(client->fd, "OK launching\n");
        strncpy(dash_launch_path, xbe_path, DASH_MAX_PATH - 1);
        lv_set_quit(LV_QUIT_OTHER);
    }
    else if (strcmp(line, "perf") == 0)
    {
        const dash_perf_t *p = dash_perf_get();
        char buf[1024];
        int n = lv_snprintf(buf, sizeof(buf),
            "OK\n[perf]\n"
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
        (void)n;
        send_str(client->fd, buf);
    }
    else if (strcmp(line, "vsync on") == 0)
    {
        dash_settings.disable_vsync = false;
        send_str(client->fd, "OK vsync on\n");
    }
    else if (strcmp(line, "vsync off") == 0)
    {
        dash_settings.disable_vsync = true;
        send_str(client->fd, "OK vsync off\n");
    }
    else if (strncmp(line, "bench ", 6) == 0)
    {
        handle_bench(client, line + 6);
    }
    else if (strncmp(line, "reload", 6) == 0)
    {
        /* reload [--debug] [--rebuild-db]
         * --debug:      new instance waits for log client before booting
         * --rebuild-db: delete the DB so it rescans all game folders */
        bool flag_debug = (strstr(line, "--debug") != NULL);
        bool flag_rebuild = (strstr(line, "--rebuild-db") != NULL);

        char resp[128];
        snprintf(resp, sizeof(resp), "OK reload%s%s\n",
                 flag_debug ? " +debug" : "",
                 flag_rebuild ? " +rebuild-db" : "");
        send_str(client->fd, resp);

        if (flag_debug)
        {
            FILE *f = fopen(DASH_DEBUG_WAIT_FLAG, "w");
            if (f) { fprintf(f, "1"); fclose(f); }
        }

        if (flag_rebuild)
        {
            FILE *f = fopen(DASH_REBUILD_DB_FLAG, "w");
            if (f) { fprintf(f, "1"); fclose(f); }
        }

        strncpy(dash_launch_path, "F:\\Apps\\testing\\default.xbe", DASH_MAX_PATH - 1);
        lv_set_quit(LV_QUIT_OTHER);
    }
    else
    {
        send_str(client->fd, "ERR unknown command\n");
    }
}

/* ── Server thread ── */
static int server_thread_fn(void *param)
{
    (void)param;
    char buf[512];

    printf("[REMOTE] Server listening on port %d\n", DASH_REMOTE_PORT);

    while (server_running)
    {
        /* Accept new connections (non-blocking) */
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        SOCKET_TYPE new_fd = accept(listen_fd, (struct sockaddr *)&addr, &addr_len);
        if (new_fd != INVALID_SOCK)
        {
            set_nonblocking(new_fd);
            SDL_LockMutex(clients_mutex);
            bool added = false;
            for (int i = 0; i < DASH_REMOTE_MAX_CLIENTS; i++)
            {
                if (clients[i].fd == INVALID_SOCK)
                {
                    clients[i].fd = new_fd;
                    clients[i].log_streaming = false;
                    added = true;
                    printf("[REMOTE] Client %d connected\n", i);
                    send_str(new_fd, "SodiumX Remote Debug v1\n");
                    break;
                }
            }
            SDL_UnlockMutex(clients_mutex);
            if (!added)
            {
                send_str(new_fd, "ERR max clients\n");
                closesocket(new_fd);
            }
        }

        /* Poll each client for data */
        SDL_LockMutex(clients_mutex);
        for (int i = 0; i < DASH_REMOTE_MAX_CLIENTS; i++)
        {
            if (clients[i].fd == INVALID_SOCK) continue;

            int n = sock_recv(clients[i].fd, buf, sizeof(buf) - 1);
            if (n > 0)
            {
                buf[n] = '\0';
                /* Handle each line in the buffer */
                char *line = strtok(buf, "\n");
                while (line)
                {
                    process_command(&clients[i], line);
                    line = strtok(NULL, "\n");
                }
            }
            else if (n == 0)
            {
                /* Client disconnected */
                printf("[REMOTE] Client %d disconnected\n", i);
                closesocket(clients[i].fd);
                clients[i].fd = INVALID_SOCK;
                clients[i].log_streaming = false;
            }
            /* n < 0 is EAGAIN/EWOULDBLOCK on non-blocking — just skip */
        }
        SDL_UnlockMutex(clients_mutex);

        SDL_Delay(16); /* ~60Hz poll rate */
    }

    return 0;
}

/* ── Public API ── */

static bool try_bind_listen(void)
{
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == INVALID_SOCK) return false;

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(DASH_REMOTE_PORT);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        closesocket(listen_fd);
        listen_fd = INVALID_SOCK;
        return false;
    }

    if (listen(listen_fd, 2) < 0)
    {
        closesocket(listen_fd);
        listen_fd = INVALID_SOCK;
        return false;
    }

    set_nonblocking(listen_fd);
    return true;
}

static int startup_thread_fn(void *param)
{
    (void)param;
    /* Retry until network stack is ready (lwIP may still be initializing) */
    for (int attempt = 0; attempt < 60; attempt++)
    {
        if (try_bind_listen())
        {
            printf("[REMOTE] Server listening on port %d\n", DASH_REMOTE_PORT);
            server_running = true;
            server_thread_fn(NULL);
            return 0;
        }
        SDL_Delay(500);
    }
    printf("[REMOTE] Failed to bind port %d after retries\n", DASH_REMOTE_PORT);
    return 1;
}

void dash_remote_init(void)
{
    /* Skip if already initialized by dash_remote_init_early */
    if (clients_mutex) return;

    clients_mutex = SDL_CreateMutex();
    for (int i = 0; i < DASH_REMOTE_MAX_CLIENTS; i++)
    {
        clients[i].fd = INVALID_SOCK;
        clients[i].log_streaming = false;
    }

    if (try_bind_listen())
    {
        printf("[REMOTE] Server listening on port %d\n", DASH_REMOTE_PORT);
        server_running = true;
        server_thread = SDL_CreateThread(server_thread_fn, "remote_debug", NULL);
    }
    else
    {
        printf("[REMOTE] Failed to bind port %d\n", DASH_REMOTE_PORT);
    }
}

/* Early init variant: retries bind in a background thread until network is ready.
 * Use when debug flag is set so the server is available during early boot. */
void dash_remote_init_early(void)
{
    clients_mutex = SDL_CreateMutex();
    for (int i = 0; i < DASH_REMOTE_MAX_CLIENTS; i++)
    {
        clients[i].fd = INVALID_SOCK;
        clients[i].log_streaming = false;
    }

    server_thread = SDL_CreateThread(startup_thread_fn, "remote_debug", NULL);
}

void dash_remote_deinit(void)
{
    server_running = false;
    if (server_thread)
    {
        SDL_WaitThread(server_thread, NULL);
        server_thread = NULL;
    }

    SDL_LockMutex(clients_mutex);
    for (int i = 0; i < DASH_REMOTE_MAX_CLIENTS; i++)
    {
        if (clients[i].fd != INVALID_SOCK)
        {
            closesocket(clients[i].fd);
            clients[i].fd = INVALID_SOCK;
        }
    }
    SDL_UnlockMutex(clients_mutex);

    if (listen_fd != INVALID_SOCK)
    {
        closesocket(listen_fd);
        listen_fd = INVALID_SOCK;
    }

    SDL_DestroyMutex(clients_mutex);
    clients_mutex = NULL;
}

void dash_remote_log(const char *fmt, ...)
{
    if (!clients_mutex) return;

    char buf[512];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);

    if (n <= 0) return;

    /* Ensure newline */
    if (buf[n - 1] != '\n')
    {
        buf[n] = '\n';
        buf[n + 1] = '\0';
    }

    SDL_LockMutex(clients_mutex);
    for (int i = 0; i < DASH_REMOTE_MAX_CLIENTS; i++)
    {
        if (clients[i].fd != INVALID_SOCK && clients[i].log_streaming)
        {
            char prefix[] = "[LOG] ";
            sock_send(clients[i].fd, prefix, (int)sizeof(prefix) - 1);
            sock_send(clients[i].fd, buf, (int)strlen(buf));
        }
    }
    SDL_UnlockMutex(clients_mutex);
}

bool dash_remote_has_log_client(void)
{
    if (!clients_mutex) return false;
    bool found = false;
    SDL_LockMutex(clients_mutex);
    for (int i = 0; i < DASH_REMOTE_MAX_CLIENTS; i++)
    {
        if (clients[i].fd != INVALID_SOCK && clients[i].log_streaming)
        {
            found = true;
            break;
        }
    }
    SDL_UnlockMutex(clients_mutex);
    return found;
}
