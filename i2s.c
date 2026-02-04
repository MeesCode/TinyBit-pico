#include "i2s.h"
#include "i2s.pio.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include <string.h>

// PIO and state machine configuration
static PIO i2s_pio = pio1;  // Use PIO1 (PIO0 is used by LCD)
static uint i2s_sm = 0;
static uint i2s_pio_offset;

// DMA configuration
static int i2s_dma_channel = -1;
static dma_channel_config i2s_dma_config;

// Stereo conversion buffer (mono -> stereo with same sample on both channels)
// Max size based on TB_AUDIO_FRAME_SAMPLES (22000/60 = ~367 samples)
#define I2S_CONVERSION_BUFFER_SIZE 512
static uint32_t conversion_buffer[I2S_CONVERSION_BUFFER_SIZE];
static uint32_t conversion_buffer_count = 0;

// DMA interrupt handler - restart playback when buffer completes
static void i2s_dma_irq_handler(void) {
    if (dma_channel_get_irq0_status(i2s_dma_channel)) {
        dma_channel_acknowledge_irq0(i2s_dma_channel);

        // Restart DMA for continuous playback
        if (conversion_buffer_count > 0) {
            dma_channel_set_read_addr(i2s_dma_channel, conversion_buffer, true);
        }
    }
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
        I2S_PIN_LRCLK,
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

    // Set up DMA interrupt
    dma_channel_set_irq0_enabled(i2s_dma_channel, true);
    irq_set_exclusive_handler(DMA_IRQ_0, i2s_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // Clear conversion buffer
    memset(conversion_buffer, 0, sizeof(conversion_buffer));

    // Initially stop the state machine
    pio_sm_set_enabled(i2s_pio, i2s_sm, false);
}

void i2s_start(void) {
    pio_sm_set_enabled(i2s_pio, i2s_sm, true);
}

void i2s_stop(void) {
    pio_sm_set_enabled(i2s_pio, i2s_sm, false);
    if (i2s_dma_channel >= 0) {
        dma_channel_abort(i2s_dma_channel);
    }
}

void i2s_queue_mono_samples(int16_t* buffer, uint32_t sample_count) {
    if (i2s_dma_channel < 0 || !buffer || sample_count == 0) return;

    // Limit to conversion buffer size
    if (sample_count > I2S_CONVERSION_BUFFER_SIZE) {
        sample_count = I2S_CONVERSION_BUFFER_SIZE;
    }

    // Convert mono to stereo: duplicate each sample to both L and R channels
    // I2S format: left in upper 16 bits, right in lower 16 bits
    for (uint32_t i = 0; i < sample_count; i++) {
        uint16_t sample = (uint16_t)buffer[i];
        conversion_buffer[i] = ((uint32_t)sample << 16) | sample;
    }
    conversion_buffer_count = sample_count;

    // Configure and start DMA transfer
    dma_channel_configure(
        i2s_dma_channel,
        &i2s_dma_config,
        &i2s_pio->txf[i2s_sm],  // Write to PIO TX FIFO
        conversion_buffer,       // Read from conversion buffer
        sample_count,            // Number of 32-bit transfers
        true                     // Start immediately
    );
}

bool i2s_buffer_ready(void) {
    return !dma_channel_is_busy(i2s_dma_channel);
}
