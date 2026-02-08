#include "i2s.h"
#include "i2s.pio.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include <string.h>

// PIO and state machine configuration
static PIO i2s_pio = pio1;  // Use PIO1 (PIO0 is used by LCD)
static uint i2s_sm = 0;
static uint i2s_pio_offset;

// DMA configuration
static int i2s_dma_channel = -1;
static dma_channel_config i2s_dma_config;

// Single conversion buffer (mono->stereo)
#define I2S_CONVERSION_BUFFER_SIZE (TB_AUDIO_FRAME_SAMPLES * 2)
static uint32_t conversion_buffer[I2S_CONVERSION_BUFFER_SIZE];

void i2s_out_program_init(PIO pio, uint sm, uint offset, uint din_pin, uint bclk_pin, uint sample_rate) {
    uint lrclk_pin = bclk_pin + 1; // LRCLK must be adjacent to BCLK
    
    pio_gpio_init(pio, din_pin);
    pio_gpio_init(pio, bclk_pin);
    pio_gpio_init(pio, lrclk_pin);

    pio_sm_set_consecutive_pindirs(pio, sm, din_pin, 1, true);
    pio_sm_set_consecutive_pindirs(pio, sm, bclk_pin, 1, true);
    pio_sm_set_consecutive_pindirs(pio, sm, lrclk_pin, 1, true);

    // Get default config
    pio_sm_config c = i2s_out_program_get_default_config(offset);
    sm_config_set_out_pins(&c, din_pin, 1);

    // Configure side-set for BCLK and LRCLK, must be adjacent and in ascending order
    sm_config_set_sideset_pin_base(&c, bclk_pin);

    // Configure output shift: shift left, autopull at 32 bits (one stereo sample)
    sm_config_set_out_shift(&c, false, true, 32);

    // Join FIFOs for TX only (8 entries instead of 4)
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    // Calculate clock divider for desired sample rate
    float div = (float)clock_get_hz(clk_sys) / (sample_rate * 32 * 2);
    sm_config_set_clkdiv(&c, div);

    // Initialize and enable state machine
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

void i2s_init(void) {
    // Add I2S program to PIO
    i2s_pio_offset = pio_add_program(i2s_pio, &i2s_out_program);

    // Initialize the I2S program
    i2s_out_program_init(
        i2s_pio,
        i2s_sm,
        i2s_pio_offset,
        I2S_PIN_DIN,
        I2S_PIN_BCLK,
        I2S_SAMPLE_RATE
    );

    // Claim a DMA channel
    i2s_dma_channel = dma_claim_unused_channel(true);

    // Configure DMA channel
    i2s_dma_config = dma_channel_get_default_config(i2s_dma_channel);
    channel_config_set_transfer_data_size(&i2s_dma_config, DMA_SIZE_32);
    channel_config_set_read_increment(&i2s_dma_config, true);
    channel_config_set_write_increment(&i2s_dma_config, false);
    channel_config_set_dreq(&i2s_dma_config, pio_get_dreq(i2s_pio, i2s_sm, true));

    memset(conversion_buffer, 0, sizeof(conversion_buffer));

    // Initially stop the state machine
    pio_sm_set_enabled(i2s_pio, i2s_sm, true);
}

void i2s_queue_mono_samples(int16_t* buffer, uint32_t sample_count) {
    if (i2s_dma_channel < 0 || !buffer || sample_count == 0) return;

    if (sample_count > I2S_CONVERSION_BUFFER_SIZE/2) {
        sample_count = I2S_CONVERSION_BUFFER_SIZE/2; // Limit to conversion buffer size
    }

    // Wait for previous transfer to finish
    // PIO FIFO (8 entries) bridges the gap during conversion
    dma_channel_wait_for_finish_blocking(i2s_dma_channel);

    // Convert mono to stereo
    for (uint32_t i = 0; i < sample_count; i++) {
        uint16_t sample = (uint16_t)buffer[i];
        conversion_buffer[i] = ((uint32_t)sample << 16) | sample;
    }

    // Start DMA transfer immediately
    dma_channel_configure(
        i2s_dma_channel,
        &i2s_dma_config,
        &i2s_pio->txf[i2s_sm],
        conversion_buffer,
        sample_count,
        true
    );
}
