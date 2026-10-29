/* Flash and LCD bus boundaries; menu navigation and persistence use production C. */
#include "sim_hw.h"
#include "param_storage.h"
#include "bsp_menu.h"
#include "display.h"
#include "lcd_init.h"
#include "mystring.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t s_flash[65536];
static unsigned s_erase_count;
static uint32_t s_last_page;
static uint8_t s_dc = 1;
static struct { uint8_t cmd, len, data[16]; } s_commands[128];
static unsigned s_command_count;
uint16_t DacZeroValue, DacFullValue;
float SpanLoValue, SpanHiValue;

uint32_t sim_flash_read(uint32_t addr, uint32_t size)
{
    uint32_t value = 0;
    assert(addr >= 0x08000000U && addr + size <= 0x08010000U);
    memcpy(&value, &s_flash[addr - 0x08000000U], size);
    return value;
}

void HAL_FLASH_Unlock(void) { }
void HAL_FLASH_Lock(void) { }
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *info, uint32_t *error)
{
    assert(info->PageAddress >= 0x0800F400U && info->PageAddress <= 0x0800FC00U);
    assert(info->NbPages == 1U && info->PageAddress % 1024U == 0U);
    memset(&s_flash[info->PageAddress - 0x08000000U], 0xFF, 1024);
    s_last_page = info->PageAddress;
    s_erase_count++;
    *error = 0;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t addr, uint64_t value)
{
    uint16_t word = (uint16_t)value;
    (void)type;
    assert(addr >= s_last_page && addr + 2U <= s_last_page + 1024U);
    assert((sim_flash_read(addr, 2) & word) == word);
    memcpy(&s_flash[addr - 0x08000000U], &word, 2);
    return HAL_OK;
}

uint32_t boot_crc32(const void *data, uint32_t size)
{
    (void)data; (void)size;
    abort(); /* These cases never contain a legacy backup blob. */
}
uint32_t HAL_GetTick(void) { return 0; }
void HAL_Delay(uint32_t ms) { (void)ms; }
void HAL_GPIO_WritePin(void *port, uint32_t pin, uint32_t value)
{
    (void)port;
    if (pin == OLED_DC_Pin) s_dc = (uint8_t)value;
}
void sim_bus_write(uint8_t value)
{
    if (!s_dc) {
        assert(s_command_count < 128);
        s_commands[s_command_count].cmd = value;
        s_commands[s_command_count++].len = 0;
    } else {
        assert(s_command_count > 0);
        unsigned i = s_command_count - 1;
        assert(s_commands[i].len < sizeof(s_commands[i].data));
        s_commands[i].data[s_commands[i].len++] = value;
    }
}
static uint16_t register_word(uint8_t cmd, unsigned offset)
{
    for (unsigned i = s_command_count; i > 0; i--) {
        if (s_commands[i - 1].cmd == cmd) {
            assert(offset + 1 < s_commands[i - 1].len);
            return (uint16_t)((s_commands[i - 1].data[offset] << 8) |
                              s_commands[i - 1].data[offset + 1]);
        }
    }
    abort();
}
static uint8_t rotation_command(void)
{
    for (unsigned i = s_command_count; i > 0; i--)
        if (s_commands[i - 1].cmd == 0x36) return s_commands[i - 1].data[0];
    abort();
}

/* Drawing is verified separately by the production pixel harness. */
void OLED_Clear(uint16_t color) { (void)color; }
void OLED_FillRectangle(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; }
void OLED_DrawText(uint16_t x, uint16_t y, const char *s, oled_font_t f, uint16_t a, uint16_t b)
{ (void)x; (void)y; (void)s; (void)f; (void)a; (void)b; }
uint16_t OLED_TextWidth(const char *s, oled_font_t f) { (void)f; return (uint16_t)strlen(s); }
void OLED_RenderFrame(void (*render)(const void *), const void *context) { render(context); }
void OLED_Present(void) { }
void run_display_invalidate(void) { }
void bsp_usart_set_modbus_addr(uint16_t addr) { (void)addr; }
void bsp_usart2_apply_uart_config(uint8_t cfg) { (void)cfg; }
char *Int2String(int value, char *text) { sprintf(text, "%d", value); return text; }

static void test_driver(void)
{
    static const uint8_t expected[4] = { 0x00, 0x60, 0xC0, 0xA0 };
    for (uint8_t i = 0; i < 4; i++) {
        s_command_count = 0;
        LCD_SetRotation(i);
        assert(rotation_command() == expected[i]);
        LCD_Address_Set(0, 0, 239, 239);
        assert(register_word(0x2A, 0) == (i == 3 ? 80 : 0));
        assert(register_word(0x2A, 2) == (i == 3 ? 319 : 239));
        assert(register_word(0x2B, 0) == (i == 2 ? 80 : 0));
        assert(register_word(0x2B, 2) == (i == 2 ? 319 : 239));
        LCD_Address_Set(7, 11, 31, 43);
        assert(register_word(0x2A, 0) == 7 + (i == 3 ? 80 : 0));
        assert(register_word(0x2B, 2) == 43 + (i == 2 ? 80 : 0));
    }
    unsigned before = s_command_count;
    LCD_SetRotation(255);
    assert(s_command_count == before);
}

static void test_storage(void)
{
    assert(param_set_meter_coeff(1.125f) == HAL_OK);
    assert(param_set_modbus_addr(17) == HAL_OK);
    for (uint8_t i = 0; i < 4; i++) {
        assert(param_set_display_rotation(i) == HAL_OK);
        assert(param_flush() == HAL_OK);
        assert(param_storage_init() == HAL_OK);
        assert(param_get_display_rotation() == i);
        unsigned before = s_erase_count;
        assert(param_set_display_rotation(i) == HAL_OK);
        assert(param_flush() == HAL_OK && s_erase_count == before);
    }
    assert(param_set_display_rotation(4) == HAL_ERROR);
    assert(param_set_display_rotation(255) == HAL_ERROR);
    assert(param_get_display_rotation() == 3);
    /* Simulate valid pre-feature PPG1 pages: reserved byte FF, recomputed CRCs. */
    for (unsigned page = 0xF400; page <= 0xFC00; page += 1024) {
        uint16_t crc;
        s_flash[page + 0x9E] = 0xFF;
        for (unsigned region = 0; region < 2; region++) {
            unsigned first = region ? 0 : 0x10;
            unsigned last = region ? 10 : 0x110;
            crc = 0xFFFF;
            for (unsigned j = first; j < last; j++) {
                crc ^= (uint16_t)s_flash[page + j] << 8;
                for (unsigned bit = 0; bit < 8; bit++)
                    crc = crc & 0x8000 ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
            }
            memcpy(&s_flash[page + (region ? 10 : 8)], &crc, 2);
        }
    }
    unsigned before = s_erase_count;
    assert(param_storage_init() == HAL_OK && s_erase_count == before);
    assert(param_get_display_rotation() == 0);
    assert(param_get_meter_coeff() == 1.125f && param_get_modbus_addr() == 17);
}

static menu_status_t s_status;
static void key(key_event_t event) { menu_process(event, &s_status); }
static void enter_rotation(void)
{
    menu_init(NULL);
    key(KEY_ENTER); /* Password 000: operator access. */
    key(KEY_ENTER); key(KEY_ENTER); key(KEY_ENTER);
    for (unsigned i = 0; i < 4; i++) key(KEY_DOWN);
    key(KEY_ENTER);
    assert(s_status.screen_id == 39);
    key(KEY_DOWN); key(KEY_ENTER);
    assert(s_status.screen_id == 45 && s_status.mode == 2);
}
static void test_menu(void)
{
    enter_rotation();
    unsigned before = s_erase_count;
    key(KEY_UP); /* lower limit */
    for (unsigned i = 0; i < 5; i++) key(KEY_DOWN);
    assert(param_get_display_rotation() == 0 && s_erase_count == before);
    key(KEY_BACK); /* Cancel does not save/apply. */
    assert(param_get_display_rotation() == 0 && s_command_count == 0);
    key(KEY_ENTER); key(KEY_DOWN); key(KEY_ENTER);
    assert(param_get_display_rotation() == 1 && rotation_command() == 0x60);
    assert(s_status.screen_id == 39);
    assert(param_storage_init() == HAL_OK && param_get_display_rotation() == 1);
    key(KEY_ENTER); key(KEY_UP); key(KEY_ENTER);
    assert(param_get_display_rotation() == 0 && rotation_command() == 0x00);
    enter_rotation();
    for (unsigned i = 0; i < 6; i++) key(KEY_DOWN);
    key(KEY_ENTER);
    assert(param_get_display_rotation() == 3 && rotation_command() == 0xA0);
    key(KEY_ENTER); key(KEY_DOWN); /* already at upper limit */
    key(KEY_HOME);
    assert(param_get_display_rotation() == 3 && rotation_command() == 0xA0);
    enter_rotation();
    key(KEY_BACK); key(KEY_DOWN); key(KEY_ENTER); /* Factory reset */
    key(KEY_DOWN); key(KEY_ENTER);
    assert(param_get_display_rotation() == 0 && rotation_command() == 0x00);
    assert(param_storage_init() == HAL_OK && param_get_display_rotation() == 0);
}

static void test_recovery(void)
{
    static const uint8_t expected[4] = { 0x00, 0x60, 0xC0, 0xA0 };
    for (uint8_t i = 0; i < 4; i++) {
        assert(param_set_display_rotation(i) == HAL_OK && param_flush() == HAL_OK);
        assert(param_storage_init() == HAL_OK);
        s_command_count = 0;
        OLED_Init();
        assert(rotation_command() == expected[i]);
        for (unsigned repeat = 0; repeat < 3; repeat++) {
            s_command_count = 0;
            OLED_Recovery();
            assert(rotation_command() == expected[i]);
            LCD_Address_Set(0, 0, 239, 239);
            assert(register_word(0x2A, 0) == (i == 3 ? 80 : 0));
            assert(register_word(0x2B, 0) == (i == 2 ? 80 : 0));
        }
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    memset(s_flash, 0xFF, sizeof(s_flash));
    assert(param_storage_init() == HAL_OK);
    if (!strcmp(argv[1], "driver")) test_driver();
    else if (!strcmp(argv[1], "storage")) test_storage();
    else if (!strcmp(argv[1], "menu")) test_menu();
    else if (!strcmp(argv[1], "recovery")) test_recovery();
    else return 2;
    puts("PASS");
    return 0;
}
