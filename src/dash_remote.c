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
    if (strcmp(name, "right") == 0)    return LV_KEY_RIGHT;
    if (strcmp(name, "left") == 0)     return LV_KEY_LEFT;
    if (strcmp(name, "up") == 0)       return LV_KEY_UP;
    if (strcmp(name, "down") == 0)     return LV_KEY_DOWN;
    if (strcmp(name, "enter") == 0)    return LV_KEY_ENTER;
    if (strcmp(name, "a") == 0)        return LV_KEY_ENTER;
    if (strcmp(name, "esc") == 0)      return LV_KEY_ESC;
    if (strcmp(name, "back") == 0)     return LV_KEY_ESC;
    if (strcmp(name, "b") == 0)        return LV_KEY_ESC;
    if (strcmp(name, "start") == 0)    return DASH_SETTINGS_PAGE;
    if (strcmp(name, "s") == 0)        return DASH_SETTINGS_PAGE;
    if (strcmp(name, "info") == 0)     return DASH_INFO_PAGE;
    if (strcmp(name, "y") == 0)        return DASH_INFO_PAGE;
    if (strcmp(name, "x") == 0)        return DASH_CONTEXT_PAGE;
    if (strcmp(name, "pageup") == 0)   return DASH_NEXT_PAGE;
    if (strcmp(name, "pagedown") == 0) return DASH_PREV_PAGE;
    if (strcmp(name, "lt") == 0)       return 'L';
    if (strcmp(name, "rt") == 0)       return 'R';
    if (strcmp(name, "q") == 0)        return 'L';
    if (strcmp(name, "e") == 0)        return 'R';
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

/* ── Status query ── */
extern parse_handle_t *parsers[];
extern int dash_scroller_get_page_current(void);
extern int dash_scroller_get_selected_index(void);
extern int dash_scroller_get_item_count(void);
extern const char *dash_scroller_get_focused_title(void);
extern bool dash_mainmenu_is_open(void);

static void handle_status(SOCKET_TYPE fd)
{
    char buf[512];
    int tab = dash_get_tab();
    int page = dash_scroller_get_page_current();
    int sel = dash_scroller_get_selected_index();
    int total = dash_scroller_get_item_count();
    const char *page_name = dash_scroller_get_title(page);
    const char *focused = dash_scroller_get_focused_title();
    bool menu = dash_mainmenu_is_open();

    snprintf(buf, sizeof(buf),
             "OK tab=%d page=%d/%s sel=%d/%d title=\"%s\" menu=%s\n",
             tab, page, page_name ? page_name : "?",
             sel, total,
             focused ? focused : "",
             menu ? "open" : "closed");
    send_str(fd, buf);
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
