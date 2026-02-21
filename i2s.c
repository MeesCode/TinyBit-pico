#include "i2s.h"
#include "i2s.pio.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include <string.h>

#include "main.h"

// PIO and state machine configuration
static PIO i2s_pio = pio1;  // Use PIO1 (PIO0 is used by LCD)
static uint i2s_sm = 0;
static uint i2s_pio_offset;

// DMA configuration
static int i2s_dma_channel = -1;
static dma_channel_config i2s_dma_config;

// Double-buffering for DMA (ping-pong buffers)
// Max size based on TB_AUDIO_FRAME_SAMPLES (22000/60 = ~367 samples)
static uint32_t conversion_buffer_a[TB_AUDIO_FRAME_SAMPLES];
static uint32_t conversion_buffer_b[TB_AUDIO_FRAME_SAMPLES];
static uint32_t* active_dma_buffer = conversion_buffer_a;  // Buffer DMA reads from
static uint32_t* fill_buffer = conversion_buffer_b;        // Buffer CPU writes to
static volatile bool cpu_buffer_full = false;             // New data ready to swap
static uint32_t current_sample_count = 0;

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

// DMA interrupt handler - swap buffers when transfer completes
static void i2s_dma_irq_handler(void) {
    dma_channel_acknowledge_irq0(i2s_dma_channel);

    // Swap buffers
    uint32_t* temp = active_dma_buffer;
    active_dma_buffer = fill_buffer;
    fill_buffer = temp;
    cpu_buffer_full = false;

    // Restart DMA with fresh buffer
    dma_channel_set_read_addr(i2s_dma_channel, active_dma_buffer, true);
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

    // Set up DMA interrupt
    dma_channel_set_irq0_enabled(i2s_dma_channel, true);
    irq_set_exclusive_handler(DMA_IRQ_0, i2s_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // Clear both conversion buffers
    memset(conversion_buffer_a, 0, sizeof(conversion_buffer_a));
    memset(conversion_buffer_b, 0, sizeof(conversion_buffer_b));

    // Initially stop the state machine
    pio_sm_set_enabled(i2s_pio, i2s_sm, true);
}

void i2s_queue_samples() {

    // wait for second buffer to be free if we're still processing the previous one
    // since this task runs in the main loop, fps is limited to audio playback
    while(cpu_buffer_full) {
        tight_loop_contents(); 
    }

    // Convert mono to stereo into the fill buffer
    // I2S format: left in upper 16 bits, right in lower 16 bits
    for (uint32_t i = 0; i < TB_AUDIO_FRAME_SAMPLES; i++) {
        uint16_t sample = (uint16_t)tinybit_memory->audio_buffer[i];
        fill_buffer[i] = ((uint32_t)sample << 16) | sample;
    }

    // Check if DMA is running
    bool dma_busy = dma_channel_is_busy(i2s_dma_channel);

    // Mark new buffer as ready
    cpu_buffer_full = true;

    // If DMA is not running, swap and start immediately
    if (!dma_busy) {
        uint32_t* temp = active_dma_buffer;
        active_dma_buffer = fill_buffer;
        fill_buffer = temp;
        cpu_buffer_full = false;

        dma_channel_configure(
            i2s_dma_channel,
            &i2s_dma_config,
            &i2s_pio->txf[i2s_sm],
            active_dma_buffer,
            TB_AUDIO_FRAME_SAMPLES,
            true
        );
    }
    // If DMA is running, interrupt handler will swap when current transfer completes
}