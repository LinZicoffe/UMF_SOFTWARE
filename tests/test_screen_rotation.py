"""Run production menu, storage and LCD commands with hardware boundaries replaced."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_industrial_display import function, replace_function

ROOT = Path(__file__).resolve().parents[1]


class RotationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='umf-rotation-')
        cls.work = Path(cls.temp.name)
        for name in ['param_storage.h', 'app_fw_version.h', 'bsp_menu.h', 'key.h',
                     'display.h', 'display_config.h', 'ftoa.h', 'ftoa.c', 'mystring.h']:
            shutil.copyfile(ROOT / 'BSP' / name, cls.work / name)
        for name in ['lcd_init.h', 'lcd.h']:
            text = (ROOT / 'newOLED' / name).read_bytes().decode('gb18030')
            (cls.work / name).write_text(text, encoding='utf-8')
        storage = (ROOT / 'BSP/param_storage.c').read_text(encoding='utf-8-sig')
        for bits in [16, 32]:
            storage = replace_function(storage, f'flash_rd{bits}',
                                       f'return sim_flash_read(addr, {bits // 8}U);')
        storage = replace_function(storage, 'feed_iwdg', '')
        (cls.work / 'storage.c').write_text(storage, encoding='utf-8')
        lcd = (ROOT / 'newOLED/lcd_init.c').read_bytes().decode('gb18030').replace('\r\n', '\n')
        lcd = replace_function(lcd, 'LCD_Writ_Bus', 'sim_bus_write(dat);')
        (cls.work / 'lcd_init.c').write_text(lcd, encoding='utf-8')
        # Initialization/recovery are production functions; drawing is covered by pixel tests.
        display = (ROOT / 'BSP/display.c').read_text(encoding='utf-8-sig')
        lifecycle = '#include "display.h"\n#include "lcd_init.h"\n#include "param_storage.h"\n'
        lifecycle += 'static uint16_t s_cursor_x, s_cursor_y;\n'
        lifecycle += ''.join(function(display, name) for name in
                             ['OLED_Init', 'OLED_Recovery', 'OLED_SetRotation'])
        (cls.work / 'lifecycle.c').write_text(lifecycle, encoding='utf-8')
        shutil.copyfile(ROOT / 'BSP/bsp_menu.c', cls.work / 'menu.c')
        for name, contents in {
            'stm32f1xx_hal.h': '#include "sim_hw.h"\n',
            'main.h': '#include "sim_hw.h"\n',
            'boot_flag.h': '#include "sim_hw.h"\n',
            'run_display.h': 'void run_display_invalidate(void);\n',
        }.items():
            (cls.work / name).write_text(contents, encoding='utf-8')
        (cls.work / 'sim_hw.h').write_text('''#ifndef SIM_HW_H
#define SIM_HW_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
typedef struct { uint32_t TypeErase, PageAddress, NbPages; } FLASH_EraseInitTypeDef;
#define FLASH_FLAG_BSY 1
#define FLASH_FLAG_EOP 2
#define FLASH_FLAG_PGERR 4
#define FLASH_FLAG_WRPERR 8
#define FLASH_TYPEERASE_PAGES 0
#define FLASH_TYPEPROGRAM_HALFWORD 0
#define __HAL_FLASH_CLEAR_FLAG(x) ((void)(x))
void HAL_FLASH_Unlock(void);
void HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *, uint32_t *);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t, uint32_t, uint64_t);
uint32_t sim_flash_read(uint32_t, uint32_t);
uint32_t boot_crc32(const void *, uint32_t);
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t);
void HAL_GPIO_WritePin(void *, uint32_t, uint32_t);
#define OLED_DC_GPIO_Port ((void *)0)
#define OLED_DC_Pin 1
#define OLED_RES_GPIO_Port ((void *)0)
#define OLED_RES_Pin 2
#define GPIO_PIN_RESET 0
#define GPIO_PIN_SET 1
void sim_bus_write(uint8_t);
extern uint16_t DacZeroValue, DacFullValue;
extern float SpanLoValue, SpanHiValue;
void bsp_usart_set_modbus_addr(uint16_t);
void bsp_usart2_apply_uart_config(uint8_t);
#endif
''', encoding='utf-8')
        cls.exe = cls.work / 'rotation-test.exe'
        command = [os.environ.get('CC', 'gcc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                   '-I' + str(cls.work), *[str(cls.work / name) for name in
                   ['storage.c', 'lcd_init.c', 'lifecycle.c', 'menu.c', 'ftoa.c']],
                   str(ROOT / 'tests/rotation_sim.c'), '-o', str(cls.exe)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check_case(self, name):
        result = subprocess.run([str(self.exe), name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_four_clockwise_commands_and_visible_windows(self):
        self.check_case('driver')

    def test_menu_confirm_cancel_limits_and_factory_reset(self):
        self.check_case('menu')

    def test_flash_roundtrip_old_pages_and_invalid_values(self):
        self.check_case('storage')

    def test_boot_and_recovery_keep_selected_rotation(self):
        self.check_case('recovery')


if __name__ == '__main__':
    unittest.main(verbosity=2)
