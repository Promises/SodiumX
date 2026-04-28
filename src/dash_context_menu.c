// SPDX-License-Identifier: MIT
// Game context menu — X button opens a menu for the selected game
// with save backup management.

#include "sodiumx.h"
#include "dash_anim.h"
#include "dash_context_menu.h"
#include "dash_backup.h"
#include "dash_panel.h"
#include "dash_pill_data.h"

/* ── Sliced pill button builder ── */
#define PILL_BTN_CAP_W   13  /* btn_h/2 = 27/2 = 13 */
#define PILL_BTN_MID_W    8  /* middle tile width */
#define PILL_BTN_H       27

typedef struct {
    const lv_img_dsc_t *left;
    const lv_img_dsc_t *mid;
    const lv_img_dsc_t *right;
} pill_btn_style_t;

static const pill_btn_style_t PILL_ACTIVE    = {&pill_btn_active_l,    &pill_btn_active_m,    &pill_btn_active_r};
static const pill_btn_style_t PILL_INACTIVE  = {&pill_btn_inactive_l,  &pill_btn_inactive_m,  &pill_btn_inactive_r};
static const pill_btn_style_t PILL_HIGHLIGHT = {&pill_btn_highlight_l, &pill_btn_highlight_m, &pill_btn_highlight_r};
static const pill_btn_style_t PILL_BUSY      = {&pill_btn_busy_l,      &pill_btn_busy_m,      &pill_btn_busy_r};

/* Max middle tiles — 20 tiles × 8px = 160px max text area */
#define PILL_MAX_MID_TILES 20
static lv_obj_t *pill_mid_imgs[PILL_MAX_MID_TILES];
static int pill_mid_count = 0;
static lv_obj_t *pill_left_img = NULL;
static lv_obj_t *pill_right_img = NULL;

static lv_obj_t *create_pill_btn(lv_obj_t *parent, const char *text,
                                  const pill_btn_style_t *style, lv_color_t text_color)
{
    /* Measure text width to determine middle tile count */
    lv_point_t txt_size;
    lv_txt_get_size(&txt_size, text, &dash_font_ui_12, 0, 0, LV_COORD_MAX, 0);
    int text_w = txt_size.x + 16; /* add horizontal padding */
    int mid_count = (text_w + PILL_BTN_MID_W - 1) / PILL_BTN_MID_W;
    if (mid_count < 1) mid_count = 1;
    if (mid_count > PILL_MAX_MID_TILES) mid_count = PILL_MAX_MID_TILES;

    int total_w = PILL_BTN_CAP_W + mid_count * PILL_BTN_MID_W + PILL_BTN_CAP_W;

    /* Container */
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_size(btn, total_w, PILL_BTN_H);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    /* Left cap */
    pill_left_img = lv_img_create(btn);
    lv_img_set_src(pill_left_img, style->left);
    lv_obj_set_pos(pill_left_img, 0, 0);

    /* Middle tiles */
    pill_mid_count = mid_count;
    for (int i = 0; i < mid_count; i++) {
        pill_mid_imgs[i] = lv_img_create(btn);
        lv_img_set_src(pill_mid_imgs[i], style->mid);
        lv_obj_set_pos(pill_mid_imgs[i], PILL_BTN_CAP_W + i * PILL_BTN_MID_W, 0);
    }

    /* Right cap */
    pill_right_img = lv_img_create(btn);
    lv_img_set_src(pill_right_img, style->right);
    lv_obj_set_pos(pill_right_img, PILL_BTN_CAP_W + mid_count * PILL_BTN_MID_W, 0);

    /* Label centered over the whole button */
    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &dash_font_ui_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, text_color, LV_PART_MAIN);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);

    return btn;
}

static void pill_btn_set_style(const pill_btn_style_t *style, lv_color_t text_color)
{
    if (pill_left_img) lv_img_set_src(pill_left_img, style->left);
    if (pill_right_img) lv_img_set_src(pill_right_img, style->right);
    for (int i = 0; i < pill_mid_count; i++) {
        if (pill_mid_imgs[i]) lv_img_set_src(pill_mid_imgs[i], style->mid);
    }
    if (pill_left_img) {
        lv_obj_t *btn = lv_obj_get_parent(pill_left_img);
        lv_obj_t *lbl = lv_obj_get_child(btn, lv_obj_get_child_cnt(btn) - 1);
        if (lbl) lv_obj_set_style_text_color(lbl, text_color, LV_PART_MAIN);
    }
}

/* Update style on any pill button container (found by iterating its children) */
static void pill_btn_restyle(lv_obj_t *btn, const pill_btn_style_t *style, lv_color_t text_color)
{
    if (!btn) return;
    int cnt = (int)lv_obj_get_child_cnt(btn);
    if (cnt < 3) return; /* need at least left + 1 mid + right + label */

    /* First child = left cap */
    lv_img_set_src(lv_obj_get_child(btn, 0), style->left);
    /* Last two children = right cap, then label */
    lv_img_set_src(lv_obj_get_child(btn, cnt - 2), style->right);
    /* Middle children = mid tiles */
    for (int i = 1; i < cnt - 2; i++) {
        lv_img_set_src(lv_obj_get_child(btn, i), style->mid);
    }
    /* Label is last child */
    lv_obj_set_style_text_color(lv_obj_get_child(btn, cnt - 1), text_color, LV_PART_MAIN);
}

/* ── State ── */
static int ctx_db_id = -1;
static char ctx_title[MAX_META_LEN];
static char ctx_title_id[16];

/* ── Backup browser state ── */
static lv_obj_t *backup_status_lbl = NULL;
static lv_obj_t *backup_detail_lbl = NULL;
static lv_timer_t *backup_poll_timer = NULL;

/* ── Restore state ── */
#define MAX_SNAPSHOTS 32

typedef struct {
    char name[32];
    int file_count;
    int total_size;
} snapshot_entry_t;

static snapshot_entry_t snapshots[MAX_SNAPSHOTS];
static int snapshot_count = 0;
static int restore_selected = 0;
static lv_obj_t *restore_items[MAX_SNAPSHOTS];
static lv_obj_t *restore_btns[MAX_SNAPSHOTS]; /* pill button containers */
static lv_obj_t *restore_status_lbl = NULL;
static volatile bool restore_list_ready = false;
static volatile bool restore_in_progress = false;
static bool restore_list_built = false;

/* ── DB callback to get title info ── */
static int ctx_db_callback(void *param, int argc, char **argv, char **azColName)
{
    (void)param;
    for (int i = 0; i < argc; i++) {
        if (strcmp(azColName[i], "title_id") == 0 && argv[i])
            strncpy(ctx_title_id, argv[i], sizeof(ctx_title_id) - 1);
        else if (strcmp(azColName[i], "title") == 0 && argv[i])
            strncpy(ctx_title, argv[i], sizeof(ctx_title) - 1);
    }
    return 0;
}

/* ══════════════════════════════════════════════════════════════════
 *  Context menu — uses shared overlay menu widget
 * ══════════════════════════════════════════════════════════════════ */
static void ctx_open_backup_browser(void *param);

static overlay_menu_item_t ctx_items[] = {
    {"Manage Save Backups", LV_SYMBOL_UPLOAD, NULL, NULL, NULL},
};

void dash_context_menu_open(int db_id)
{
    if (dash_overlay_menu_is_open()) {
        dash_overlay_menu_close();
        return;
    }

    ctx_db_id = db_id;
    ctx_title[0] = '\0';
    ctx_title_id[0] = '\0';

    char cmd[SQL_MAX_COMMAND_LEN];
    lv_snprintf(cmd, sizeof(cmd), SQL_TITLE_GET_BY_ID, db_id);
    db_command_with_callback(cmd, ctx_db_callback, NULL);
    if (!ctx_title[0]) strncpy(ctx_title, "Unknown", sizeof(ctx_title) - 1);

    /* Wire up callback (can't be static const because cb_param changes) */
    ctx_items[0].cb = ctx_open_backup_browser;

    overlay_menu_config_t config = {
        .eyebrow    = ctx_title_id[0] ? ctx_title_id : "GAME",
        .title      = ctx_title,
        .close_hint = "B " LV_SYMBOL_RIGHT " close",
        .close_key  = DASH_CONTEXT_PAGE,
        .items      = ctx_items,
        .item_count = DASH_ARRAY_SIZE(ctx_items),
    };
    dash_overlay_menu_open(&config);
}

/* ══════════════════════════════════════════════════════════════════
 *  Backup panel — uses dash_panel for two-pane layout
 * ══════════════════════════════════════════════════════════════════ */

/* ── Status section ── */
static void status_poll_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!backup_status_lbl) return;
    const char *status = dash_backup_get_status();
    lv_label_set_text_fmt(backup_status_lbl, "Last backup: %s", status ? status : "Never");
}

static void build_status_section(lv_obj_t *body)
{
    lv_obj_t *title = lv_label_create(body);
    lv_obj_set_style_text_font(title, &dash_font_ui_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, EF_FG, LV_PART_MAIN);
    lv_label_set_text(title, "Backup Status");

    lv_obj_t *sub = lv_label_create(body);
    lv_obj_add_style(sub, &body_muted_style, LV_PART_MAIN);
    lv_label_set_text_fmt(sub, "Save data for %s", ctx_title);
    lv_obj_set_style_pad_bottom(sub, 16, LV_PART_MAIN);

    lv_obj_t *r0 = lv_label_create(body);
    lv_obj_set_style_text_font(r0, &dash_font_ui_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(r0, EF_FG, LV_PART_MAIN);
    lv_label_set_text_fmt(r0, "Title ID: %s", ctx_title_id[0] ? ctx_title_id : "N/A");

    backup_status_lbl = lv_label_create(body);
    lv_obj_set_style_text_font(backup_status_lbl, &dash_font_ui_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(backup_status_lbl, EF_FG, LV_PART_MAIN);
    lv_obj_set_style_pad_top(backup_status_lbl, 8, LV_PART_MAIN);

    lv_obj_t *r2 = lv_label_create(body);
    lv_obj_set_style_text_font(r2, &dash_font_ui_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(r2, EF_FG_MUTED, LV_PART_MAIN);
    lv_obj_set_style_pad_top(r2, 8, LV_PART_MAIN);
    if (dash_settings.backup_server[0])
        lv_label_set_text_fmt(r2, "Server: %s:%d", dash_settings.backup_server, dash_settings.backup_port);
    else
        lv_label_set_text(r2, "Server: Not configured");

    status_poll_cb(NULL);
    if (backup_poll_timer) lv_timer_del(backup_poll_timer);
    backup_poll_timer = lv_timer_create(status_poll_cb, 500, NULL);
}

/* ── Force Backup section ── */
static lv_obj_t *force_btn_img = NULL;

static void force_poll_update(void)
{
    if (!force_btn_img) return;

    backup_state_t st = dash_backup_get_state();

    bool focused = dash_panel_is_open() &&
                   dash_panel_get_section() == 1 /* BK_SECT_FORCE */ &&
                   !dash_panel_is_nav_focused();

    switch (st) {
        case BACKUP_CONNECTING:
        case BACKUP_SCANNING:
        case BACKUP_TRANSFERRING:
            pill_btn_set_style(&PILL_BUSY, EF_FG);
            break;
        default:
            if (focused)
                pill_btn_set_style(&PILL_HIGHLIGHT, lv_color_hex(0x1d2021));
            else if (dash_backup_is_synced())
                pill_btn_set_style(&PILL_INACTIVE, EF_FG);
            else
                pill_btn_set_style(&PILL_ACTIVE, lv_color_hex(0x1d2021));
            break;
    }

    /* Last backup time */
    if (backup_status_lbl) {
        const char *t = dash_backup_get_last_time();
        lv_label_set_text(backup_status_lbl, t);
    }

    /* Sync indicator */
    if (backup_detail_lbl) {
        if (dash_backup_is_synced()) {
            lv_obj_set_style_text_color(backup_detail_lbl, dash_accent_color, LV_PART_MAIN);
            lv_label_set_text(backup_detail_lbl, "All saves backed up");
        } else {
            lv_obj_set_style_text_color(backup_detail_lbl, EF_FG_MUTED, LV_PART_MAIN);
            lv_label_set_text(backup_detail_lbl, "Changes pending");
        }
    }
}

static void build_loading_placeholder(lv_obj_t *body, const char *section_title)
{
    lv_obj_t *title = lv_label_create(body);
    lv_obj_set_style_text_font(title, &dash_font_ui_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, EF_FG, LV_PART_MAIN);
    lv_label_set_text(title, section_title);

    lv_obj_t *sub = lv_label_create(body);
    lv_obj_add_style(sub, &body_muted_style, LV_PART_MAIN);
    lv_obj_set_style_pad_top(sub, 16, LV_PART_MAIN);

    backup_state_t st = dash_backup_get_state();
    if (!dash_settings.backup_server[0]) {
        lv_label_set_text(sub, "No backup server configured.\nSet server address in sodiumx.toml.");
    } else if (st == BACKUP_CONNECTING) {
        lv_label_set_text(sub, "Connecting to backup server...");
    } else if (st == BACKUP_FAILED) {
        lv_label_set_text(sub, "Could not reach backup server.");
    } else {
        lv_label_set_text(sub, "Waiting for backup server...");
    }
}

static bool is_server_ready(void)
{
    if (!dash_settings.backup_server[0]) return false;
    backup_state_t st = dash_backup_get_state();
    /* Server is "ready" if we've completed at least one backup cycle */
    return (st == BACKUP_DONE || st == BACKUP_IDLE) &&
           strcmp(dash_backup_get_last_time(), "Never") != 0;
}

static void build_force_section(lv_obj_t *body)
{
    if (!is_server_ready()) {
        build_loading_placeholder(body, "Force Backup");
        return;
    }

    lv_obj_t *title = lv_label_create(body);
    lv_obj_set_style_text_font(title, &dash_font_ui_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, EF_FG, LV_PART_MAIN);
    lv_label_set_text(title, "Force Backup");

    lv_obj_t *sub = lv_label_create(body);
    lv_obj_add_style(sub, &body_muted_style, LV_PART_MAIN);
    lv_label_set_text(sub, "Manually trigger a save backup.");
    lv_obj_set_style_pad_bottom(sub, 16, LV_PART_MAIN);

    /* Setting row: label+desc on left, button on right */
    lv_obj_t *row = lv_obj_create(body);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_add_style(row, &setting_row_style, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* Left: label + description */
    lv_obj_t *left = lv_obj_create(row);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(left, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(left, 0, LV_PART_MAIN);
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, 2, LV_PART_MAIN);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(left);
    lv_obj_set_style_text_font(lbl, &dash_font_ui_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, EF_FG, LV_PART_MAIN);
    lv_label_set_text(lbl, "Backup Now");

    lv_obj_t *desc = lv_label_create(left);
    lv_obj_set_style_text_font(desc, &dash_font_ui_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(desc, EF_FG_MUTED, LV_PART_MAIN);
    lv_obj_set_width(desc, 300);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_label_set_text(desc, "Send all changed saves to the backup server.");

    /* Right: sliced pill button */
    create_pill_btn(row, "Run Backup", &PILL_ACTIVE, lv_color_hex(0x1d2021));
    /* Store refs for state updates */
    force_btn_img = pill_left_img; /* just need any ref to find the parent */

    /* Status readout below */
    lv_obj_t *status_row = lv_obj_create(body);
    lv_obj_set_size(status_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_add_style(status_row, &setting_row_style, LV_PART_MAIN);
    lv_obj_set_layout(status_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(status_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *status_label = lv_label_create(status_row);
    lv_obj_set_style_text_font(status_label, &dash_font_ui_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label, EF_FG, LV_PART_MAIN);
    lv_label_set_text(status_label, "Last Backup");

    backup_status_lbl = lv_label_create(status_row);
    lv_obj_set_style_text_font(backup_status_lbl, &dash_font_ui_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(backup_status_lbl, EF_FG_MUTED, LV_PART_MAIN);

    /* Sync status row */
    lv_obj_t *sync_row = lv_obj_create(body);
    lv_obj_set_size(sync_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_add_style(sync_row, &setting_row_style, LV_PART_MAIN);
    lv_obj_set_layout(sync_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(sync_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sync_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(sync_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sync_label = lv_label_create(sync_row);
    lv_obj_set_style_text_font(sync_label, &dash_font_ui_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(sync_label, EF_FG, LV_PART_MAIN);
    lv_label_set_text(sync_label, "Sync");

    backup_detail_lbl = lv_label_create(sync_row);
    lv_obj_set_style_text_font(backup_detail_lbl, &dash_font_ui_14, LV_PART_MAIN);

    force_poll_update();
    if (backup_poll_timer) lv_timer_del(backup_poll_timer);
    backup_poll_timer = lv_timer_create((lv_timer_cb_t)force_poll_update, 500, NULL);
}

static bool force_on_key(lv_key_t key)
{
    if (key == LV_KEY_ENTER) {
        dash_backup_start();
        force_poll_update();
        return true;
    }
    /* Update pill state immediately on any focus change */
    force_poll_update();
    return false;
}

/* ── Restore section ── */
static int fetch_snapshots_thread(void *param)
{
    (void)param;
    snapshot_count = 0;
    restore_list_ready = false;

    BK_SOCKET_TYPE fd = dash_backup_connect();
    if (fd == BK_INVALID_SOCK) { restore_list_ready = true; return 1; }

    char line[256];
    dash_backup_send_line(fd, "LIST %s", ctx_title_id);

    if (dash_backup_recv_line(fd, line, sizeof(line)) < 0 ||
        strncmp(line, "SNAPSHOTS ", 10) != 0) {
        bk_closesocket(fd);
        restore_list_ready = true;
        return 1;
    }

    int count = atoi(line + 10);
    if (count > MAX_SNAPSHOTS) count = MAX_SNAPSHOTS;

    for (int i = 0; i < count; i++) {
        if (dash_backup_recv_line(fd, line, sizeof(line)) < 0) break;
        char *tab1 = strchr(line, '\t');
        if (!tab1) continue;
        *tab1 = '\0';
        char *tab2 = strchr(tab1 + 1, '\t');
        if (!tab2) continue;
        *tab2 = '\0';
        strncpy(snapshots[snapshot_count].name, line, sizeof(snapshots[0].name) - 1);
        snapshots[snapshot_count].file_count = atoi(tab1 + 1);
        snapshots[snapshot_count].total_size = atoi(tab2 + 1);
        snapshot_count++;
    }

    bk_closesocket(fd);
    restore_list_ready = true;
    return 0;
}

static int restore_snapshot_thread(void *param)
{
    int idx = (int)(intptr_t)param;
    restore_in_progress = true;

    BK_SOCKET_TYPE fd = dash_backup_connect();
    if (fd == BK_INVALID_SOCK) { restore_in_progress = false; return 1; }

    char line[512];
    dash_backup_send_line(fd, "RESTORE %s %s", snapshots[idx].name, ctx_title_id);

    if (dash_backup_recv_line(fd, line, sizeof(line)) < 0 ||
        strncmp(line, "RESTORE_BEGIN ", 14) != 0) {
        bk_closesocket(fd);
        restore_in_progress = false;
        return 1;
    }

    int file_count = atoi(line + 14);
    dash_printf(LEVEL_TRACE, "[BACKUP] Restoring %d files from %s\n", file_count, snapshots[idx].name);

    for (int i = 0; i < file_count; i++) {
        if (dash_backup_recv_line(fd, line, sizeof(line)) < 0) break;
        if (strncmp(line, "RFILE ", 6) != 0) break;

        char *tab = strchr(line + 6, '\t');
        if (!tab) break;
        *tab = '\0';
        char *rel_path = line + 6;
        int size = atoi(tab + 1);

        char full[DASH_MAX_PATH];
        if (!dash_backup_resolve_path(rel_path, full, sizeof(full))) {
            /* Skip data for unknown path */
            for (int rem = size; rem > 0; ) {
                char skip[512];
                int chunk = rem > (int)sizeof(skip) ? (int)sizeof(skip) : rem;
                int n = bk_recv(fd, skip, chunk);
                if (n <= 0) goto done;
                rem -= n;
            }
            continue;
        }

        dash_printf(LEVEL_TRACE, "[BACKUP] Restoring %s (%d bytes)\n", full, size);
        FILE *f = fopen(full, "wb");
        if (!f) {
            for (int rem = size; rem > 0; ) {
                char skip[512];
                int chunk = rem > (int)sizeof(skip) ? (int)sizeof(skip) : rem;
                int n = bk_recv(fd, skip, chunk);
                if (n <= 0) goto done;
                rem -= n;
            }
            continue;
        }

        int received = 0;
        char chunk[4096];
        while (received < size) {
            int to_recv = size - received;
            if (to_recv > (int)sizeof(chunk)) to_recv = (int)sizeof(chunk);
            int n = bk_recv(fd, chunk, to_recv);
            if (n <= 0) { fclose(f); goto done; }
            fwrite(chunk, 1, n, f);
            received += n;
        }
        fclose(f);
    }
    dash_backup_recv_line(fd, line, sizeof(line));

done:
    bk_closesocket(fd);
    restore_in_progress = false;
    dash_printf(LEVEL_TRACE, "[BACKUP] Restore complete\n");
    return 0;
}

static void update_restore_highlight(void)
{
    bool right_active = dash_panel_is_open() && !dash_panel_is_nav_focused();

    for (int i = 0; i < snapshot_count; i++) {
        pill_btn_restyle(restore_btns[i], &PILL_INACTIVE, EF_FG);
    }

    if (restore_selected >= 0 && restore_selected < snapshot_count && right_active) {
        pill_btn_restyle(restore_btns[restore_selected], &PILL_HIGHLIGHT, lv_color_hex(0x1d2021));
    }
}

static void restore_poll_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!restore_status_lbl) return;

    if (!restore_list_ready) {
        lv_label_set_text(restore_status_lbl, "Loading snapshots...");
        return;
    }

    if (restore_in_progress) {
        lv_label_set_text(restore_status_lbl, "Restoring...");
        return;
    }

    /* Build list items once data arrives */
    if (restore_list_ready && !restore_list_built && snapshot_count > 0) {
        restore_list_built = true;
        dash_panel_rebuild_body();
        return;
    }

    if (snapshot_count == 0) {
        lv_label_set_text(restore_status_lbl,
            dash_settings.backup_server[0] ? "No backups found for this game." : "No server configured.");
        return;
    }

    lv_label_set_text_fmt(restore_status_lbl, "UP/DOWN to select, A to restore. (%d/%d)",
                          restore_selected + 1, snapshot_count);
}

static void build_restore_section(lv_obj_t *body)
{
    if (!is_server_ready()) {
        build_loading_placeholder(body, "Restore Save");
        /* Still start the poll timer so we rebuild when ready */
        if (backup_poll_timer) lv_timer_del(backup_poll_timer);
        backup_poll_timer = lv_timer_create(restore_poll_cb, 500, NULL);
        return;
    }

    lv_obj_t *title = lv_label_create(body);
    lv_obj_set_style_text_font(title, &dash_font_ui_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, EF_FG, LV_PART_MAIN);
    lv_label_set_text(title, "Restore Save");

    lv_obj_t *sub = lv_label_create(body);
    lv_obj_add_style(sub, &body_muted_style, LV_PART_MAIN);
    lv_label_set_text_fmt(sub, "Restore %s saves from a backup snapshot.", ctx_title);
    lv_obj_set_style_pad_bottom(sub, 16, LV_PART_MAIN);

    restore_status_lbl = lv_label_create(body);
    lv_obj_set_style_text_font(restore_status_lbl, &dash_font_ui_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(restore_status_lbl, EF_FG, LV_PART_MAIN);
    lv_label_set_text(restore_status_lbl, "Loading snapshots...");
    lv_obj_set_style_pad_bottom(restore_status_lbl, 12, LV_PART_MAIN);

    memset(restore_items, 0, sizeof(restore_items));
    memset(restore_btns, 0, sizeof(restore_btns));

    if (restore_list_ready && snapshot_count > 0) {
        for (int i = 0; i < snapshot_count && i < MAX_SNAPSHOTS; i++) {
            /* Setting-row style: label+desc left, pill button right */
            lv_obj_t *row = lv_obj_create(body);
            lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_add_style(row, &setting_row_style, LV_PART_MAIN);
            if (i == 0) lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
            lv_obj_set_layout(row, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            /* Left: snapshot name + details */
            lv_obj_t *left = lv_obj_create(row);
            lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(left, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(left, 0, LV_PART_MAIN);
            lv_obj_set_layout(left, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(left, 2, LV_PART_MAIN);
            lv_obj_set_flex_grow(left, 1);
            lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *name_lbl = lv_label_create(left);
            lv_obj_set_style_text_font(name_lbl, &dash_font_ui_16, LV_PART_MAIN);
            lv_obj_set_style_text_color(name_lbl, EF_FG, LV_PART_MAIN);
            lv_label_set_text(name_lbl, snapshots[i].name);

            lv_obj_t *detail_lbl = lv_label_create(left);
            lv_obj_set_style_text_font(detail_lbl, &dash_font_ui_12, LV_PART_MAIN);
            lv_obj_set_style_text_color(detail_lbl, EF_FG_MUTED, LV_PART_MAIN);
            if (snapshots[i].total_size > 1024)
                lv_label_set_text_fmt(detail_lbl, "%d files  |  %dKB",
                                      snapshots[i].file_count, snapshots[i].total_size / 1024);
            else
                lv_label_set_text_fmt(detail_lbl, "%d files  |  %dB",
                                      snapshots[i].file_count, snapshots[i].total_size);

            /* Right: sliced pill button */
            restore_btns[i] = create_pill_btn(row, "Restore", &PILL_INACTIVE, EF_FG);

            restore_items[i] = row;
        }
        restore_selected = 0;
        update_restore_highlight();
    }

    /* Poll timer for status updates + deferred list build */
    if (backup_poll_timer) lv_timer_del(backup_poll_timer);
    backup_poll_timer = lv_timer_create(restore_poll_cb, 500, NULL);

    /* Fetch snapshot list if not loaded */
    if (!restore_list_ready && dash_settings.backup_server[0] && ctx_title_id[0]) {
        restore_list_built = false;
        SDL_Thread *t = SDL_CreateThread(fetch_snapshots_thread, "bk_list", NULL);
        SDL_DetachThread(t);
    }
}

static bool restore_on_key(lv_key_t key)
{
    /* Update highlight immediately on any focus change */
    update_restore_highlight();

    if (!restore_list_ready || snapshot_count == 0) return false;

    if (key == LV_KEY_UP) {
        restore_selected = (restore_selected - 1 + snapshot_count) % snapshot_count;
        update_restore_highlight();
        if (restore_items[restore_selected])
            lv_obj_scroll_to_view(restore_items[restore_selected], LV_ANIM_ON);
        return true;
    }
    if (key == LV_KEY_DOWN) {
        restore_selected = (restore_selected + 1) % snapshot_count;
        update_restore_highlight();
        if (restore_items[restore_selected])
            lv_obj_scroll_to_view(restore_items[restore_selected], LV_ANIM_ON);
        return true;
    }
    if (key == LV_KEY_ENTER && !restore_in_progress) {
        pill_btn_restyle(restore_btns[restore_selected], &PILL_BUSY, EF_FG);
        SDL_Thread *t = SDL_CreateThread(restore_snapshot_thread, "bk_restore",
                                         (void *)(intptr_t)restore_selected);
        SDL_DetachThread(t);
        if (restore_status_lbl)
            lv_label_set_text(restore_status_lbl, "Restoring...");
        return true;
    }
    return false;
}

/* ── Panel config ── */
static const dash_panel_section_t backup_sections[] = {
    { "Backup Status", LV_SYMBOL_EYE_OPEN, build_status_section, NULL,           true,  NULL },
    { "Force Backup",  LV_SYMBOL_UPLOAD,   build_force_section,  force_on_key,   false, NULL },
    { "Restore Save",  LV_SYMBOL_DOWNLOAD, build_restore_section, restore_on_key, false, NULL },
};

static void backup_panel_on_close(void)
{
    if (backup_poll_timer) {
        lv_timer_del(backup_poll_timer);
        backup_poll_timer = NULL;
    }
    backup_status_lbl = NULL;
    backup_detail_lbl = NULL;
    restore_status_lbl = NULL;
}

static void ctx_open_backup_browser(void *param)
{
    (void)param;
    restore_list_ready = false;
    restore_list_built = false;
    snapshot_count = 0;
    restore_selected = 0;

    static dash_panel_config_t panel_cfg;
    panel_cfg.nav_title = "SAVE BACKUPS";
    panel_cfg.nav_subtitle = ctx_title;
    panel_cfg.sections = backup_sections;
    panel_cfg.section_count = 3;
    panel_cfg.on_close = backup_panel_on_close;

    dash_panel_open(&panel_cfg);
}
