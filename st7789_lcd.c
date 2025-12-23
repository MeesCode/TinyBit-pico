/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"

#include "st7789_lcd.pio.h"
#include "main.h"

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240

#define PIN_DIN 0
#define PIN_CLK 1
#define PIN_CS 2
#define PIN_DC 3
#define PIN_RESET 4
#define PIN_BL 5

#define SERIAL_CLK_DIV 1.f

static PIO pio = pio0;
static uint sm = 0;

// Format: cmd length (including cmd byte), post delay in units of 5 ms, then cmd payload
static const uint8_t st7789_init_seq[] = {
        1, 20, 0x01,                        // Software reset
        1, 10, 0x11,                        // Exit sleep mode
        2, 2, 0x3a, 0x55,                   // Set colour mode to 16 bit
        2, 0, 0x36, 0x00,                   // Set MADCTL: row then column, refresh is bottom to top
        5, 0, 0x2a, 0x00, 0x00, SCREEN_WIDTH >> 8, SCREEN_WIDTH & 0xff,   // CASET: column addresses
        5, 0, 0x2b, 0x00, 0x00, SCREEN_HEIGHT >> 8, SCREEN_HEIGHT & 0xff, // RASET: row addresses
        1, 2, 0x21,                         // Inversion on, then 10 ms delay
        1, 2, 0x13,                         // Normal display on, then 10 ms delay
        1, 2, 0x29,                         // Main screen turn on, then wait 500 ms
        0                                   // Terminate list
};

static inline void lcd_set_dc_cs(bool dc, bool cs) {
    sleep_us(1);
    gpio_put_masked((1u << PIN_DC) | (1u << PIN_CS), !!dc << PIN_DC | !!cs << PIN_CS);
    sleep_us(1);
}

static inline void lcd_write_cmd(PIO pio, uint sm, const uint8_t *cmd, size_t count) {
    st7789_lcd_wait_idle(pio, sm);
    lcd_set_dc_cs(0, 0);
    st7789_lcd_put(pio, sm, *cmd++);
    if (count >= 2) {
        st7789_lcd_wait_idle(pio, sm);
        lcd_set_dc_cs(1, 0);
        for (size_t i = 0; i < count - 1; ++i)
            st7789_lcd_put(pio, sm, *cmd++);
    }
    st7789_lcd_wait_idle(pio, sm);
    lcd_set_dc_cs(1, 1);
}

static inline void lcd_init(PIO pio, uint sm, const uint8_t *init_seq) {
    const uint8_t *cmd = init_seq;
    while (*cmd) {
        lcd_write_cmd(pio, sm, cmd + 2, *cmd);
        sleep_ms(*(cmd + 1) * 5);
        cmd += *cmd + 2;
    }
}

static inline void st7789_start_pixels(PIO pio, uint sm) {
    uint8_t cmd = 0x2c; // RAMWR
    lcd_write_cmd(pio, sm, &cmd, 1);
    lcd_set_dc_cs(1, 0);
}

void lcd_init_display(void) {
    uint offset = pio_add_program(pio, &st7789_lcd_program);
    st7789_lcd_program_init(pio, sm, offset, PIN_DIN, PIN_CLK, SERIAL_CLK_DIV);

    gpio_init(PIN_CS);
    gpio_init(PIN_DC);
    gpio_init(PIN_RESET);
    gpio_init(PIN_BL);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_set_dir(PIN_RESET, GPIO_OUT);
    gpio_set_dir(PIN_BL, GPIO_OUT);

    gpio_put(PIN_CS, 1);
    gpio_put(PIN_RESET, 1);
    lcd_init(pio, sm, st7789_init_seq);
    gpio_put(PIN_BL, 1);
}

void render_frame(void) {
    st7789_start_pixels(pio, sm);

    for (int y = 0; y < SCREEN_WIDTH; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {

            if (x >= 120 || y >= 120) {
                st7789_lcd_put(pio, sm, 0x00);
                st7789_lcd_put(pio, sm, 0x00);
                continue;
            }

            size_t addr = ((y * TB_SCREEN_WIDTH + x) * 2);

            uint8_t r = (tb_mem.display[addr + 0] << 0) & 0xf0;
            uint8_t g = (tb_mem.display[addr + 0] << 4) & 0xf0;
            uint8_t b = (tb_mem.display[addr + 1] << 0) & 0xf0;

            // Convert RGBA4444 to RGB565
            uint16_t rgb = ((r & 0xF0) << 8) | // Red
                           ((g & 0xF0) << 3) | // Green
                           ((b & 0xF0) >> 3);  // Blue
            st7789_lcd_put(pio, sm, rgb >> 8);    // High byte
            st7789_lcd_put(pio, sm, rgb & 0xFF);
        }
    }
}
