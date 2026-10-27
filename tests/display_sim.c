#include "run_display.h"
#include "display.h"
#include "sim_hw.h"
#include <stdio.h>
#include <string.h>

sim_iwdg_t sim_iwdg;
static uint16_t s_pixels[240][240];
static uint16_t s_x, s_y, s_right, s_left;
static unsigned s_writes;
static uint8_t s_out_of_bounds;

void LCD_Address_Set(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    if (x1 > x2 || y1 > y2 || x2 >= 240 || y2 >= 240) s_out_of_bounds = 1U;
    s_x = s_left = x1;
    s_y = y1;
    s_right = x2;
}

void sim_pixel(uint16_t color)
{
    if (s_x < 240 && s_y < 240) s_pixels[s_y][s_x] = color;
    else s_out_of_bounds = 1U;
    s_writes++;
    if (++s_x > s_right) {
        s_x = s_left;
        s_y++;
    }
}

void save_frame(const char *prefix, unsigned index)
{
    char path[64];
    unsigned x, y;
    FILE *file;
    snprintf(path, sizeof(path), "%s_%u.rgb565", prefix, index);
    file = fopen(path, "wb");
    for (y = 0; y < 240; y++) {
        for (x = 0; x < 240; x++) {
            fputc(s_pixels[y][x] & 0xff, file);
            fputc(s_pixels[y][x] >> 8, file);
        }
    }
    fclose(file);
}

int sim_menu_tests(void);

int main(void)
{
    static const float rates[] = {
        0.0f, 0.008f, 0.009f, 1.234f, 9.9996f, 10.0f, 99.999f, 100.0f,
        999.999f, 1000.0f, 99999.0f, 100000.0f, -3.0f
    };
    Uart_SendfloatTypeDef rate = { 0 }, temp = { 25.0f }, pressure = { 101.3f };
    uint8_t module_state = 0, sum_unit = 0;
    uint16_t dac = 36050, dacs[2] = { 12100, 60000 };
    uint64_t cumulative = 1234567;
    run_display_input_t input = {
        &rate, &temp, &pressure, &cumulative,
        (const unsigned char *)"000001234.567", &sum_unit, &module_state,
        &dac, dacs, "m3/h", "m3"
    };
    unsigned i;
    run_display_init(NULL);
    for (i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        rate.num = rates[i];
        run_display_render(&input);
        save_frame("partial", i);
        OLED_Clear(OLED_BLACK);
        run_display_invalidate();
        s_writes = 0;
        run_display_render(&input);
        if (s_writes != 57600U) return 2;
        save_frame("full", i);
        s_writes = 0;
        run_display_render(&input);
        if (s_writes != 0U) return 3;
    }
    rate.num = 123.4f;
    run_display_invalidate();
    run_display_render(&input);
    save_frame("main", 0);
    run_display_set_page(RUN_PAGE_AUX);
    run_display_render(&input);
    save_frame("aux", 0);
    s_writes = 0;
    run_display_render(&input);
    if (s_writes != 0U) return 3;
    for (i = 0; i < 4; i++) {
        unsigned g, count = i < 2 ? 97 : 12;
        unsigned columns = i == 0 ? 16 : i == 1 ? 12 : i == 2 ? 6 : 4;
        unsigned cell_width = 240 / columns;
        unsigned cell_height = OLED_FontHeight((oled_font_t)i);
        OLED_Clear(OLED_BLACK);
        for (g = 0; g < count; g++) {
            char text[3] = { 0, 0, 0 };
            if (i >= 2) text[0] = "0123456789.-"[g];
            else if (g < 95) text[0] = (char)(g + 32);
            else { text[0] = (char)0xC2; text[1] = g == 95 ? (char)0xB0 : (char)0xB3; }
            OLED_DrawText((g % columns) * cell_width, (g / columns) * cell_height,
                          text, (oled_font_t)i, OLED_WHITE, OLED_BLACK);
        }
        save_frame("font", i);
    }
    input.p_flow_unit_str = "L/h";
    input.p_total_unit_str = "L";
    run_display_set_page(RUN_PAGE_MAIN);
    run_display_invalidate();
    run_display_render(&input);
    save_frame("units", 0);
    input.p_flow_unit_str = "L/min";
    input.p_total_unit_str = "kg";
    run_display_invalidate();
    run_display_render(&input);
    save_frame("units", 1);
    if (sim_menu_tests() != 0) return 6;
    if (s_out_of_bounds) return 4;
    puts("PASS: bounded full frames, partial updates and unchanged-value no-write checks");
    return 0;
}
