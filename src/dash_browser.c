// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2022 Ryzee119

#include "lithiumx.h"
#include "dash_anim.h"

#define FOLDER_COLOR "#F2E25D"
#define FILE_COLOR "#FFFFFF"

typedef struct list_item
{
    char *item;
    uint8_t is_dir;
} list_item_t;

static void list_dir(const char *path, list_item_t *list, int *cnt);

typedef struct
{
    char *cwd;
    list_item_t *list;
    lv_obj_t *file_list_container;
    int item_cnt;
    int selected;
    browser_item_selection_cb cb;
    lv_obj_t *overlay;
    lv_obj_t *panel;
    lv_obj_t *breadcrumb_label;
    lv_obj_t **row_objs;
} dash_browser_info_t;

static void browser_update_selection(dash_browser_info_t *dinfo);
static void browser_close(dash_browser_info_t *dinfo);

/* ============================================================
 *  Row highlight
 * ============================================================ */
static void browser_update_selection(dash_browser_info_t *dinfo)
{
    for (int i = 0; i < dinfo->item_cnt; i++)
    {
        lv_obj_remove_style(dinfo->row_objs[i], &file_row_selected_style, LV_PART_MAIN);
        lv_obj_add_style(dinfo->row_objs[i], &file_row_style, LV_PART_MAIN);
    }
    if (dinfo->selected >= 0 && dinfo->selected < dinfo->item_cnt)
    {
        lv_obj_remove_style(dinfo->row_objs[dinfo->selected], &file_row_style, LV_PART_MAIN);
        lv_obj_add_style(dinfo->row_objs[dinfo->selected], &file_row_selected_style, LV_PART_MAIN);
        lv_obj_scroll_to_view(dinfo->row_objs[dinfo->selected], LV_ANIM_ON);
    }
}

/* ============================================================
 *  Close
 * ============================================================ */
static void browser_close(dash_browser_info_t *dinfo)
{
    for (int i = 0; i < dinfo->item_cnt; i++)
    {
        lv_mem_free(dinfo->list[i].item);
    }
    lv_mem_free(dinfo->list);
    lv_mem_free(dinfo->row_objs);
    lv_mem_free(dinfo->cwd);

    if (dinfo->overlay)
    {
        lv_obj_del(dinfo->overlay);
    }
    lv_mem_free(dinfo);
    dash_focus_pop_depth();
}

/* ============================================================
 *  Key handler
 * ============================================================ */
static void browser_key_handler(lv_event_t *event)
{
    dash_browser_info_t *dinfo = lv_event_get_user_data(event);
    lv_key_t key = *((lv_key_t *)lv_event_get_param(event));

    if (key == LV_KEY_ESC)
    {
        browser_close(dinfo);
        return;
    }

    if (key == LV_KEY_DOWN)
    {
        if (dinfo->selected < dinfo->item_cnt - 1) dinfo->selected++;
        browser_update_selection(dinfo);
    }
    else if (key == LV_KEY_UP)
    {
        if (dinfo->selected > 0) dinfo->selected--;
        browser_update_selection(dinfo);
    }
    else if (key == 'L' || key == 'R')
    {
        int jump = (key == 'R') ? 10 : -10;
        dinfo->selected = LV_CLAMP(0, dinfo->selected + jump, dinfo->item_cnt - 1);
        browser_update_selection(dinfo);
    }
    else if (key == LV_KEY_ENTER)
    {
        char cwd[DASH_MAX_PATH];
        strcpy(cwd, dinfo->cwd);
        if (strlen(cwd) > 0) strcat(cwd, "\\");
        strcat(cwd, dinfo->list[dinfo->selected].item);

        if (dinfo->cb(cwd)) return;

        if (dinfo->list[dinfo->selected].is_dir)
        {
            browser_item_selection_cb cb = dinfo->cb;
            browser_close(dinfo);
            dash_browser_open(cwd, cb);
        }
    }
}

/* ============================================================
 *  Open
 * ============================================================ */
void dash_browser_open(char *path, browser_item_selection_cb cb)
{
    int cnt;
    dash_browser_info_t *dinfo = lv_mem_alloc(sizeof(dash_browser_info_t));
    lv_memset(dinfo, 0, sizeof(dash_browser_info_t));

    list_dir(path, NULL, &cnt);
    if (cnt == 0)
    {
        lv_mem_free(dinfo);
        return;
    }

    dinfo->item_cnt = cnt;
    dinfo->list = lv_mem_alloc(cnt * sizeof(list_item_t));
    dinfo->row_objs = lv_mem_alloc(cnt * sizeof(lv_obj_t *));
    dinfo->cwd = lv_strdup(path);
    dinfo->cb = cb;
    dinfo->selected = 0;

    list_dir(path, dinfo->list, &cnt);

    lv_coord_t scr_w = lv_obj_get_width(lv_scr_act());
    lv_coord_t scr_h = lv_obj_get_height(lv_scr_act());

    /* Fullscreen overlay */
    dinfo->overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(dinfo->overlay, scr_w, scr_h);
    lv_obj_set_style_bg_opa(dinfo->overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(dinfo->overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dinfo->overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(dinfo->overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(dinfo->overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Scrim */
    lv_obj_t *scrim = lv_obj_create(dinfo->overlay);
    lv_obj_set_size(scrim, scr_w, scr_h);
    lv_obj_add_style(scrim, &overlay_scrim_style, LV_PART_MAIN);
    lv_obj_clear_flag(scrim, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Panel (single column) */
    dinfo->panel = lv_obj_create(dinfo->overlay);
    lv_obj_set_pos(dinfo->panel, 40, 44);
    lv_obj_set_size(dinfo->panel, scr_w - 80, scr_h - 44 - 64);
    lv_obj_add_style(dinfo->panel, &panel_style, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dinfo->panel, 26, LV_PART_MAIN);
    lv_obj_set_layout(dinfo->panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(dinfo->panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(dinfo->panel, 14, LV_PART_MAIN);

    /* Header */
    lv_obj_t *header = lv_obj_create(dinfo->panel);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header_left = lv_obj_create(header);
    lv_obj_set_size(header_left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header_left, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(header_left, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header_left, 0, LV_PART_MAIN);
    lv_obj_set_layout(header_left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header_left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(header_left, 4, LV_PART_MAIN);
    lv_obj_clear_flag(header_left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *eyebrow = lv_label_create(header_left);
    lv_obj_add_style(eyebrow, &eyebrow_style, LV_PART_MAIN);
    lv_obj_set_style_text_color(eyebrow, EF_BLUE, LV_PART_MAIN);
    lv_label_set_text(eyebrow, "BROWSER");

    lv_obj_t *title = lv_label_create(header_left);
    lv_obj_set_style_text_font(title, &lv_font_rubik_24, LV_PART_MAIN);
    lv_label_set_text(title, "Files & XBEs");

    /* Breadcrumb */
    dinfo->breadcrumb_label = lv_label_create(dinfo->panel);
    lv_obj_add_style(dinfo->breadcrumb_label, &mono_small_style, LV_PART_MAIN);
    lv_label_set_text_fmt(dinfo->breadcrumb_label, LV_SYMBOL_DIRECTORY " %s", dinfo->cwd);

    /* File list container (scrollable) */
    dinfo->file_list_container = lv_obj_create(dinfo->panel);
    lv_obj_set_size(dinfo->file_list_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(dinfo->file_list_container, 1);
    lv_obj_set_style_bg_opa(dinfo->file_list_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(dinfo->file_list_container, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(dinfo->file_list_container, EF_FG, LV_PART_MAIN);
    lv_obj_set_style_border_opa(dinfo->file_list_container, 20, LV_PART_MAIN);
    lv_obj_set_style_radius(dinfo->file_list_container, 10, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(dinfo->file_list_container, true, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dinfo->file_list_container, 0, LV_PART_MAIN);
    lv_obj_set_layout(dinfo->file_list_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(dinfo->file_list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(dinfo->file_list_container, 0, LV_PART_MAIN);

    /* Header row */
    lv_obj_t *hdr_row = lv_obj_create(dinfo->file_list_container);
    lv_obj_set_size(hdr_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_add_style(hdr_row, &file_header_row_style, LV_PART_MAIN);
    lv_obj_add_style(hdr_row, &file_row_style, LV_PART_MAIN);
    lv_obj_set_layout(hdr_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hdr_row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(hdr_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr_icon = lv_label_create(hdr_row);
    lv_obj_set_width(hdr_icon, 28);
    lv_label_set_text(hdr_icon, "");

    lv_obj_t *hdr_name = lv_label_create(hdr_row);
    lv_obj_set_flex_grow(hdr_name, 1);
    lv_label_set_text(hdr_name, "NAME");

    lv_obj_t *hdr_size = lv_label_create(hdr_row);
    lv_obj_set_width(hdr_size, 100);
    lv_label_set_text(hdr_size, "SIZE");

    /* File rows */
    for (int i = 0; i < cnt; i++)
    {
        lv_obj_t *row = lv_obj_create(dinfo->file_list_container);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_add_style(row, &file_row_style, LV_PART_MAIN);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* Icon */
        lv_obj_t *icon = lv_label_create(row);
        lv_obj_set_width(icon, 28);
        lv_label_set_text(icon, dinfo->list[i].is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);
        lv_obj_set_style_text_color(icon, EF_FG_MUTED, LV_PART_MAIN);

        /* Name */
        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_flex_grow(name, 1);
        lv_label_set_text(name, dinfo->list[i].item);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(name, &lv_font_rubik_14, LV_PART_MAIN);

        /* Size placeholder */
        lv_obj_t *size = lv_label_create(row);
        lv_obj_set_width(size, 100);
        lv_label_set_text(size, dinfo->list[i].is_dir ? "-" : "");
        lv_obj_set_style_text_color(size, EF_FG_MUTED, LV_PART_MAIN);

        dinfo->row_objs[i] = row;
    }

    /* Initial selection */
    browser_update_selection(dinfo);

    /* Footer */
    lv_obj_t *footer = lv_label_create(dinfo->panel);
    lv_obj_add_style(footer, &mono_small_style, LV_PART_MAIN);
    lv_label_set_text_fmt(footer, "%d items " LV_SYMBOL_DUMMY " sorted by name", cnt);

    /* Entry animation */
    dash_anim_overlay_in(dinfo->panel, 300);

    /* Focus management */
    lv_group_add_obj(lv_group_get_default(), dinfo->panel);
    lv_obj_add_event_cb(dinfo->panel, browser_key_handler, LV_EVENT_KEY, dinfo);
    dash_focus_change_depth(dinfo->panel);
}

/* ============================================================
 *  Directory listing (preserved from original)
 * ============================================================ */
static void list_sort(list_item_t *arr, int size)
{
    int i, j;
    list_item_t temp;

    for (i = 0; i < size - 1; i++)
    {
        for (j = 0; j < size - i - 1; j++)
        {
            char *path1 = arr[j].item;
            char *path2 = arr[j + 1].item;
            bool folder1 = arr[j].is_dir;
            bool folder2 = arr[j + 1].is_dir;
            if (!folder1 && folder2)
            {
                temp = arr[j]; arr[j] = arr[j + 1]; arr[j + 1] = temp;
            }
            else if (folder1 == folder2 && strcasecmp(path1, path2) > 0)
            {
                temp = arr[j]; arr[j] = arr[j + 1]; arr[j + 1] = temp;
            }
        }
    }
}

static void list_dir(const char *path, list_item_t *list, int *cnt)
{
    WIN32_FIND_DATA findData;
    HANDLE hFind;
    char searchPath[DASH_MAX_PATH];
    int i = 0;

    *cnt = 0;

#ifdef NXDK
    if (strcmp(path, DASH_ROOT_PATH) == 0)
    {
        static const char root_drives[][3] = {"C:", "D:", "E:", "F:", "G:", "R:", "S:",
                                              "V:", "W:", "A:", "B:", "P:", "Q:", "X:", "Y:", "Z:"};
        int _cnt = 0;
        for (int i = 0; i < DASH_ARRAY_SIZE(root_drives); i++)
        {
            if (!nxIsDriveMounted(root_drives[i][0]))
            {
                continue;
            }
            if (list != NULL)
            {
                list[_cnt].item = lv_strdup(root_drives[i]);
                list[_cnt].is_dir = 1;
            }
            _cnt++;
        }
        *cnt = _cnt;
        return;
    }
#endif

    lv_snprintf(searchPath, MAX_PATH, "%s\\*", path);
    hFind = FindFirstFileA(searchPath, &findData);

    if (hFind == INVALID_HANDLE_VALUE) return;

    do
    {
        if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0)
        {
            if (list != NULL)
            {
                list[i].item = lv_strdup(findData.cFileName);
                list[i].is_dir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
            }
            i++;
        }
    } while (FindNextFileA(hFind, &findData) != 0);

    if (list) list_sort(list, i);
    FindClose(hFind);
    *cnt = i;
}
