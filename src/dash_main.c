// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2022 Ryzee119

#include "lithiumx.h"

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


/* ── Tab bar state ── */
static lv_obj_t *tab_bar;
static lv_obj_t *tab_objs[5];
static int current_tab = 0;
static const char *tab_names[] = {"Home", "Recently Played", "Apps", "Files", "System"};
#define TAB_COUNT 5

/* Hero strip + meta row labels (updated by scroller on focus change) */
static lv_obj_t *hero_eyebrow;
static lv_obj_t *hero_counter;
static lv_obj_t *meta_title_label;
static lv_obj_t *meta_sub_label;
static lv_obj_t *meta_pills;
static lv_obj_t *meta_launches_pill;
static lv_obj_t *meta_launches_label;

static void tab_bar_update(void);

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

    for (int i = 0; i < TAB_COUNT; i++)
    {
        lv_obj_t *pill = lv_obj_create(tab_bar);
        lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_layout(pill, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(pill, 8, LV_PART_MAIN);

        lv_obj_t *lbl = lv_label_create(pill);
        lv_label_set_text(lbl, tab_names[i]);

        tab_objs[i] = pill;
    }
    tab_bar_update();
}

static void tab_bar_update(void)
{
    for (int i = 0; i < TAB_COUNT; i++)
    {
        /* Remove all styles first, then apply the correct one */
        lv_obj_remove_style(tab_objs[i], &tab_active_style, LV_PART_MAIN);
        lv_obj_remove_style(tab_objs[i], &tab_inactive_style, LV_PART_MAIN);

        if (i == current_tab)
        {
            lv_obj_add_style(tab_objs[i], &tab_active_style, LV_PART_MAIN);
        }
        else
        {
            lv_obj_add_style(tab_objs[i], &tab_inactive_style, LV_PART_MAIN);
        }
    }
}

void dash_set_tab(int tab_index)
{
    if (tab_index < 0 || tab_index >= TAB_COUNT) return;
    current_tab = tab_index;
    tab_bar_update();

    /* Update hero strip text */
    const char *eyebrow_texts[] = {"YOUR LIBRARY", "RECENTLY PLAYED", "APPS & HOMEBREW", "BROWSER", "SYSTEM"};
    if (hero_eyebrow)
    {
        lv_label_set_text(hero_eyebrow, eyebrow_texts[current_tab]);
    }

    /* Update controls bar context */
    if (current_tab == 3) /* Files */
    {
        dash_controls_bar_set_context("Open", "Back", "Info", "Sort");
    }
    else if (current_tab == 4) /* System */
    {
        dash_controls_bar_set_context("-", "Back", "Refresh", "Export");
    }
    else
    {
        dash_controls_bar_set_context("Launch", "Back", "Details", "Sort");
    }
}

int dash_get_tab(void)
{
    return current_tab;
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
    lv_label_set_text(hero_eyebrow, "YOUR LIBRARY");

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

    meta_launches_pill = lv_obj_create(meta_pills);
    lv_obj_add_style(meta_launches_pill, &meta_pill_style, LV_PART_MAIN);
    lv_obj_set_size(meta_launches_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(meta_launches_pill, LV_OBJ_FLAG_SCROLLABLE);
    meta_launches_label = lv_label_create(meta_launches_pill);
    lv_label_set_text(meta_launches_label, "0 LAUNCHES");

    lv_obj_t *ready_pill = lv_obj_create(meta_pills);
    lv_obj_add_style(ready_pill, &meta_pill_style, LV_PART_MAIN);
    lv_obj_set_size(ready_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_border_color(ready_pill, EF_GREEN, LV_PART_MAIN);
    lv_obj_set_style_border_opa(ready_pill, 77, LV_PART_MAIN);
    lv_obj_clear_flag(ready_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ready_lbl = lv_label_create(ready_pill);
    lv_obj_set_style_text_color(ready_lbl, EF_GREEN, LV_PART_MAIN);
    lv_label_set_text(ready_lbl, LV_SYMBOL_OK " READY");

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

    /* Initial tab */
    dash_set_tab(0);

    lvgl_removelock();
}
