/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <math.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/interp.h"

#include "st7789_lcd.pio.h"
#include "image_rgb565.h"
#include "cartridge.h"

#include "TinyBit-lib/tinybit.h"

// Tested with the parts that have the height of 240 and 320
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240
#define IMAGE_SIZE 256
#define LOG_IMAGE_SIZE 8

#define PIN_DIN 0
#define PIN_CLK 1
#define PIN_CS 2
#define PIN_DC 3
#define PIN_RESET 4
#define PIN_BL 5

#define SERIAL_CLK_DIV 1.f

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PIO pio = pio0;
uint sm = 0;

struct TinyBitMemory tb_mem = {0};
bool button_state[TB_BUTTON_COUNT] = {0};

// Format: cmd length (including cmd byte), post delay in units of 5 ms, then cmd payload
// Note the delays have been shortened a little
static const uint8_t st7789_init_seq[] = {
        1, 20, 0x01,                        // Software reset
        1, 10, 0x11,                        // Exit sleep mode
        2, 2, 0x3a, 0x55,                   // Set colour mode to 16 bit
        2, 0, 0x36, 0x00,                   // Set MADCTL: row then column, refresh is bottom to top ????
        5, 0, 0x2a, 0x00, 0x00, SCREEN_WIDTH >> 8, SCREEN_WIDTH & 0xff,   // CASET: column addresses
        5, 0, 0x2b, 0x00, 0x00, SCREEN_HEIGHT >> 8, SCREEN_HEIGHT & 0xff, // RASET: row addresses
        1, 2, 0x21,                         // Inversion on, then 10 ms delay (supposedly a hack?)
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

void tinybit_poll_input(){
    // Placeholder: No input handling in this demo
}

int to_ms(){
    return to_ms_since_boot(get_absolute_time());
}

void render_frame(){
    printf("Rendering frame...\n");
    st7789_start_pixels(pio, sm);
    // interp0->accum[1] = 0; // Reset Y

    for (int y = 0; y < SCREEN_WIDTH; y++) {
        // interp0->accum[0] = 0; // Reset X
        
        for (int x = 0; x < SCREEN_WIDTH; x++) {

            if(x >= 128 || y >= 128) {
                // Send black pixel for out-of-bounds
                st7789_lcd_put(pio, sm, 0x00);
                st7789_lcd_put(pio, sm, 0x00);
                continue;
            }

            // // 1. Get scaled address from Interpolator
            // uint16_t* addr = (uint16_t*)tb_mem.display[y*SCREEN_WIDTH + x];
            // uint16_t raw = *addr;

            // // 2. Convert RGBA4444 to RGB565
            // // RRRR GGGG BBBB AAAA -> RRRRR GGGGGG BBBBB
            // uint16_t rgb = ((raw & 0xF000) >> 1) | // Red
            //                ((raw & 0x0F00) >> 3) | // Green
            //                ((raw & 0x00F0) >> 4);  // Blue

            // // 3. Stream directly to PIO FIFO
            // st7789_lcd_put(pio, sm, rgb >> 8);    // High byte
            // st7789_lcd_put(pio, sm, rgb & 0xFF);  // Low byte

            uint8_t r = (tb_mem.display[(y * TB_SCREEN_WIDTH + x) * 2 + 0] << 0) & 0xf0;
            uint8_t g = (tb_mem.display[(y * TB_SCREEN_WIDTH + x) * 2 + 0] << 4) & 0xf0;
            uint8_t b = (tb_mem.display[(y * TB_SCREEN_WIDTH + x) * 2 + 1] << 0) & 0xf0;
            uint8_t a = (tb_mem.display[(y * TB_SCREEN_WIDTH + x) * 2 + 1] << 4) & 0xf0;

            // Convert RGBA4444 to RGB565
            // RRRR GGGG BBBB AAAA -> RRRRR GGGGGG BBBBB
            uint16_t rgb = ((r & 0xF0) << 8) | // Red
                           ((g & 0xF0) << 3) | // Green
                           ((b & 0xF0) >> 3);  // Blue
            st7789_lcd_put(pio, sm, rgb >> 8);    // High byte
            st7789_lcd_put(pio, sm, rgb & 0xFF);
        }
        // Increment Y accumulator for next row
        // interp0->accum[1] += interp0->base[1];
    }
}

void log_printf(const char* msg){
    printf("%s", msg);
}

void sleep_ms_wrapper(int ms){
    sleep_ms(ms);
}

int main() {
    stdio_init_all();

    printf("TinyBit on ST7789 LCD Demo\n");

    PIO pio = pio0;
    uint sm = 0;
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

    tinybit_log_cb(log_printf);
    tinybit_gamecount_cb(NULL);
    tinybit_gameload_cb(NULL);    
    tinybit_render_cb(render_frame);
    tinybit_poll_input_cb(tinybit_poll_input);
    tinybit_sleep_cb(sleep_ms_wrapper);
    tinybit_get_ticks_ms_cb(to_ms);

    tinybit_init(&tb_mem, button_state);
    int result = tinybit_feed_cartridge(games_flappy_tb_png, games_flappy_tb_png_len);

    if(result < 0){
        while(1) printf("Failed to load cartridge!\n");
    }

    // // Lane 0: Horizontal scaling
    // interp_config c0 = interp_default_config();
    // interp_config_set_shift(&c0, 24);
    // interp_config_set_mask(&c0, 0, 6); // 0-127
    // interp_set_config(interp0, 0, &c0);

    // // Lane 1: Vertical scaling (multiplied by 128 for row offset)
    // interp_config c1 = interp_default_config();
    // interp_config_set_shift(&c1, 24);
    // interp_config_set_mask(&c1, 7, 13); // (0-127) << 7
    // interp_set_config(interp0, 1, &c1);

    // // Base2 is the memory start pointer
    // interp0->base[2] = (uintptr_t)tb_mem.display;
    
    // // Step: (128/240) * 2^24
    // uint32_t step = (uint32_t)((128.0f / 240.0f) * (1 << 24));
    // interp0->base[0] = step;
    // interp0->base[1] = step;

    tinybit_start();

    tinybit_loop();


    // Other SDKs: static image on screen, lame, boring
    // Raspberry Pi Pico SDK: spinning image on screen, bold, exciting

    // Lane 0 will be u coords (bits 8:1 of addr offset), lane 1 will be v
    // coords (bits 16:9 of addr offset), and we'll represent coords with
    // 16.16 fixed point. ACCUM0,1 will contain current coord, BASE0/1 will
    // contain increment vector, and BASE2 will contain image base pointer
#define UNIT_LSB 16
    interp_config lane0_cfg = interp_default_config();
    interp_config_set_shift(&lane0_cfg, UNIT_LSB - 1); // -1 because 2 bytes per pixel
    interp_config_set_mask(&lane0_cfg, 1, 1 + (LOG_IMAGE_SIZE - 1));
    interp_config_set_add_raw(&lane0_cfg, true); // Add full accumulator to base with each POP
    interp_config lane1_cfg = interp_default_config();
    interp_config_set_shift(&lane1_cfg, UNIT_LSB - (1 + LOG_IMAGE_SIZE));
    interp_config_set_mask(&lane1_cfg, 1 + LOG_IMAGE_SIZE, 1 + (2 * LOG_IMAGE_SIZE - 1));
    interp_config_set_add_raw(&lane1_cfg, true);

    interp_set_config(interp0, 0, &lane0_cfg);
    interp_set_config(interp0, 1, &lane1_cfg);
    interp0->base[2] = (uint32_t) image_240x240;

    float theta = 0.f;
    float theta_max = 2.f * (float) M_PI;
    while (1) {
        theta += 0.02f;
        if (theta > theta_max)
            theta -= theta_max;
        int32_t rotate[4] = {
                (int32_t) (cosf(theta) * (1 << UNIT_LSB)), (int32_t) (-sinf(theta) * (1 << UNIT_LSB)),
                (int32_t) (sinf(theta) * (1 << UNIT_LSB)), (int32_t) (cosf(theta) * (1 << UNIT_LSB))
        };
        interp0->base[0] = rotate[0];
        interp0->base[1] = rotate[2];
        st7789_start_pixels(pio, sm);
        printf("frame theta: %.2f radians\n", theta);
        for (int y = 0; y < SCREEN_HEIGHT; ++y) {
            interp0->accum[0] = rotate[1] * y;
            interp0->accum[1] = rotate[3] * y;
            for (int x = 0; x < SCREEN_WIDTH; ++x) {
                uint16_t colour = *(uint16_t *) (interp0->pop[2]);
                st7789_lcd_put(pio, sm, colour >> 8);
                st7789_lcd_put(pio, sm, colour & 0xff);
            }
        }
    }
}