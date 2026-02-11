/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/interp.h"
#include "hardware/dma.h"

#include "st7789_lcd.pio.h"
#include "main.h"

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240

#define RENDER_WIDTH 128
#define RENDER_HEIGHT 128

// Fixed-point fractional bits for scaling
#define FRAC_BITS 16

// Scaling factor: RENDER_WIDTH / SCREEN_WIDTH in fixed-point
// 128/240 = 0.5333... -> (128 << 16) / 240 = 34952
#define SCALE_X ((RENDER_WIDTH << FRAC_BITS) / SCREEN_WIDTH)
#define SCALE_Y ((RENDER_HEIGHT << FRAC_BITS) / SCREEN_HEIGHT)

// Double buffer for DMA transfers - each scanline is SCREEN_WIDTH pixels * 2 bytes
static uint8_t scanline_buf[2][SCREEN_WIDTH * 2];
static int dma_chan;
static dma_channel_config dma_cfg;

// Double buffer for frame data (128x128 RGBA4444 = 32KB each)
static volatile int display_buffer_idx = 0;  // Buffer being displayed by core1
static volatile int render_buffer_idx = 1;   // Buffer being rendered to by core0

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

static inline void st7789_lcd_program_init(PIO pio, uint sm, uint offset, uint data_pin, uint clk_pin, float clk_div) {
    pio_gpio_init(pio, data_pin);
    pio_gpio_init(pio, clk_pin);
    pio_sm_set_consecutive_pindirs(pio, sm, data_pin, 1, true);
    pio_sm_set_consecutive_pindirs(pio, sm, clk_pin, 1, true);
    pio_sm_config c = st7789_lcd_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, clk_pin);
    sm_config_set_out_pins(&c, data_pin, 1);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, clk_div);
    sm_config_set_out_shift(&c, false, true, 8);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

// Making use of the narrow store replication behaviour on RP2040 to get the
// data left-justified (as we are using shift-to-left to get MSB-first serial)

static inline void st7789_lcd_put(PIO pio, uint sm, uint8_t x) {
    while (pio_sm_is_tx_fifo_full(pio, sm))
        ;
    *(volatile uint8_t*)&pio->txf[sm] = x;
}

// SM is done when it stalls on an empty FIFO

static inline void st7789_lcd_wait_idle(PIO pio, uint sm) {
    uint32_t sm_stall_mask = 1u << (sm + PIO_FDEBUG_TXSTALL_LSB);
    pio->fdebug = sm_stall_mask;
    while (!(pio->fdebug & sm_stall_mask))
        ;
}

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

    // Initialize DMA channel for scanline transfers
    dma_chan = dma_claim_unused_channel(true);
    dma_cfg = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_8);
    channel_config_set_dreq(&dma_cfg, pio_get_dreq(pio, sm, true));
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
}

static inline void build_scanline_from_buffer(uint8_t *dest, const uint8_t *src_buffer, uint32_t src_y) {
    const uint16_t *row_base = (const uint16_t *)&src_buffer[src_y * RENDER_WIDTH * 2];

    interp0->accum[0] = 0;
    interp0->base[0] = SCALE_X;

    for (int x = 0; x < SCREEN_WIDTH; x++) {
        uint32_t src_x = interp0->accum[0] >> FRAC_BITS;
        (void)interp0->pop[0];

        uint16_t pixel = row_base[src_x];

        uint8_t r = (pixel >> 0) & 0xf0;
        uint8_t g = (pixel << 4) & 0xf0;
        uint8_t b = (pixel >> 8) & 0xf0;

        uint16_t rgb565 = ((r & 0xF0) << 8) |
                          ((g & 0xF0) << 3) |
                          ((b & 0xF0) >> 3);

        dest[x * 2 + 0] = rgb565 >> 8;
        dest[x * 2 + 1] = rgb565 & 0xFF;
    }
}

// Send frame buffer to LCD with scanline double-buffering
void send_frame_to_lcd() {

    st7789_start_pixels(pio, sm);

    interp_config cfg = interp_default_config();
    interp_config_set_add_raw(&cfg, true);
    interp_set_config(interp0, 0, &cfg);

    int current_buf = 0;
    uint32_t src_y = 0;
    build_scanline_from_buffer(scanline_buf[current_buf], frame_buffer_copy, src_y);

    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        dma_channel_configure(
            dma_chan,
            &dma_cfg,
            &pio->txf[sm],
            scanline_buf[current_buf],
            SCREEN_WIDTH * 2,
            true
        );

        current_buf = 1 - current_buf;

        if (y < SCREEN_HEIGHT - 1) {
            src_y = ((y + 1) * SCALE_Y) >> FRAC_BITS;
            build_scanline_from_buffer(scanline_buf[current_buf], frame_buffer_copy, src_y);
        }

        dma_channel_wait_for_finish_blocking(dma_chan);
    }
}

// Signal frame ready - non-blocking for Lua
void render_frame(void) {
    // Copy to render buffer
    memcpy(frame_buffer_copy, tb_mem.display, RENDER_WIDTH * RENDER_HEIGHT * 2);

    // Signal and return immediately
    frame_ready = true;
}