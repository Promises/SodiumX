// SPDX-License-Identifier: MIT
// Save game backup client for SodiumX
//
// Backs up E:\UDATA and E:\TDATA to a remote server via a custom TCP
// protocol optimised for very short windows (1-10 seconds).
//
// The companion server is tools/backup_server.py.
//
// Protocol (binary, over TCP port 9877 by default):
//
//   Session negotiation:
//     → RESUME <session_id> <partial_path> <offset>\n
//     ← GO <offset>\n                       (resume partial file)
//     ← NEW\n                               (session expired, send manifest)
//
//   Manifest exchange:
//     → MANIFEST <count>\n
//     → <path>\t<size>\t<mtime>\n  ...      (one per file)
//     ← NEED <count>\n
//     ← <path>\n  ...                       (sorted: partial first, then newest mtime)
//
//   File transfer (streamed, no per-file ACK wait):
//     → FILE <path>\t<size>\t<offset>\n
//     → <raw bytes>
//     (server sends nothing — Xbox streams files back-to-back)
//
//   Completion:
//     → DONE\n
//     ← OK <snapshot_name>\n
//
//   Abort (clean shutdown):
//     → ABORT\n

#ifndef _DASH_BACKUP_H
#define _DASH_BACKUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define DASH_BACKUP_PORT_DEFAULT 9877

#ifdef NXDK
#define DASH_BACKUP_UDATA_PATH "E:\\UDATA"
#define DASH_BACKUP_TDATA_PATH "E:\\TDATA"
#define DASH_BACKUP_UDATA_PREFIX "UDATA"
#define DASH_BACKUP_TDATA_PREFIX "TDATA"
#define DASH_BACKUP_SESSION_PATH "E:\\UDATA\\SodiumX\\backup_session"
#define DASH_BACKUP_MANIFEST_CACHE_PATH "E:\\UDATA\\SodiumX\\backup_manifest"
#else
#define DASH_BACKUP_UDATA_PATH "test_udata"
#define DASH_BACKUP_TDATA_PATH "test_tdata"
#define DASH_BACKUP_UDATA_PREFIX "UDATA"
#define DASH_BACKUP_TDATA_PREFIX "TDATA"
#define DASH_BACKUP_SESSION_PATH "backup_session"
#define DASH_BACKUP_MANIFEST_CACHE_PATH "backup_manifest"
#endif

#define DASH_BACKUP_MAX_FILES     4096
#define DASH_BACKUP_CONNECT_TIMEOUT_MS 3000
#define DASH_BACKUP_CHUNK_SIZE   4096

typedef enum {
    BACKUP_IDLE = 0,
    BACKUP_SCANNING,
    BACKUP_CONNECTING,
    BACKUP_TRANSFERRING,
    BACKUP_DONE,
    BACKUP_FAILED,
} backup_state_t;

/* Initialise the backup subsystem (call once at startup).
 * Starts background manifest scanner if server is configured. */
void dash_backup_init(void);

/* Trigger a backup now (non-blocking — runs in background thread). */
void dash_backup_start(void);

/* Request graceful abort of any in-progress backup. */
void dash_backup_abort(void);

/* Shutdown the backup subsystem. */
void dash_backup_deinit(void);

/* Human-readable status string for the settings UI. */
const char *dash_backup_get_status(void);

/* Current state (for lifecycle hooks to check). */
backup_state_t dash_backup_get_state(void);

/* Last backup timestamp (ISO 8601) and sync state. */
const char *dash_backup_get_last_time(void);
bool dash_backup_is_synced(void);

/* ── Shared network helpers (used by context menu for LIST/RESTORE) ── */

#ifdef NXDK
#include <lwip/sockets.h>
#define BK_SOCKET_TYPE int
#define BK_INVALID_SOCK -1
#define bk_closesocket(s) lwip_close(s)
#define bk_send(s,b,l) lwip_send(s,b,l,0)
#define bk_recv(s,b,l) lwip_recv(s,b,l,0)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define BK_SOCKET_TYPE int
#define BK_INVALID_SOCK -1
#define bk_closesocket(s) close(s)
#define bk_send(s,b,l) send(s,b,l,0)
#define bk_recv(s,b,l) recv(s,b,l,0)
#endif

BK_SOCKET_TYPE dash_backup_connect(void);
int dash_backup_recv_line(BK_SOCKET_TYPE fd, char *buf, int max);
void dash_backup_send_line(BK_SOCKET_TYPE fd, const char *fmt, ...);
bool dash_backup_resolve_path(const char *rel_path, char *out, int out_size);

#ifdef __cplusplus
}
#endif

#endif
