#ifndef I2S_H
#define I2S_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/pio.h"
#include "hardware/dma.h"

// I2S pin definitions
#define I2S_PIN_DIN     9
#define I2S_PIN_BCLK    8
#define I2S_PIN_LRCLK   7

// Audio configuration
#define I2S_SAMPLE_RATE     22000
#define I2S_BITS_PER_SAMPLE 16

// Initialize I2S peripheral with PIO and DMA
void i2s_init(void);

// Start I2S audio output
void i2s_start(void);

// Stop I2S audio output
void i2s_stop(void);

// Queue mono audio samples for playback (will be duplicated to both channels)
// buffer: pointer to int16_t mono samples
// sample_count: number of samples
void i2s_queue_mono_samples(int16_t* buffer, uint32_t sample_count);

// Check if the I2S driver is ready for more data
bool i2s_buffer_ready(void);

#endif // I2S_H
