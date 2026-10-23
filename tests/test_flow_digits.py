"""Host pixel regression for the ST7789 flow field. Run with CC pointing to GCC."""
from pathlib import Path
import hashlib
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FONT_CASES = [(7, 10, 2), (6, 8, 3), (6, 8, 4)]
RATE_TEXT = ['0.000', '1.234', '10.000', '10.00', '100.00', '100.0',
             '1000.0', '1000', '99999', '----', '0.000']


def replace_function(source, name, body):
    match = re.search(r'(?:static )?(?:void|char|uint16_t|uint8_t) ' + name + r'\([^)]*\)\s*\{', source)
    start = match.end() - 1
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[:start] + '{' + body + '}' + source[end:]


def glyph_rows(name):
    source = (ROOT / 'OLED/ssd1306_fonts.c').read_text(encoding='utf-8-sig')
    block = re.search(r'Font' + name + r'\s*\[\]\s*=\s*\{(.*?)\};', source, re.S).group(1)
    block = re.sub(r'//[^\n]*|/\*.*?\*/', '', block, flags=re.S)
    return [int(v, 16) for v in re.findall(r'0x([\dA-Fa-f]+)', block)]


class FlowDigitTests(unittest.TestCase):
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
        (cls.work / 'display.c').write_text(source, encoding='utf-8')
        for name in ['display.h', 'run_display.h', 'run_display.c', 'ftoa.c', 'ftoa.h']:
            shutil.copyfile(ROOT / 'BSP' / name, cls.work / name)
        shutil.copyfile(ROOT / 'OLED/ssd1306_fonts.c', cls.work / 'fonts.c')
        shutil.copyfile(ROOT / 'OLED/bmp.h', cls.work / 'bmp.h')
        for name in ['flow_digits_24x32.h', 'main_labels_24.h']:
            asset = ROOT / 'OLED' / name
            if asset.exists():
                shutil.copyfile(asset, cls.work / asset.name)
        (cls.work / 'display_config.h').write_text('#define DISPLAY_ST7789 1\n')
        (cls.work / 'stm32f1xx_hal.h').write_text('#include <stdint.h>\n')
        (cls.work / 'bsp_usart.h').write_text('typedef struct { float num; } Uart_SendfloatTypeDef;\n')
        (cls.work / 'ssd1306.h').write_text('''#ifndef SIM_SSD1306_H
#define SIM_SSD1306_H
#include <stdint.h>
#include <stddef.h>
typedef struct { const uint8_t width, height; const uint16_t *const data; const uint8_t *const char_width; } SSD1306_Font_t;
#define SSD1306_INCLUDE_FONT_6x8
#define SSD1306_INCLUDE_FONT_7x10
#endif
''')
        (cls.work / 'ssd1306_fonts.h').write_text('''#include "ssd1306.h"
extern const SSD1306_Font_t Font_6x8, Font_7x10;
''')
        (cls.work / 'sim_hw.h').write_text('''#include <stdint.h>
typedef struct { uint32_t KR; } sim_iwdg_t;
extern sim_iwdg_t sim_iwdg;
#define IWDG (&sim_iwdg)
void sim_pixel(uint16_t color);
void LCD_Address_Set(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2);
static inline void LCD_Init(void) { }
static inline void LCD_WR_REG(uint8_t x) { (void)x; }
static inline void LCD_WR_DATA8(uint8_t x) { (void)x; }
''')
        executable = cls.work / 'display-test.exe'
        command = [os.environ.get('CC', 'gcc'), '-std=c99', '-O0', '-I' + str(cls.work),
                   str(cls.work / 'display.c'), str(cls.work / 'fonts.c'),
                   str(cls.work / 'run_display.c'), str(cls.work / 'ftoa.c'),
                   str(ROOT / 'tests/display_sim.c'), '-o', str(executable)]
        subprocess.run(command, check=True, capture_output=True, text=True)
        result = subprocess.run([str(executable)], cwd=cls.work, check=True, capture_output=True, text=True)
        print(result.stdout.strip())

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def frame(self, kind, index):
        return (self.work / f'{kind}_{index}.rgb565').read_bytes()

    def test_flow_field_uses_native_24x32_digit_glyphs(self):
        # This independent bitmap fixture catches incorrect bit order and enlarged old glyphs.
        glyphs = (ROOT / 'tests/fixtures/flow_digits_24x32.bitmap').read_bytes()
        for index, text in enumerate(RATE_TEXT):
            image = self.frame('full', index)
            x0 = (240 - 24 * len(text)) // 2
            for char_index, char in enumerate(text):
                glyph = '0123456789.-'.index(char)
                for y in range(32):
                    row = int.from_bytes(glyphs[glyph * 96 + y * 3:glyph * 96 + y * 3 + 3], 'big')
                    for x in range(24):
                        offset = ((98 + y) * 240 + x0 + char_index * 24 + x) * 2
                        expected = b'\xff\xff' if row & (1 << (23 - x)) else b'\0\0'
                        self.assertEqual(image[offset:offset + 2], expected,
                                         f'rate={text}, char={char}, x={x}, y={y}')

    def test_changed_digits_and_length_match_fresh_full_render(self):
        for index in range(len(RATE_TEXT)):
            self.assertEqual(self.frame('partial', index), self.frame('full', index), f'case {index}')

    def test_all_twelve_glyphs_use_native_pixels(self):
        glyphs = (ROOT / 'tests/fixtures/flow_digits_24x32.bitmap').read_bytes()
        image = self.frame('digits', 0)
        for glyph in range(12):
            x0 = glyph * 24 if glyph < 10 else (glyph - 10) * 24
            y0 = 0 if glyph < 10 else 40
            for y in range(32):
                row = int.from_bytes(glyphs[glyph * 96 + y * 3:glyph * 96 + y * 3 + 3], 'big')
                for x in range(24):
                    offset = ((y0 + y) * 240 + x0 + x) * 2
                    expected = b'\xff\xff' if row & (1 << (23 - x)) else b'\0\0'
                    self.assertEqual(image[offset:offset + 2], expected, f'glyph={glyph}, x={x}, y={y}')

    def test_existing_three_font_sizes_are_unchanged(self):
        for index, (width, height, scale) in enumerate(FONT_CASES):
            rows = glyph_rows(f'{width}x{height}')
            expected = bytearray(240 * 240 * 2)
            for i, char in enumerate('A0.9'):
                for y in range(height * scale):
                    row = rows[(ord(char) - 32) * height + y // scale]
                    for x in range(width * scale):
                        offset = (y * 240 + i * width * scale + x) * 2
                        if row & (0x8000 >> (x // scale)):
                            expected[offset:offset + 2] = b'\xff\xff'
            self.assertEqual(self.frame('legacy', index), bytes(expected), f'font {index}')

    def test_main_titles_use_native_letter_glyphs(self):
        glyphs = (ROOT / 'tests/fixtures/main_labels_24.bitmap').read_bytes()
        advances = dict(zip('FLOWTA', [15, 13, 19, 23, 15, 16]))
        image = self.frame('full', 5)
        for text, y0, width in [('FLOW', 49, 72), ('TOTAL', 153, 90)]:
            expected = bytearray(width * 24 * 2)
            x0 = 0
            for char in text:
                glyph = 'FLOWTA'.index(char)
                for y in range(24):
                    row = int.from_bytes(glyphs[glyph * 72 + y * 3:glyph * 72 + y * 3 + 3], 'big')
                    for x in range(advances[char]):
                        if row & (1 << (23 - x)):
                            offset = (y * width + x0 + x) * 2
                            expected[offset:offset + 2] = b'\xff\x07'
                x0 += advances[char]
            actual = b''.join(image[((y0 + y) * 240 + 8) * 2:((y0 + y) * 240 + 8 + width) * 2]
                              for y in range(24))
            self.assertEqual(actual, bytes(expected), text)

    def test_main_content_outside_changed_fonts_is_unchanged(self):
        # Mask only the approved font and unit changes; all other pixels must match.
        data = bytearray(self.frame('full', 5))
        for x, y, width, height in [(8, 83, 232, 51), (8, 49, 72, 24), (8, 153, 90, 24),
                                    (8, 8, 56, 20), (8, 213, 28, 20)]:
            for row in range(y, y + height):
                data[(row * 240 + x) * 2:(row * 240 + x + width) * 2] = bytes(width * 2)
        self.assertEqual(hashlib.sha256(data).hexdigest(),
                         '1816284dc16887f22d5838289d49afbe029b7acab4508fafb803355644049682')

    def test_main_cubic_units_use_a_raised_three(self):
        self.check_unit_pixels(self.frame('main', 0), 'm³/h', 8, 56, b'\xff\x07')
        self.check_unit_pixels(self.frame('main', 0), 'm³', 213, 28, b'\xff\xff')

    def test_other_main_units_are_unchanged(self):
        for index, flow, total in [(0, 'L/h', 'L'), (1, 'L/min', 'kg')]:
            image = self.frame('units', index)
            self.check_unit_pixels(image, flow, 8, 70, b'\xff\x07')
            self.check_unit_pixels(image, total, 213, 28, b'\xff\xff')

    def check_unit_pixels(self, image, text, y0, width, color):
        rows = glyph_rows('7x10')
        # Temperature starts at y=26 and covers the unit's final two blank rows.
        height = 18 if y0 == 8 else 20
        expected = bytearray(width * height * 2)
        x0 = 0
        for char in text:
            scale = 1 if char == '³' else 2
            code = ord('3' if char == '³' else char)
            for y in range(min(10 * scale, height)):
                row = rows[(code - 32) * 10 + y // scale]
                for x in range(7 * scale):
                    if row & (0x8000 >> (x // scale)):
                        offset = (y * width + x0 + x) * 2
                        expected[offset:offset + 2] = color
            x0 += 7 * scale
        actual = b''.join(image[((y0 + y) * 240 + 8) * 2:((y0 + y) * 240 + 8 + width) * 2]
                          for y in range(height))
        self.assertEqual(actual, bytes(expected), text)

    def test_auxiliary_page_is_unchanged(self):
        self.assertEqual(hashlib.sha256(self.frame('aux', 0)).hexdigest(),
                         'c492348c2c017442cd5b56c7c03b43200ebff369edb4c184d23b109c5fced7b1')


if __name__ == '__main__':
    unittest.main(verbosity=2)
