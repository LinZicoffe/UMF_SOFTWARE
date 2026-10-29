/* Included after the unchanged production menu rendering functions. */
void save_frame(const char *prefix, unsigned index);
void reset_refresh_stats(void);
void save_refresh_stats(FILE *file, const char *kind, unsigned screen,
                        unsigned previous, unsigned current, unsigned frame);

static nav_frame_t s_test_frame;

static void draw_test_menu(const void *context)
{
    nav_frame_t *f = &s_test_frame;
    (void)context;
    switch (f->mode) {
    case MODE_LIST: render_list(f); break;
    case MODE_NUMERIC: render_numeric(f); break;
    case MODE_ENUM: render_enum(f); break;
    case MODE_PASSWORD: render_password(f); break;
    case MODE_READONLY: render_readonly(f); break;
    case MODE_CONFIRM: render_confirm(f); break;
    }
}

static void fresh_menu(unsigned index)
{
    OLED_RenderFrame(draw_test_menu, NULL);
    save_frame("menu_full", index);
}

static void compare_menu(unsigned index)
{
    save_frame("menu_partial", index);
    fresh_menu(index);
}

int sim_menu_tests(void)
{
    nav_frame_t *f = &s_test_frame;
    const list_item_t *items;
    uint8_t count, previous, scroll;
    unsigned id, index = 0;
    FILE *trace = fopen("menu_refresh.csv", "w");
    if (!trace) return 1;
    fputs("kind,screen,previous,current,start,clears,full_frames,pixels,frame\n", trace);
    s_access_level = ACCESS_DEVELOPER;
    for (id = 3; id < SCR_COUNT; id++) {
        memset(f, 0, sizeof(*f));
        f->screen_id = (screen_t)id;
        f->mode = id == 4 ? MODE_PASSWORD : detect_mode(f->screen_id);
        f->edit_val = load_param_val(f->screen_id);
        f->enum_val = load_enum_idx(f->screen_id);
        coeff_digits_from_value(f, f->edit_val);
        fresh_menu(id);
    }
    /* Both directions, including five-row window boundaries. */
    for (id = 3; id < SCR_COUNT; id++) {
        if (detect_mode((screen_t)id) != MODE_LIST) continue;
        memset(f, 0, sizeof(*f));
        f->screen_id = (screen_t)id;
        f->mode = MODE_LIST;
        get_list_data(f->screen_id, &items, &count);
        if (count == 0U) continue;
        OLED_RenderFrame(draw_test_menu, NULL);
        for (unsigned step = 0; step < (unsigned)(count - 1) * 2; step++) {
            previous = f->cursor;
            scroll = f->scroll;
            f->cursor += step < (unsigned)(count - 1) ? 1 : -1;
            reset_refresh_stats();
            render_list_selection(f, items, count, previous, scroll);
            save_refresh_stats(trace, "list", id, previous, f->cursor, 100 + index);
            compare_menu(100 + index++);
        }
    }
    for (id = 3; id < SCR_COUNT; id++) {
        if (detect_mode((screen_t)id) != MODE_ENUM) continue;
        const char * const *options;
        memset(f, 0, sizeof(*f));
        f->screen_id = (screen_t)id;
        f->mode = MODE_ENUM;
        get_enum_data(f->screen_id, &options, &count);
        OLED_RenderFrame(draw_test_menu, NULL);
        for (unsigned step = 0; step < (unsigned)(count - 1) * 2; step++) {
            previous = f->enum_val;
            f->enum_val += step < (unsigned)(count - 1) ? 1 : -1;
            reset_refresh_stats();
            render_enum_selection(f, previous);
            save_refresh_stats(trace, "enum", id, previous, f->enum_val, 100 + index);
            compare_menu(100 + index++);
        }
    }
    fclose(trace);
    memset(f, 0, sizeof(*f));
    f->screen_id = SCR_4MA_VALUE;
    f->mode = MODE_NUMERIC;
    f->edit_val = -9999.0f;
    OLED_RenderFrame(draw_test_menu, NULL);
    {
        static const float values[] = { 0.0f, 9.9f, 10.0f, 99999.0f, -0.1f };
        for (unsigned step = 0; step < sizeof(values) / sizeof(values[0]); step++) {
            float old_value = f->edit_val;
            f->edit_val = values[step];
            render_numeric_value(f, old_value);
            compare_menu(100 + index++);
        }
    }
    f->screen_id = SCR_METER_COEFF;
    coeff_digits_from_value(f, 1.0f);
    OLED_RenderFrame(draw_test_menu, NULL);
    for (unsigned step = 0; step < 5; step++) {
        previous = f->cursor;
        f->cursor = step;
        f->coeff_digits[step] = 9;
        render_coeff_digit(f, previous);
        render_coeff_digit(f, f->cursor);
        compare_menu(100 + index++);
    }
    f->screen_id = SCR_PASSWORD;
    f->mode = MODE_PASSWORD;
    f->cursor = 0;
    OLED_RenderFrame(draw_test_menu, NULL);
    for (unsigned step = 0; step < 3; step++) {
        previous = f->cursor;
        f->cursor = step;
        f->pwd_digits[step] = 8;
        render_password_digit(f, previous);
        render_password_digit(f, f->cursor);
        compare_menu(100 + index++);
    }
    for (unsigned step = 0; step < 2; step++) {
        f->pwd_err_visible = step == 0;
        render_password_content(f);
        compare_menu(100 + index++);
    }
    f->screen_id = SCR_CLEAR_TOTALS;
    f->mode = MODE_CONFIRM;
    OLED_RenderFrame(draw_test_menu, NULL);
    f->confirm_sel = 1;
    render_confirm_buttons(f);
    compare_menu(100 + index++);
    printf("PASS: 43 menu pages and %u partial updates captured\n", index);
    return 0;
}
