// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2022 Ryzee119

#include "lithiumx.h"
#include "dash_prerender.h"
#include "dash_pill_data.h"

// Globals
toml_table_t *dash_search_paths;
dash_settings_t dash_settings;
char dash_launch_path[DASH_MAX_PATH];

static lv_obj_t *focus_stack[32];
static lv_indev_t *indev;
static lv_group_t *input_group;
static int focus_stack_index;
static char *toml_default = ""
                            "# Use keyword Recent to create that page that stores recently launched titles.\n"
                            "# All paths should use a forward slash \"/\" Do not use \"\\\".\n"
                            "# On a syntax error, it will reset back to default\n"
                            "# Page names must be unique\n"
                            "[[pages]]\n"
                            "name = \"Recent\"\n"
                            "\n"
                            "[[pages]]\n"
                            "name = \"Games\"\n"
                            "paths = [\"E:/Games\", \"F:/Games\", \"G:/Games\"]\n"
                            "\n"
                            "[[pages]]\n"
                            "name = \"Applications\"\n"
                            "paths = [\"E:/Applications\", \"F:/Applications\", \"G:/Applications\",\n"
                            "         \"E:/Apps\", \"F:/Apps\", \"G:/Apps\"]\n"
                            "\n"
                            "[[pages]]\n"
                            "name = \"Homebrew\"\n"
                            "paths = [\"E:/Homebrew\", \"F:/Homebrew\", \"G:/Homebrew\"]\n"
                            "\0";

static bool check_path_toml(char *err_msg, int err_msg_len)
{
    const char *search_path = DASH_SEARCH_PATH_CONFIG;
    bool parse_ok = false;
    char errbuf[256];
    toml_array_t *array;

    FILE *fp = fopen(search_path, "r");
    if (fp != NULL)
    {
        dash_search_paths = toml_parse_file(fp, errbuf, sizeof(errbuf));
        fclose(fp);
        if (dash_search_paths != NULL)
        {
            array = toml_array_in(dash_search_paths, "pages");
            if (array)
            {
                parse_ok = true;
            }
            else
            {
                lv_snprintf(err_msg, err_msg_len,
                    "No \"pages\" entry in %s file. Resetting config to default\n", search_path);
            }
        }
        else
        {
            lv_snprintf(err_msg, err_msg_len,
                "Cannot parse %s, Probably a syntax error. Resetting config to default\n", search_path);
        }
    }

    // Could not find or parse a toml path file. Fallback to default and attempt to write it
    if (parse_ok == false)
    {
        dash_search_paths = toml_parse(toml_default, errbuf, sizeof(errbuf));
        assert(dash_search_paths != NULL);
        fp = fopen(search_path, "wb");
        if (fp)
        {
            fwrite(toml_default, strlen(toml_default), 1, fp);
            fclose(fp);
        }
    }
    return true;
}

void dash_focus_set_final(lv_obj_t *focus)
{
    // Make sure the item is in our input group
    lv_group_add_obj(lv_group_get_default(), focus);
    focus_stack[0] = focus;

}

void dash_focus_change_depth(lv_obj_t *new_focus)
{
    // Make sure the item is in our input group
    lv_group_add_obj(lv_group_get_default(), new_focus);

    // Push the currently focused into onto the stack
    focus_stack[++focus_stack_index] = lv_group_get_focused(lv_group_get_default());

    // Change focus to our new item
    dash_focus_change(new_focus);
}

lv_obj_t *dash_focus_pop_depth()
{
    // Get the last item focus item until we get a valid one then jump to it
    assert(focus_stack_index > 0);
    lv_obj_t *pop = focus_stack[focus_stack_index--];
    while (lv_obj_is_valid(pop) == false)
    {
        assert(focus_stack_index >= 0);
        pop = focus_stack[focus_stack_index--];
    }
    dash_focus_change(pop);
    focus_stack_index = LV_MAX(focus_stack_index, 0);
    return pop;
}

void dash_focus_change(lv_obj_t *new_obj)
{
    lv_group_focus_freeze(lv_group_get_default(), false);
    lv_group_focus_obj(new_obj);
    lv_group_focus_freeze(lv_group_get_default(), true);
}

static void create_warning_box(const char *str)
{
    lv_obj_t *window = container_open();
    lv_obj_t *label = lv_label_create(window);
    lv_obj_set_size(label, lv_obj_get_width(window), LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text_fmt(label, "Warning: %s\n", str);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
}

static int db_rebuild_thread_f(void *param)
{
    int *complete = param;
    db_rebuild(dash_search_paths);
    *complete = 1;
    lvgl_getlock();
    lv_obj_clean(lv_scr_act());
    dash_create();
    lvgl_removelock();
    return 0;
}

static int db_rebuild_progress_thread_f(void *param)
{
    lv_obj_t *label = param;
    int *complete = label->user_data;
    while (1)
    {
        if (*complete)
        {
            lv_mem_free(complete);
            break;
        }
        extern int db_rebuild_scanned_items;
        lvgl_getlock();
        lv_label_set_text_fmt(label, "Rebuilding Database, please wait... %d", db_rebuild_scanned_items);
        lvgl_removelock();
        SDL_Delay(100);
    }
    return 0;
}

static char err_msg_toml[256], err_msg_db[256];
static bool in_memory_warning;
void dash_init(void)
{
    err_msg_toml[0] = '\0';
    err_msg_db[0] = '\0';

    focus_stack_index = 0;
    lv_memset(focus_stack, 0, sizeof(focus_stack));

    // Set default settings
    lv_memset(&dash_settings, 0, sizeof(dash_settings));
    dash_settings.magic = DASH_SETTINGS_MAGIC;
    dash_settings.max_recent_items = 15;
    dash_settings.theme_colour = (22 << 16) | (111 << 8) | (15 << 0);
    dash_settings.items_per_row = (lv_obj_get_width(lv_scr_act()) == 640) ? 4 : 6;
    /* v3 defaults */
    dash_settings.accent_index = ACCENT_GREEN;
    dash_settings.show_fps_overlay = true;
    dash_settings.show_controller_hints = true;
    dash_settings.show_clock_chip = true;
    dash_settings.show_network_chip = true;
    dash_settings.show_temp_chip = true;
    dash_settings.animated_background = true;
    dash_settings.backdrop_blur = true;
    dash_settings.tile_parallax = true;
    dash_settings.film_grain = true;
    dash_settings.resolution_mode = 1;
    dash_settings.ui_sounds = true;
    dash_settings.ftp_enabled = true;

    // Read in the toml file that has all the search paths
    check_path_toml(err_msg_toml, sizeof(err_msg_toml));

    // Setup input devices and a default input group
    input_group = lv_group_create();
    lv_group_set_default(input_group);
    indev = NULL;
    for (;;)
    {
        indev = lv_indev_get_next(indev);
        if (!indev)
        {
            break;
        }
        lv_indev_set_group(indev, input_group);
    }

    // Open up the database. If the database doesnt exist it was created
    // It it couldnt be created on disk, it is created in RAM which is not persistent so
    // will cause a warning.
    in_memory_warning = !db_open();

    // Check that the database is valid (Correct tables, and columns). Otherwise begin a database rebuild
    if (db_init(err_msg_db, sizeof(err_msg_db)) == true)
    {
        dash_create();
    }
    else
    {
        lv_obj_t *window = lv_obj_create(lv_scr_act());
        lv_obj_set_size(window, lv_obj_get_width(lv_scr_act()), lv_obj_get_height(lv_scr_act()));
        lv_obj_set_style_bg_color(window, lv_color_make(0,0,0), LV_PART_MAIN);
        lv_obj_t *label = lv_label_create(window);
        lv_obj_center(label);
        lv_label_set_text_static(label, "Rebuilding Database, please wait...");
        lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
        int *complete = lv_mem_alloc(sizeof (int));
        *complete = 0;
        label->user_data = complete;
        SDL_CreateThread(db_rebuild_progress_thread_f, "db_rebuild_progress_thread_f", label);
        SDL_CreateThread(db_rebuild_thread_f, "db_rebuild_thread_f", complete);
    }
    return;
}


/* ── Pre-rendered pill endcaps ── */
#define PILL_PAD_V    8   /* pad_top and pad_bottom */
#define PILL_BORDER   1

/* Set to true to use old GPU-rendered pills instead of pre-rendered textures */
#define USE_OLD_STYLE_PILLS 0

/* Computed at init from font metrics */
static int pill_height;
static int pill_radius;

typedef enum { PILL_ACTIVE = 0, PILL_FOCUSED, PILL_INACTIVE, PILL_STATE_COUNT } pill_state_t;

static prerender_pill_t tab_pills[PILL_STATE_COUNT];

/* ── Tab bar state ── */
static lv_obj_t *tab_bar;
static lv_obj_t *tab_objs[5];
/* Sub-objects for 3-slice pills */
static lv_obj_t *tab_left_caps[5];
static lv_obj_t *tab_middles[5];    /* canvas for middle section */
static lv_obj_t *tab_right_caps[5];
static lv_obj_t *tab_labels[5];
static void *tab_mid_mem[5];        /* backing buffers for middle canvases */
static lv_coord_t tab_mid_w[5];     /* middle widths */
static int current_tab = 0;
static const char *tab_names[] = {"Recent", "Games", "Apps", "Files", "System"};
#define TAB_COUNT 5

/* Tab navigation mode state */
static bool tab_nav_active = false;
static int tab_nav_origin_page = -1;
static int tab_nav_origin_tab = 0;
static lv_obj_t *tab_nav_focus_obj; /* invisible focusable for tab key events */

/* Hero strip + meta row labels (updated by scroller on focus change) */
static lv_obj_t *hero_eyebrow;
static lv_obj_t *hero_counter;
static lv_obj_t *meta_title_label;
static lv_obj_t *meta_sub_label;
static lv_obj_t *meta_pills;
static lv_obj_t *meta_launches_pill;
static lv_obj_t *meta_launches_label;

static void tab_bar_update(void);

static void tab_bar_create_old_style(lv_obj_t *bar)
{
    for (int i = 0; i < TAB_COUNT; i++)
    {
        lv_obj_t *pill = lv_obj_create(bar);
        lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(pill, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(pill, 8, LV_PART_MAIN);

        lv_obj_t *lbl = lv_label_create(pill);
        lv_label_set_text(lbl, tab_names[i]);

        tab_objs[i] = pill;
        tab_labels[i] = lbl;
        tab_left_caps[i] = NULL;
        tab_middles[i] = NULL;
        tab_right_caps[i] = NULL;
    }
}

static void tab_bar_create_3slice(lv_obj_t *bar)
{
    /* Measure real pill height from a temp old-style pill */
    {
        lv_obj_t *tmp = lv_obj_create(bar);
        lv_obj_set_size(tmp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_add_style(tmp, &tab_active_style, LV_PART_MAIN);
        lv_obj_t *tl = lv_label_create(tmp);
        lv_label_set_text(tl, "X");
        lv_obj_update_layout(tmp);
        pill_height = lv_obj_get_height(tmp);
        pill_radius = pill_height / 2;
        lv_obj_del(tmp);
    }

    /* Pre-render pills for all 3 states */
    dash_prerender_pill(&tab_pills[PILL_ACTIVE], pill_height, PILL_BORDER,
                        dash_accent_color, 31, dash_accent_color, 77);
    dash_prerender_pill(&tab_pills[PILL_FOCUSED], pill_height, PILL_BORDER,
                        dash_accent_color, 51, dash_accent_color, 128);
    dash_prerender_pill(&tab_pills[PILL_INACTIVE], pill_height, 0,
                        lv_color_black(), 0, lv_color_black(), 0);

    for (int i = 0; i < TAB_COUNT; i++)
    {
        lv_obj_t *pill = lv_obj_create(bar);
        lv_obj_set_size(pill, LV_SIZE_CONTENT, pill_height);
        lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(pill, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(pill, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(pill, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(pill, 0, LV_PART_MAIN);
        lv_obj_set_layout(pill, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(pill, 0, LV_PART_MAIN);

        lv_obj_t *left_cap = lv_img_create(pill);
        lv_img_set_src(left_cap, &tab_pills[PILL_ACTIVE].left);

        /* Measure text to compute middle width */
        lv_point_t txt_size;
        lv_txt_get_size(&txt_size, tab_names[i], &lv_font_rubik_14, 0, 0, LV_COORD_MAX, 0);
        lv_coord_t mid_w = txt_size.x + 8; /* small padding for text breathing room */
        tab_mid_w[i] = mid_w;

        /* Middle section — fixed size, no padding, canvas fills entire area */
        lv_obj_t *middle = lv_obj_create(pill);
        lv_obj_set_size(middle, mid_w, pill_height);
        lv_obj_set_style_radius(middle, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(middle, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(middle, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(middle, 0, LV_PART_MAIN);
        lv_obj_clear_flag(middle, LV_OBJ_FLAG_SCROLLABLE);

        /* Canvas covers full middle area */
        lv_obj_t *mid_canvas = lv_canvas_create(middle);
        void *mid_mem = lv_mem_alloc(mid_w * pill_height * sizeof(lv_color_t));
        lv_canvas_set_buffer(mid_canvas, mid_mem, mid_w, pill_height, LV_IMG_CF_TRUE_COLOR_ALPHA);
        lv_obj_set_pos(mid_canvas, 0, 0);
        lv_obj_move_background(mid_canvas);
        tab_mid_mem[i] = mid_mem;

        /* Label centered on top of canvas */
        lv_obj_t *lbl = lv_label_create(middle);
        lv_label_set_text(lbl, tab_names[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_rubik_14, LV_PART_MAIN);
        lv_obj_center(lbl);

        lv_obj_t *right_cap = lv_img_create(pill);
        lv_img_set_src(right_cap, &tab_pills[PILL_ACTIVE].right);

        tab_objs[i] = pill;
        tab_left_caps[i] = left_cap;
        tab_middles[i] = middle;
        tab_right_caps[i] = right_cap;
        tab_labels[i] = lbl;
    }
}

static void tab_bar_create(lv_obj_t *parent)
{
    lv_coord_t w = lv_obj_get_width(parent);

    tab_bar = lv_obj_create(parent);
    lv_obj_set_size(tab_bar, w, 40);
    lv_obj_align(tab_bar, LV_ALIGN_TOP_LEFT, 0, 44);
    lv_obj_set_style_bg_opa(tab_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(tab_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab_bar, 0, LV_PART_MAIN);
    lv_obj_set_layout(tab_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab_bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tab_bar, 4, LV_PART_MAIN);
    lv_obj_clear_flag(tab_bar, LV_OBJ_FLAG_SCROLLABLE);

#if USE_OLD_STYLE_PILLS
    tab_bar_create_old_style(tab_bar);
#else
    tab_bar_create_3slice(tab_bar);
#endif

    tab_bar_update();
}

static void tab_pill_fill_middle(int i, pill_state_t ps)
{
    dash_prerender_tile_middle(tab_mid_mem[i], tab_mid_w[i], pill_height, &tab_pills[ps]);
    lv_obj_invalidate(tab_middles[i]);
}

static void tab_pill_set_state(int i, int state)
{
    /* state: -1=inactive, 0=active, 1=focused */
#if USE_OLD_STYLE_PILLS
    lv_obj_remove_style(tab_objs[i], &tab_active_style, LV_PART_MAIN);
    lv_obj_remove_style(tab_objs[i], &tab_inactive_style, LV_PART_MAIN);
    lv_obj_remove_style(tab_objs[i], &tab_focused_style, LV_PART_MAIN);
    if (state < 0)
        lv_obj_add_style(tab_objs[i], &tab_inactive_style, LV_PART_MAIN);
    else if (state == 1)
        lv_obj_add_style(tab_objs[i], &tab_focused_style, LV_PART_MAIN);
    else
        lv_obj_add_style(tab_objs[i], &tab_active_style, LV_PART_MAIN);
#else
    if (state < 0)
    {
        lv_img_set_src(tab_left_caps[i], &tab_pills[PILL_INACTIVE].left);
        lv_img_set_src(tab_right_caps[i], &tab_pills[PILL_INACTIVE].right);
        lv_obj_clear_flag(tab_left_caps[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(tab_right_caps[i], LV_OBJ_FLAG_HIDDEN);
        tab_pill_fill_middle(i, PILL_INACTIVE);
        lv_obj_set_style_text_color(tab_labels[i], EF_FG_MUTED, LV_PART_MAIN);
    }
    else
    {
        pill_state_t ps = (state == 1) ? PILL_FOCUSED : PILL_ACTIVE;

        lv_obj_clear_flag(tab_left_caps[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(tab_right_caps[i], LV_OBJ_FLAG_HIDDEN);
        lv_img_set_src(tab_left_caps[i], &tab_pills[ps].left);
        lv_img_set_src(tab_right_caps[i], &tab_pills[ps].right);

        tab_pill_fill_middle(i, ps);

        lv_color_t text_col = (ps == PILL_FOCUSED) ? EF_FG : dash_accent_color;
        lv_obj_set_style_text_color(tab_labels[i], text_col, LV_PART_MAIN);
    }
#endif
}

static void tab_bar_update_nav(int highlighted_tab);

static void tab_bar_update(void)
{
    if (tab_nav_active)
    {
        tab_bar_update_nav(current_tab);
        return;
    }
    for (int i = 0; i < TAB_COUNT; i++)
    {
        tab_pill_set_state(i, (i == current_tab) ? 0 : -1);
    }
}

void dash_set_tab(int tab_index)
{
    if (tab_index < 0 || tab_index >= TAB_COUNT) return;
    current_tab = tab_index;
    tab_bar_update();

    /* Update hero strip text */
    const char *eyebrow_texts[] = {"RECENTLY PLAYED", "GAMES", "APPS & HOMEBREW", "BROWSER", "SYSTEM"};
    if (hero_eyebrow)
        lv_label_set_text(hero_eyebrow, eyebrow_texts[current_tab]);

    /* Update controls bar context */
    if (current_tab == 3) /* Files */
        dash_controls_bar_set_context("Open", "Back", "Info", "Sort");
    else if (current_tab == 4) /* System */
        dash_controls_bar_set_context("-", "Back", "Refresh", "Export");
    else
        dash_controls_bar_set_context("Launch", "Back", "Details", "Sort");
}

int dash_get_tab(void)
{
    return current_tab;
}

bool dash_tab_nav_is_active(void)
{
    return tab_nav_active;
}

/* ── Tab bar navigation mode ── */

static void tab_bar_update_nav(int highlighted_tab)
{
    for (int i = 0; i < TAB_COUNT; i++)
    {
        tab_pill_set_state(i, (i == highlighted_tab) ? 1 : -1);
    }
}

/* Find the first TOML page that maps to a given tab index, or -1 */
static int page_for_tab(int tab_index)
{
    int page_count = dash_scroller_get_page_count();
    for (int i = 0; i < page_count; i++)
    {
        const char *title = dash_scroller_get_title(i);
        if (!title) continue;
        int tab = -1;
        if (strcmp(title, "Recent") == 0)            tab = 0;
        else if (strcmp(title, "Applications") == 0)  tab = 2;
        else if (strcmp(title, "Homebrew") == 0)      tab = 2;
        else if (strcmp(title, "Apps") == 0)          tab = 2;
        else                                          tab = 1;
        if (tab == tab_index) return i;
    }
    return -1;
}

static void tab_nav_activate_tab(int tab_index)
{
    current_tab = tab_index;
    tab_bar_update_nav(current_tab);

    /* Switch scroller page if this tab has one */
    int page = page_for_tab(tab_index);
    if (page >= 0)
    {
        dash_scroller_set_page_index(page);
    }
    else
    {
        /* Tab has no page (Files/System) — clear empty label if visible */
        dash_scroller_clear_empty_label();
    }

    /* Update hero eyebrow */
    const char *eyebrow_texts[] = {"RECENTLY PLAYED", "GAMES", "APPS & HOMEBREW", "BROWSER", "SYSTEM"};
    if (hero_eyebrow)
        lv_label_set_text(hero_eyebrow, eyebrow_texts[current_tab]);
}

static void tab_nav_key_handler(lv_event_t *event)
{
    lv_key_t key = *((lv_key_t *)lv_event_get_param(event));

    if (key == LV_KEY_LEFT || key == DASH_PREV_PAGE)
    {
        if (current_tab > 0)
            tab_nav_activate_tab(current_tab - 1);
    }
    else if (key == LV_KEY_RIGHT || key == DASH_NEXT_PAGE)
    {
        if (current_tab < TAB_COUNT - 1)
            tab_nav_activate_tab(current_tab + 1);
    }
    else if (key == LV_KEY_ENTER || key == LV_KEY_DOWN)
    {
        /* Confirm — exit tab nav, stay on current tab/page */
        dash_tab_bar_exit_nav(false);
    }
    else if (key == LV_KEY_ESC)
    {
        /* Cancel — restore original tab/page */
        dash_tab_bar_exit_nav(true);
    }
}

void dash_tab_bar_enter_nav(int current_page)
{
    tab_nav_active = true;
    tab_nav_origin_page = current_page;
    tab_nav_origin_tab = current_tab;

    /* Create an invisible focusable object for key events */
    tab_nav_focus_obj = lv_obj_create(lv_scr_act());
    lv_obj_set_size(tab_nav_focus_obj, 0, 0);
    lv_obj_add_flag(tab_nav_focus_obj, LV_OBJ_FLAG_HIDDEN);
    lv_group_add_obj(lv_group_get_default(), tab_nav_focus_obj);
    lv_obj_add_event_cb(tab_nav_focus_obj, tab_nav_key_handler, LV_EVENT_KEY, NULL);

    /* Highlight current tab with focused style */
    tab_bar_update_nav(current_tab);

    /* Push focus depth so B returns here */
    dash_focus_change_depth(tab_nav_focus_obj);
}

void dash_tab_bar_exit_nav(bool cancel)
{
    if (!tab_nav_active) return;
    tab_nav_active = false;

    if (cancel)
    {
        /* Restore original tab and page */
        current_tab = tab_nav_origin_tab;
    }

    /* Delete the nav focus object first */
    if (tab_nav_focus_obj)
    {
        lv_obj_del(tab_nav_focus_obj);
        tab_nav_focus_obj = NULL;
    }
    /* Discard the stale focus stack entry (don't pop to a potentially wrong tile) */
    if (focus_stack_index > 0) focus_stack_index--;

    /* Restore normal tab styling */
    tab_bar_update();

    /* Re-set the correct page so focus lands on a valid, visible tile */
    int target_page = cancel ? tab_nav_origin_page : page_for_tab(current_tab);
    if (target_page >= 0)
        dash_scroller_set_page_index(target_page);
    else if (tab_nav_origin_page >= 0)
        dash_scroller_set_page_index(tab_nav_origin_page);
}

/* Update the meta row with title info */
void dash_update_meta(const char *title, const char *subtitle)
{
    if (meta_title_label && title)
    {
        lv_label_set_text(meta_title_label, title);
    }
    if (meta_sub_label && subtitle)
    {
        lv_label_set_text(meta_sub_label, subtitle);
    }
}

void dash_update_hero_counter(int selected, int total)
{
    if (hero_counter)
    {
        lv_label_set_text_fmt(hero_counter, "%d / %d", selected + 1, total);
    }
}

void dash_create()
{
    dash_settings_read();

    // Resolve accent color
    lv_color_t col = dash_accent_from_enum(
        (dash_settings.accent_index < ACCENT_MAX) ? dash_settings.accent_index : ACCENT_GREEN);
    dash_styles_init(col);

    lv_coord_t scr_w = lv_obj_get_width(lv_scr_act());
    lv_coord_t scr_h = lv_obj_get_height(lv_scr_act());
    lv_obj_set_scrollbar_mode(lv_scr_act(), LV_SCROLLBAR_MODE_OFF);

    /* Layer 0: Background fill (EF_BG_DIM) */
    lv_obj_t *bg = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bg, scr_w, scr_h);
    lv_obj_set_style_bg_color(bg, EF_BG_DIM, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);
    lv_obj_center(bg);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    /* Layer 1: Backdrop blur (behind everything else) */
    dash_backdrop_create(lv_scr_act());

    /* Layer 2: Status bar (top 44px) */
    dash_statusbar_create(lv_scr_act());

    /* Layer 3: Tab bar */
    tab_bar_create(lv_scr_act());

    /* Layer 4: Hero strip (y=68..128) */
    lv_obj_t *hero = lv_obj_create(lv_scr_act());
    lv_obj_set_size(hero, scr_w - 80, 60);
    lv_obj_align(hero, LV_ALIGN_TOP_LEFT, 40, 68);
    lv_obj_set_style_bg_opa(hero, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(hero, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hero, 0, LV_PART_MAIN);
    lv_obj_set_layout(hero, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);

    hero_eyebrow = lv_label_create(hero);
    lv_obj_add_style(hero_eyebrow, &eyebrow_style, LV_PART_MAIN);
    lv_obj_set_style_text_color(hero_eyebrow, dash_accent_color, LV_PART_MAIN);
    lv_label_set_text(hero_eyebrow, "RECENTLY PLAYED");

    hero_counter = lv_label_create(hero);
    lv_obj_add_style(hero_counter, &mono_small_style, LV_PART_MAIN);
    lv_label_set_text(hero_counter, "0 / 0");

    /* Layer 5: Rail — created by dash_scroller_init() */

    /* Layer 6: Meta row (bottom 68px above controls bar) */
    lv_obj_t *meta = lv_obj_create(lv_scr_act());
    lv_obj_set_size(meta, scr_w - 80, 50);
    lv_obj_align(meta, LV_ALIGN_BOTTOM_LEFT, 40, -62);
    lv_obj_set_style_bg_opa(meta, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(meta, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(meta, 0, LV_PART_MAIN);
    lv_obj_set_layout(meta, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(meta, LV_OBJ_FLAG_SCROLLABLE);

    /* Left: title + subtitle */
    lv_obj_t *meta_left = lv_obj_create(meta);
    lv_obj_set_size(meta_left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(meta_left, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(meta_left, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(meta_left, 0, LV_PART_MAIN);
    lv_obj_set_layout(meta_left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(meta_left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(meta_left, 2, LV_PART_MAIN);
    lv_obj_clear_flag(meta_left, LV_OBJ_FLAG_SCROLLABLE);

    meta_title_label = lv_label_create(meta_left);
    lv_obj_add_style(meta_title_label, &meta_title_style, LV_PART_MAIN);
    lv_label_set_text(meta_title_label, "");

    meta_sub_label = lv_label_create(meta_left);
    lv_obj_add_style(meta_sub_label, &body_muted_style, LV_PART_MAIN);
    lv_label_set_text(meta_sub_label, "");

    /* Right: pills */
    meta_pills = lv_obj_create(meta);
    lv_obj_set_size(meta_pills, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(meta_pills, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(meta_pills, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(meta_pills, 0, LV_PART_MAIN);
    lv_obj_set_layout(meta_pills, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(meta_pills, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(meta_pills, 8, LV_PART_MAIN);
    lv_obj_clear_flag(meta_pills, LV_OBJ_FLAG_SCROLLABLE);

    /* "0 LAUNCHES" pill — pre-compiled image */
    meta_launches_pill = lv_obj_create(meta_pills);
    lv_obj_set_size(meta_launches_pill, pill_meta_launches.header.w, pill_meta_launches.header.h);
    lv_obj_set_style_bg_opa(meta_launches_pill, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(meta_launches_pill, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(meta_launches_pill, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(meta_launches_pill, 0, LV_PART_MAIN);
    lv_obj_clear_flag(meta_launches_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *launches_img = lv_img_create(meta_launches_pill);
    lv_img_set_src(launches_img, &pill_meta_launches);
    lv_obj_set_pos(launches_img, 0, 0);
    meta_launches_label = lv_label_create(meta_launches_pill);
    lv_label_set_text(meta_launches_label, "0 LAUNCHES");
    lv_obj_add_flag(meta_launches_label, LV_OBJ_FLAG_FLOATING);
    lv_obj_center(meta_launches_label);

    /* "READY" pill — pre-compiled image with green border */
    lv_obj_t *ready_pill = lv_obj_create(meta_pills);
    lv_obj_set_size(ready_pill, pill_meta_ready_green.header.w, pill_meta_ready_green.header.h);
    lv_obj_set_style_bg_opa(ready_pill, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ready_pill, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ready_pill, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ready_pill, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ready_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ready_img = lv_img_create(ready_pill);
    lv_img_set_src(ready_img, &pill_meta_ready_green);
    lv_obj_set_pos(ready_img, 0, 0);
    lv_obj_t *ready_lbl = lv_label_create(ready_pill);
    lv_obj_set_style_text_color(ready_lbl, EF_GREEN, LV_PART_MAIN);
    lv_label_set_text(ready_lbl, LV_SYMBOL_OK " READY");
    lv_obj_add_flag(ready_lbl, LV_OBJ_FLAG_FLOATING);
    lv_obj_center(ready_lbl);

    /* Layer 7: Controls bar (bottom 52px) */
    dash_controls_bar_create(lv_scr_act());

    /* Create the scrollers for gameart */
    dash_scroller_init();

    /* dash_scroller_init has threads. Use lvgl locks now */
    lvgl_getlock();
    dash_scroller_scan_db();
    dash_scroller_set_page();

    if (in_memory_warning)
    {
        create_warning_box("Warning: Could not open database at " DASH_DATABASE_PATH
                                     " Using in memory database only.");
    }
    if (strlen(err_msg_toml) > 0)
    {
        create_warning_box(err_msg_toml);
    }
    if (strlen(err_msg_db) > 0)
    {
        create_warning_box(err_msg_db);
    }

    /* Initial tab — sync with startup page */
    {
        const char *page_title = dash_scroller_get_title(dash_settings.startup_page_index);
        if (page_title)
        {
            if (strcmp(page_title, "Recent") == 0)            dash_set_tab(0);
            else if (strcmp(page_title, "Applications") == 0) dash_set_tab(2);
            else if (strcmp(page_title, "Homebrew") == 0)     dash_set_tab(2);
            else if (strcmp(page_title, "Apps") == 0)         dash_set_tab(2);
            else                                              dash_set_tab(1);
        }
        else dash_set_tab(0);
    }

    lvgl_removelock();
}
