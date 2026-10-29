"""Pixel regression of native production ST7789 fonts, runtime and menu templates."""
from pathlib import Path
import csv
import os
import re
import shutil
import subprocess
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RATE_TEXT = ['0.000', '0.008', '0.009', '1.234', '10.000', '10.00', '100.00',
             '100.0', '1000.0', '1000', '99999', '----', '0.000']


def function(source, name):
    match = re.search(r'^(?:static )?[^\n;{}]+\b' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    if not match:
        raise ValueError(name)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


def replace_function(source, name, body):
    original = function(source, name)
    return source.replace(original, original[:original.index('{')] + '{' + body + '}\n')


def menu_source():
    """Compile actual drawing functions; substitute only parameter/hardware inputs."""
    source = (ROOT / 'BSP/bsp_menu.c').read_text(encoding='utf-8-sig')
    params = (ROOT / 'BSP/param_storage.c').read_text(encoding='utf-8-sig')
    header = (ROOT / 'BSP/param_storage.h').read_text(encoding='utf-8-sig')
    body = '#include "display.h"\n#include "ftoa.h"\n#include <stdio.h>\n#include <string.h>\n'
    body += source[source.index('typedef enum'):source.index('/* ===== 模块静态变量')]
    body += header[header.index('typedef enum'):header.index('/* ===== 参数集合结构体')]
    body += '\n'.join(re.findall(r'^#define DEF_.*', params, re.M)) + '\n'
    body += params[params.index('static const char * const s_std_cond_str'):params.index('/* ===== 内部 RAM')]
    macros = dict(re.findall(r'^#define DEF_(\w+)\s+([^\s/]+)', params, re.M))
    for kind, key in re.findall(r'^(uint8_t|uint16_t|float)\s+param_get_(\w+)\(void\);', header, re.M):
        if key.upper() in macros:
            body += f'{kind} param_get_{key}(void) {{ return DEF_{key.upper()}; }}\n'
    for key in ['std_cond', 'flow_unit', 'total_unit', 'pulse_equiv', 'baud_rate']:
        body += f'const char * const *param_get_{key}_strings(void) {{ return s_{key}_str; }}\n'
    body += '''const char *param_get_total_unit_str(uint8_t i) { return s_total_unit_str[i]; }
static access_level_t s_access_level;
static uint16_t DacZeroValue = DEF_DAC_ZERO, DacFullValue = DEF_DAC_FULL;
static float SpanLoValue = 4.0f, SpanHiValue = 20.0f;
static const char * const s_language_str[LANG_COUNT] = { "English" };
static void Int2String(int n, char *text) { itoa_local(n, text); }
'''
    for name in ['get_list_data', 'detect_mode', 'load_param_val', 'load_enum_idx',
                 'load_readonly_val', 'is_coeff_screen', 'get_numeric_range',
                 'coeff_digits_from_value', 'get_screen_title']:
        body += function(source, name)
    body += source[source.index('#define LCD_MENU_ROWS'):source.index('static void render_menu_frame')]
    body += (ROOT / 'tests/menu_sim.c').read_text(encoding='utf-8')
    return body


class DisplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='umf-display-')
        cls.work = Path(cls.temp.name)
        source = (ROOT / 'BSP/display.c').read_text(encoding='utf-8-sig')
        source = source.replace('#include "lcd_init.h"', '#include "sim_hw.h"')
        for name in ['LCD_StreamBegin', 'LCD_StreamEnd']:
            source = replace_function(source, name, '')
        source = replace_function(source, 'LCD_StreamWrite8', '(void)dat;')
        source = replace_function(source, 'stream_color', 'sim_pixel(color);')
        source = source.replace('    const industrial_font_t *data = lcd_font_data(font);\n    if (!text) return;',
                                '    const industrial_font_t *data = lcd_font_data(font);\n'
                                '    sim_check_text(x, y, text, font);\n    if (!text) return;')
        if '    sim_check_text(x, y, text, font);' not in source:
            raise RuntimeError('DrawText bounds instrumentation did not match production source')
        source += '''\nvoid sim_check_text(uint16_t x, uint16_t y, const char *text, oled_font_t font)
{
    if (text && *text && (x + OLED_TextWidth(text, font) > OLED_WIDTH ||
                         y + OLED_FontHeight(font) > OLED_HEIGHT)) {
        fprintf(stderr, "Text outside display: %u,%u %s\\n", x, y, text);
        exit(7);
    }
}
'''
        source = '#include <stdio.h>\n#include <stdlib.h>\n' + source
        (cls.work / 'display.c').write_text(source, encoding='utf-8')
        for name in ['display.h', 'run_display.h', 'run_display.c', 'ftoa.c', 'ftoa.h']:
            shutil.copyfile(ROOT / 'BSP' / name, cls.work / name)
        for name in ['industrial_fonts.h', 'bmp.h']:
            shutil.copyfile(ROOT / 'OLED' / name, cls.work / name)
        (cls.work / 'menu.c').write_text(menu_source(), encoding='utf-8')
        (cls.work / 'display_config.h').write_text('#define DISPLAY_ST7789 1\n')
        (cls.work / 'stm32f1xx_hal.h').write_text('#include <stdint.h>\n')
        (cls.work / 'bsp_usart.h').write_text('typedef struct { float num; } Uart_SendfloatTypeDef;\n')
        (cls.work / 'ssd1306.h').write_text('#include <stdint.h>\n#include <stddef.h>\n')
        (cls.work / 'ssd1306_fonts.h').write_text('')
        (cls.work / 'param_storage.h').write_text('#include <stdint.h>\nuint8_t param_get_display_rotation(void);\n')
        (cls.work / 'sim_hw.h').write_text('''#include <stdint.h>
void sim_check_text(uint16_t x, uint16_t y, const char *text, oled_font_t font);
typedef struct { uint32_t KR; } sim_iwdg_t;
extern sim_iwdg_t sim_iwdg;
#define IWDG (&sim_iwdg)
void sim_pixel(uint16_t color);
void LCD_Address_Set(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2);
static inline void LCD_Init(void) { }
static inline void LCD_SetRotation(uint8_t x) { (void)x; }
static inline void LCD_WR_REG(uint8_t x) { (void)x; }
static inline void LCD_WR_DATA8(uint8_t x) { (void)x; }
''')
        executable = cls.work / 'display-test.exe'
        command = [os.environ.get('CC', 'gcc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-function', '-O0', '-I' + str(cls.work),
                   *[str(cls.work / name) for name in ['display.c', 'menu.c', 'run_display.c', 'ftoa.c']],
                   str(ROOT / 'tests/display_sim.c'), '-o', str(executable)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)
        result = subprocess.run([str(executable)], cwd=cls.work, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f'pixel harness exit {result.returncode}: {result.stdout} {result.stderr}')
        print(result.stdout.strip())
        with (cls.work / 'menu_refresh.csv').open(newline='') as trace:
            cls.moves = [{key: value if key == 'kind' else int(value)
                          for key, value in row.items()} for row in csv.DictReader(trace)]
        if os.environ.get('UMF_DISPLAY_EXPORT'):
            export = Path(os.environ['UMF_DISPLAY_EXPORT'])
            export.mkdir(parents=True, exist_ok=True)
            for frame in cls.work.glob('*.rgb565'):
                cls.decode(frame.read_bytes()).save(export / (frame.stem + '.png'))

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    @staticmethod
    def decode(raw):
        rgb = bytearray()
        for i in range(0, len(raw), 2):
            pixel = int.from_bytes(raw[i:i + 2], 'little')
            rgb.extend(((pixel >> 11) * 255 // 31, ((pixel >> 5) & 63) * 255 // 63,
                        (pixel & 31) * 255 // 31))
        return Image.frombytes('RGB', (240, 240), bytes(rgb))

    def frame(self, kind, index):
        return (self.work / f'{kind}_{index}.rgb565').read_bytes()

    def pixel(self, raw, x, y):
        return int.from_bytes(raw[(y * 240 + x) * 2:(y * 240 + x + 1) * 2], 'little')

    def test_all_native_glyphs_match_independent_ttf_rasters(self):
        for index in range(4):
            expected = Image.open(ROOT / f'tests/fixtures/industrial-font-{index}.png').convert('RGB')
            self.assertEqual(self.decode(self.frame('font', index)).tobytes(), expected.tobytes(), f'font {index}')

    def test_changed_digits_lengths_and_decimal_positions_match_full_render(self):
        for index in range(len(RATE_TEXT)):
            self.assertEqual(self.frame('partial', index), self.frame('full', index), f'rate {RATE_TEXT[index]}')

    def test_main_has_industrial_dividers_and_large_white_digits(self):
        image = self.frame('main', 0)
        for y in [39, 170]:
            self.assertTrue(all(self.pixel(image, x, y) == 0x31C8 for x in range(14, 226)))
        white_rows = [y for y in range(74, 138)
                      if any(self.pixel(image, x, y) == 0xFFFF for x in range(8, 232))]
        self.assertGreaterEqual(max(white_rows) - min(white_rows), 40)
        self.assertEqual(self.pixel(image, 0, 0), 0)

    def test_main_units_and_full_total_remain_visible(self):
        for kind, index in [('main', 0), ('units', 0), ('units', 1)]:
            image = self.frame(kind, index)
            for x0, y0, x1, y1 in [(90, 141, 150, 161), (14, 204, 180, 224), (205, 183, 226, 199)]:
                self.assertTrue(any(self.pixel(image, x, y) != 0 for y in range(y0, y1)
                                    for x in range(x0, x1)), f'{kind} {index} region {x0},{y0}')

    def test_menu_partial_updates_match_fresh_full_render(self):
        paths = list(self.work.glob('menu_partial_*.rgb565'))
        self.assertGreaterEqual(len(paths), 100)
        for path in paths:
            self.assertEqual(path.read_bytes(), (self.work / path.name.replace('_partial_', '_full_')).read_bytes(), path.name)

    def test_all_43_menu_pages_have_header_and_footer(self):
        for screen in range(3, 46):
            image = self.frame('menu_full', screen)
            self.assertEqual(self.pixel(image, 14, 39), 0x31C8, f'screen {screen}')
            self.assertEqual(self.pixel(image, 14, 212), 0x31C8, f'screen {screen}')

    def test_selected_row_has_cyan_edge_and_five_row_window(self):
        image = self.frame('menu_full', 5)
        self.assertEqual(self.pixel(image, 9, 47), 0x565B)
        self.assertEqual(self.pixel(image, 12, 47), 0x1187)
        self.assertEqual(self.pixel(image, 12, 78), 0)
        self.assertEqual(self.pixel(image, 12, 210), 0)

    def test_confirmation_defaults_to_no_on_left(self):
        image = self.frame('menu_full', 32)
        self.assertEqual(self.pixel(image, 14, 159), 0x1187)
        self.assertEqual(self.pixel(image, 128, 159), 0)

    def test_lists_and_enums_use_fixed_five_item_pages(self):
        for kind in ['list', 'enum']:
            moves = [move for move in self.moves if move['kind'] == kind]
            self.assertTrue(moves)
            for move in moves:
                self.assertEqual(move['start'], move['current'] // 5 * 5, move)

    def test_same_page_selection_does_not_redraw_list(self):
        for move in self.moves:
            if move['previous'] // 5 == move['current'] // 5:
                self.assertEqual(move['clears'], 0, move)
                self.assertLess(move['pixels'], 25000, move)

    def test_cross_page_selection_redraws_list_once_without_full_screen(self):
        for move in self.moves:
            self.assertEqual(move['full_frames'], 0, move)
            if move['previous'] // 5 != move['current'] // 5:
                self.assertEqual(move['clears'], 1, move)
                image = self.frame('menu_partial', move['frame'])
                row = 0 if move['current'] > move['previous'] else 4
                self.assertEqual(self.pixel(image, 9, 47 + row * 33), 0x565B, move)

    def test_last_partial_page_clears_old_rows(self):
        # The 11-item list and both 6-item enums must leave four rows empty.
        for screen, current in [(5, 10), (18, 5), (42, 5)]:
            move = next(move for move in self.moves
                        if move['screen'] == screen and move['current'] == current)
            image = self.frame('menu_partial', move['frame'])
            self.assertTrue(all(self.pixel(image, x, y) == 0
                                for y in range(80, 210) for x in range(9, 231)), move)


if __name__ == '__main__':
    unittest.main(verbosity=2)
