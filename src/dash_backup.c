// SPDX-License-Identifier: MIT
// Save game backup client — optimised for 1-10 second transfer windows.
//
// Design:
//   1. Cached manifest loaded from disk on init (instant, no I/O scan)
//   2. Full rescan in background updates manifest and cache
//   3. Cached NEED list persisted to disk — next window skips manifest exchange
//   4. Newest-changed files first, but partial file always resumes first
//   5. Fire-and-forget streaming — no per-file ACK wait
//   6. Graceful abort on shutdown/game launch with session persistence

#include "sodiumx.h"
#include "dash_backup.h"
#include "dash_remote.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifdef NXDK
#include <lwip/netdb.h>
#include <windows.h>
#define bk_connect(s,a,l) lwip_connect(s,a,l)
#else
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#define bk_connect(s,a,l) connect(s,a,l)
#endif

/* Use shared socket macros from header */
#define SOCKET_TYPE BK_SOCKET_TYPE
#define INVALID_SOCK BK_INVALID_SOCK

/* ── File manifest entry ── */
typedef struct {
    char path[DASH_MAX_PATH];   /* prefixed path: "UDATA/titleid/file" */
    uint32_t size;
    uint32_t mtime;
} backup_file_t;

/* ── Cached manifest on disk ── */
typedef struct {
    uint32_t magic;
    int count;
} manifest_cache_header_t;

#define MANIFEST_CACHE_MAGIC 0xBAC10002

/* ── Persisted session (cached NEED list for fast resume) ── */
typedef struct {
    uint32_t magic;
    char partial_path[DASH_MAX_PATH];
    uint32_t partial_offset;
    int need_count;
} backup_session_t;

#define BACKUP_SESSION_MAGIC 0xBAC00002

/* ── Module state ── */
static volatile backup_state_t state = BACKUP_IDLE;
static volatile bool abort_requested = false;
static SDL_Thread *backup_thread = NULL;
static SDL_mutex *state_mutex = NULL;
static SDL_mutex *manifest_mutex = NULL;

static backup_file_t *manifest = NULL;
static int manifest_count = 0;
static volatile bool manifest_ready = false;
static SDL_Thread *scan_thread = NULL;
static bool backup_initialized = false;

static char status_buf[128] = "Never run";
static char last_backup_time[32] = "Never";
static bool last_backup_synced = false; /* true if server said 0 files needed */

/* ── Need list (files the server wants) ── */
static char (*need_list)[DASH_MAX_PATH] = NULL;
static int need_count = 0;
static char partial_path[DASH_MAX_PATH];
static uint32_t partial_offset = 0;

/* ── Helpers ── */
static void set_state(backup_state_t s)
{
    SDL_LockMutex(state_mutex);
    state = s;
    SDL_UnlockMutex(state_mutex);
}

static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(status_buf, sizeof(status_buf), fmt, ap);
    va_end(ap);
}

static bool should_skip_dir(const char *name)
{
    return (strcmp(name, "SodiumX") == 0);
}

/* ══════════════════════════════════════════════════════════════════
 *  Manifest cache
 * ══════════════════════════════════════════════════════════════════ */
static bool load_manifest_cache(void)
{
    FILE *f = fopen(DASH_BACKUP_MANIFEST_CACHE_PATH, "rb");
    if (!f) return false;

    manifest_cache_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 ||
        hdr.magic != MANIFEST_CACHE_MAGIC ||
        hdr.count <= 0 || hdr.count > DASH_BACKUP_MAX_FILES) {
        fclose(f);
        return false;
    }

    int loaded = (int)fread(manifest, sizeof(backup_file_t), hdr.count, f);
    fclose(f);

    if (loaded != hdr.count) return false;
    manifest_count = hdr.count;
    return true;
}

static void save_manifest_cache(void)
{
    FILE *f = fopen(DASH_BACKUP_MANIFEST_CACHE_PATH, "wb");
    if (!f) return;

    manifest_cache_header_t hdr;
    hdr.magic = MANIFEST_CACHE_MAGIC;
    hdr.count = manifest_count;

    fwrite(&hdr, sizeof(hdr), 1, f);
    fwrite(manifest, sizeof(backup_file_t), manifest_count, f);
    fclose(f);
}

/* ══════════════════════════════════════════════════════════════════
 *  File scanning — walks a directory tree, stores paths prefixed
 *  with "UDATA/" or "TDATA/" so they're unique across both roots.
 * ══════════════════════════════════════════════════════════════════ */
#ifdef NXDK
static void scan_directory(const char *prefix, const char *scan_root, const char *dir,
                           backup_file_t *files, int *count, int max)
{
    char search[DASH_MAX_PATH];
    lv_snprintf(search, sizeof(search), "%s\\*", dir);

    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (*count >= max) break;
        if (fd.cFileName[0] == '.') continue;
        if (should_skip_dir(fd.cFileName)) continue;

        char full[DASH_MAX_PATH];
        lv_snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_directory(prefix, scan_root, full, files, count, max);
        } else {
            /* rel = path relative to scan_root */
            const char *rel = full + strlen(scan_root);
            if (*rel == '\\' || *rel == '/') rel++;

            /* Store as "PREFIX/rel" e.g. "UDATA/4541000e/save.dat" */
            lv_snprintf(files[*count].path, DASH_MAX_PATH, "%s/%s", prefix, rel);
            files[*count].size = fd.nFileSizeLow;
            uint64_t ft = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) |
                           fd.ftLastWriteTime.dwLowDateTime;
            files[*count].mtime = (uint32_t)((ft / 10000000ULL) - 11644473600ULL);
            (*count)++;
        }
    } while (FindNextFile(h, &fd));
    FindClose(h);
}
#else
static void scan_directory(const char *prefix, const char *scan_root, const char *dir,
                           backup_file_t *files, int *count, int max)
{
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (*count >= max) break;
        if (ent->d_name[0] == '.') continue;
        if (should_skip_dir(ent->d_name)) continue;

        char full[DASH_MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_directory(prefix, scan_root, full, files, count, max);
        } else {
            const char *rel = full + strlen(scan_root);
            if (*rel == '/') rel++;

            snprintf(files[*count].path, DASH_MAX_PATH, "%s/%s", prefix, rel);
            files[*count].size = (uint32_t)st.st_size;
            files[*count].mtime = (uint32_t)st.st_mtime;
            (*count)++;
        }
    }
    closedir(d);
}
#endif

static int scan_thread_fn(void *param)
{
    (void)param;

    SDL_LockMutex(manifest_mutex);
    manifest_count = 0;
    scan_directory(DASH_BACKUP_UDATA_PREFIX, DASH_BACKUP_UDATA_PATH,
                   DASH_BACKUP_UDATA_PATH, manifest, &manifest_count, DASH_BACKUP_MAX_FILES);
    scan_directory(DASH_BACKUP_TDATA_PREFIX, DASH_BACKUP_TDATA_PATH,
                   DASH_BACKUP_TDATA_PATH, manifest, &manifest_count, DASH_BACKUP_MAX_FILES);
    SDL_UnlockMutex(manifest_mutex);

    save_manifest_cache();
    manifest_ready = true;
    dash_printf(LEVEL_TRACE, "[BACKUP] Scan complete: %d files\n", manifest_count);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════
 *  Session persistence (cached NEED list)
 * ══════════════════════════════════════════════════════════════════ */
static void save_session(const char *p_path, uint32_t p_offset,
                          char (*needs)[DASH_MAX_PATH], int n_count)
{
    FILE *f = fopen(DASH_BACKUP_SESSION_PATH, "wb");
    if (!f) return;

    backup_session_t hdr;
    hdr.magic = BACKUP_SESSION_MAGIC;
    strncpy(hdr.partial_path, p_path ? p_path : "", DASH_MAX_PATH - 1);
    hdr.partial_path[DASH_MAX_PATH - 1] = '\0';
    hdr.partial_offset = p_offset;
    hdr.need_count = n_count;

    fwrite(&hdr, sizeof(hdr), 1, f);
    for (int i = 0; i < n_count; i++) {
        fwrite(needs[i], DASH_MAX_PATH, 1, f);
    }
    fclose(f);
}

static bool load_session(void)
{
    FILE *f = fopen(DASH_BACKUP_SESSION_PATH, "rb");
    if (!f) return false;

    backup_session_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 || hdr.magic != BACKUP_SESSION_MAGIC) {
        fclose(f);
        return false;
    }

    strncpy(partial_path, hdr.partial_path, DASH_MAX_PATH - 1);
    partial_path[DASH_MAX_PATH - 1] = '\0';
    partial_offset = hdr.partial_offset;
    need_count = hdr.need_count;

    if (need_count > DASH_BACKUP_MAX_FILES) need_count = DASH_BACKUP_MAX_FILES;

    if (need_list) free(need_list);
    need_list = malloc(need_count * DASH_MAX_PATH);
    if (!need_list) { fclose(f); return false; }

    for (int i = 0; i < need_count; i++) {
        if (fread(need_list[i], DASH_MAX_PATH, 1, f) != 1) {
            need_count = i;
            break;
        }
    }
    fclose(f);
    return need_count > 0;
}

static void clear_session(void)
{
    remove(DASH_BACKUP_SESSION_PATH);
    partial_path[0] = '\0';
    partial_offset = 0;
    need_count = 0;
}

/* ══════════════════════════════════════════════════════════════════
 *  Network helpers
 * ══════════════════════════════════════════════════════════════════ */
static SOCKET_TYPE connect_to_server(void)
{
    if (!dash_settings.backup_server[0]) return INVALID_SOCK;

    SOCKET_TYPE fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCK) return INVALID_SOCK;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(dash_settings.backup_port ? dash_settings.backup_port : DASH_BACKUP_PORT_DEFAULT);
    addr.sin_addr.s_addr = inet_addr(dash_settings.backup_server);

    if (bk_connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        bk_closesocket(fd);
        return INVALID_SOCK;
    }
    return fd;
}

static int recv_line(SOCKET_TYPE fd, char *buf, int max)
{
    int pos = 0;
    while (pos < max - 1) {
        char c;
        int n = bk_recv(fd, &c, 1);
        if (n <= 0) return -1;
        if (c == '\n') break;
        buf[pos++] = c;
    }
    buf[pos] = '\0';
    return pos;
}

static void send_line(SOCKET_TYPE fd, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[len] = '\n';
    bk_send(fd, buf, len + 1);
}

/* ══════════════════════════════════════════════════════════════════
 *  File transfer — resolves prefixed path back to full Xbox path
 * ══════════════════════════════════════════════════════════════════ */
static void normalize_separators(char *path)
{
#ifdef NXDK
    for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
#else
    for (char *p = path; *p; p++) { if (*p == '\\') *p = '/'; }
#endif
}

static bool resolve_full_path(const char *rel_path, char *out, int out_size)
{
    /* rel_path is "UDATA/titleid/file" or "TDATA/titleid/file" */
    if (strncmp(rel_path, DASH_BACKUP_UDATA_PREFIX "/", strlen(DASH_BACKUP_UDATA_PREFIX) + 1) == 0) {
        const char *sub = rel_path + strlen(DASH_BACKUP_UDATA_PREFIX) + 1;
#ifdef NXDK
        lv_snprintf(out, out_size, "%s\\%s", DASH_BACKUP_UDATA_PATH, sub);
#else
        snprintf(out, out_size, "%s/%s", DASH_BACKUP_UDATA_PATH, sub);
#endif
        normalize_separators(out);
        return true;
    }
    if (strncmp(rel_path, DASH_BACKUP_TDATA_PREFIX "/", strlen(DASH_BACKUP_TDATA_PREFIX) + 1) == 0) {
        const char *sub = rel_path + strlen(DASH_BACKUP_TDATA_PREFIX) + 1;
#ifdef NXDK
        lv_snprintf(out, out_size, "%s\\%s", DASH_BACKUP_TDATA_PATH, sub);
#else
        snprintf(out, out_size, "%s/%s", DASH_BACKUP_TDATA_PATH, sub);
#endif
        normalize_separators(out);
        return true;
    }
    return false;
}

static bool send_file(SOCKET_TYPE fd, const char *rel_path, uint32_t offset)
{
    char full[DASH_MAX_PATH];
    if (!resolve_full_path(rel_path, full, sizeof(full))) {
        dash_printf(LEVEL_WARN, "[BACKUP] Cannot resolve path: %s\n", rel_path);
        return true; /* skip, not fatal */
    }

    FILE *f = fopen(full, "rb");
    if (!f) {
        dash_printf(LEVEL_WARN, "[BACKUP] Cannot open %s, skipping\n", full);
        send_line(fd, "FILE %s\t0\t0", rel_path);
        return true;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);

    if ((long)offset > fsize) offset = 0;
    fseek(f, offset, SEEK_SET);

    uint32_t remaining = (uint32_t)(fsize - offset);
    send_line(fd, "FILE %s\t%u\t%u", rel_path, remaining, offset);

    char chunk[DASH_BACKUP_CHUNK_SIZE];
    uint32_t sent = 0;
    while (sent < remaining && !abort_requested) {
        uint32_t to_read = remaining - sent;
        if (to_read > DASH_BACKUP_CHUNK_SIZE) to_read = DASH_BACKUP_CHUNK_SIZE;

        size_t n = fread(chunk, 1, to_read, f);
        if (n == 0) break;

        int s = bk_send(fd, chunk, (int)n);
        if (s <= 0) { fclose(f); return false; }
        sent += (uint32_t)s;
    }

    fclose(f);
    return !abort_requested;
}

/* ══════════════════════════════════════════════════════════════════
 *  Main backup thread
 * ══════════════════════════════════════════════════════════════════ */
static int backup_thread_fn(void *param)
{
    (void)param;
    set_state(BACKUP_CONNECTING);
    set_status("Connecting...");

    /* Retry connection — network stack may not be ready on early boot */
    SOCKET_TYPE fd = INVALID_SOCK;
    for (int attempt = 0; attempt < 10 && !abort_requested; attempt++) {
        dash_printf(LEVEL_TRACE, "[BACKUP] Connect attempt %d to %s:%d\n",
                    attempt + 1, dash_settings.backup_server, dash_settings.backup_port);
        fd = connect_to_server();
        if (fd != INVALID_SOCK) break;
        SDL_Delay(2000);
    }
    if (fd == INVALID_SOCK) {
        dash_printf(LEVEL_WARN, "[BACKUP] Failed to connect after retries\n");
        set_status("Failed: server unreachable");
        set_state(BACKUP_FAILED);
        return 1;
    }
    dash_printf(LEVEL_TRACE, "[BACKUP] Connected\n");

    char line[512];
    int total = 0;
    int transferred = 0;
    bool have_cached_session = load_session();

    if (have_cached_session) {
        send_line(fd, "RESUME %s %u", partial_path, partial_offset);
        if (recv_line(fd, line, sizeof(line)) < 0) goto fail;

        if (strncmp(line, "GO ", 3) == 0) {
            partial_offset = (uint32_t)strtoul(line + 3, NULL, 10);
        } else if (strcmp(line, "NEW") == 0) {
            have_cached_session = false;
            clear_session();
        } else {
            goto fail;
        }
    }

    if (!have_cached_session) {
        /* Wait for manifest scan to finish */
        set_state(BACKUP_SCANNING);
        set_status("Scanning saves...");
        while (!manifest_ready && !abort_requested) {
            SDL_Delay(50);
        }
        if (abort_requested) goto abort;

        /* Send manifest */
        SDL_LockMutex(manifest_mutex);
        dash_printf(LEVEL_TRACE, "[BACKUP] Sending manifest: %d files\n", manifest_count);
        send_line(fd, "MANIFEST %d", manifest_count);
        for (int i = 0; i < manifest_count && !abort_requested; i++) {
            send_line(fd, "%s\t%u\t%u",
                      manifest[i].path, manifest[i].size, manifest[i].mtime);
        }
        SDL_UnlockMutex(manifest_mutex);
        if (abort_requested) goto abort;

        /* Receive NEED list */
        if (recv_line(fd, line, sizeof(line)) < 0) goto fail;
        if (strncmp(line, "NEED ", 5) != 0) goto fail;

        need_count = atoi(line + 5);
        dash_printf(LEVEL_TRACE, "[BACKUP] Server needs %d files\n", need_count);
        if (need_count <= 0) {
            send_line(fd, "DONE");
            platform_get_iso8601_time(last_backup_time);
            last_backup_synced = true;
            set_status("Up to date");
            set_state(BACKUP_DONE);
            bk_closesocket(fd);
            clear_session();
            return 0;
        }
        if (need_count > DASH_BACKUP_MAX_FILES) need_count = DASH_BACKUP_MAX_FILES;

        if (need_list) free(need_list);
        need_list = malloc(need_count * DASH_MAX_PATH);
        if (!need_list) goto fail;

        for (int i = 0; i < need_count && !abort_requested; i++) {
            if (recv_line(fd, need_list[i], DASH_MAX_PATH) < 0) goto fail;
        }
        if (abort_requested) goto abort;

        partial_path[0] = '\0';
        partial_offset = 0;
    }

    /* ── Transfer files ── */
    set_state(BACKUP_TRANSFERRING);
    total = need_count;
    transferred = 0;

    for (int i = 0; i < need_count && !abort_requested; i++) {
        uint32_t offset = 0;

        if (partial_path[0] && strcmp(need_list[i], partial_path) == 0) {
            offset = partial_offset;
        }

        set_status("Backing up %d/%d", transferred + 1, total);
        dash_printf(LEVEL_TRACE, "[BACKUP] Sending %s (offset=%u)\n", need_list[i], offset);

        if (!send_file(fd, need_list[i], offset)) {
            dash_printf(LEVEL_WARN, "[BACKUP] Failed sending %s\n", need_list[i]);
            save_session(need_list[i], offset, &need_list[i], need_count - i);
            goto fail;
        }
        transferred++;

        if (partial_path[0]) {
            partial_path[0] = '\0';
            partial_offset = 0;
        }
    }

    if (abort_requested) goto abort;

    /* All files sent */
    send_line(fd, "DONE");
    platform_get_iso8601_time(last_backup_time);
    last_backup_synced = true;
    if (recv_line(fd, line, sizeof(line)) >= 0 && strncmp(line, "OK ", 3) == 0) {
        set_status("Done: %d files backed up", transferred);
    } else {
        set_status("Done: %d files (no server confirm)", transferred);
    }

    set_state(BACKUP_DONE);
    bk_closesocket(fd);
    clear_session();
    return 0;

abort:
    send_line(fd, "ABORT");
    if (need_list && transferred < need_count) {
        save_session(need_list[transferred], 0,
                     &need_list[transferred], need_count - transferred);
    }
    set_status("Interrupted: %d/%d done", transferred, total);
    set_state(BACKUP_IDLE);
    bk_closesocket(fd);
    return 0;

fail:
    set_status("Failed: connection lost");
    set_state(BACKUP_FAILED);
    if (fd != INVALID_SOCK) bk_closesocket(fd);
    return 1;
}

/* ══════════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════════ */
void dash_backup_init(void)
{
    if (backup_initialized) return;
    backup_initialized = true;

    state_mutex = SDL_CreateMutex();
    manifest_mutex = SDL_CreateMutex();
    manifest = malloc(sizeof(backup_file_t) * DASH_BACKUP_MAX_FILES);
    if (!manifest) return;

    /* Try loading cached manifest — makes manifest_ready instantly */
    if (load_manifest_cache()) {
        manifest_ready = true;
        dash_printf(LEVEL_TRACE, "[BACKUP] Loaded cached manifest: %d files\n", manifest_count);
    }

    /* Kick off full scan in background (updates cache when done) */
    scan_thread = SDL_CreateThread(scan_thread_fn, "backup_scan", NULL);
}

void dash_backup_start(void)
{
    dash_printf(LEVEL_TRACE, "[BACKUP] dash_backup_start() server='%s'\n",
                dash_settings.backup_server);
    if (!dash_settings.backup_server[0]) {
        set_status("No server configured");
        return;
    }

    backup_state_t cur = dash_backup_get_state();
    if (cur == BACKUP_CONNECTING || cur == BACKUP_TRANSFERRING || cur == BACKUP_SCANNING) {
        return;
    }

    abort_requested = false;

    if (!manifest_ready && !scan_thread) {
        scan_thread = SDL_CreateThread(scan_thread_fn, "backup_scan", NULL);
    }

    backup_thread = SDL_CreateThread(backup_thread_fn, "backup_xfer", NULL);
    SDL_DetachThread(backup_thread);
}

void dash_backup_abort(void)
{
    abort_requested = true;
}

void dash_backup_deinit(void)
{
    abort_requested = true;
    if (scan_thread) {
        SDL_WaitThread(scan_thread, NULL);
        scan_thread = NULL;
    }
    if (manifest) { free(manifest); manifest = NULL; }
    if (need_list) { free(need_list); need_list = NULL; }
    if (manifest_mutex) { SDL_DestroyMutex(manifest_mutex); manifest_mutex = NULL; }
    if (state_mutex) { SDL_DestroyMutex(state_mutex); state_mutex = NULL; }
}

const char *dash_backup_get_status(void)
{
    return status_buf;
}

backup_state_t dash_backup_get_state(void)
{
    SDL_LockMutex(state_mutex);
    backup_state_t s = state;
    SDL_UnlockMutex(state_mutex);
    return s;
}

const char *dash_backup_get_last_time(void)
{
    return last_backup_time;
}

bool dash_backup_is_synced(void)
{
    return last_backup_synced;
}

/* ── Shared helpers for context menu ── */
BK_SOCKET_TYPE dash_backup_connect(void) { return connect_to_server(); }
int dash_backup_recv_line(BK_SOCKET_TYPE fd, char *buf, int max) { return recv_line(fd, buf, max); }

void dash_backup_send_line(BK_SOCKET_TYPE fd, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[len] = '\n';
    bk_send(fd, buf, len + 1);
}

bool dash_backup_resolve_path(const char *rel_path, char *out, int out_size)
{
    return resolve_full_path(rel_path, out, out_size);
}
