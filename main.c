/**
 * Main application logic for TinyBit on Pico
 */

// #define SYS_CLK_MHZ 200
#define PICO_USE_FASTEST_SUPPORTED_CLOCK 1

#include <stdio.h>
#include <pico/stdlib.h>
#include <pico/multicore.h>
#include <hardware/clocks.h>
#include "main.h"
#include "cartridge.h"
#include <tusb.h>

#include "hw_config.h"
#include "f_util.h"
#include "ff.h"
#include "i2s.h"

struct TinyBitMemory tb_mem = {0};
bool button_state[TB_BUTTON_COUNT] = {0};

// Pre-allocated audio buffer (mono samples for one frame)
static int16_t audio_buffer[TB_AUDIO_FRAME_SAMPLES];

// Filesystem state (kept mounted for game loading)
static FATFS fs;
static bool fs_mounted = false;

// Count PNG files in root directory
int sd_gamecount(void) {
    if (!fs_mounted) return 0;

    DIR dir;
    FILINFO fno;
    int count = 0;

    FRESULT fr = f_opendir(&dir, "/");
    if (fr != FR_OK) return 0;

    while (1) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;

        // Skip directories
        if (fno.fattrib & AM_DIR) continue;

        // Check for .png extension (case insensitive)
        size_t len = strlen(fno.fname);
        if (len > 4) {
            const char* ext = &fno.fname[len - 4];
            if ((ext[0] == '.' || ext[0] == '.') &&
                (ext[1] == 'p' || ext[1] == 'P') &&
                (ext[2] == 'n' || ext[2] == 'N') &&
                (ext[3] == 'g' || ext[3] == 'G')) {
                count++;
            }
        }
    }

    f_closedir(&dir);
    return count;
}

// Load PNG game file by index
void sd_gameload(int index) {
    if (!fs_mounted) return;

    DIR dir;
    FILINFO fno;
    int count = 0;

    FRESULT fr = f_opendir(&dir, "/");
    if (fr != FR_OK) return;

    // Find the file at the given index
    while (1) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;

        if (fno.fattrib & AM_DIR) continue;

        size_t len = strlen(fno.fname);
        if (len > 4) {
            const char* ext = &fno.fname[len - 4];
            if ((ext[0] == '.') &&
                (ext[1] == 'p' || ext[1] == 'P') &&
                (ext[2] == 'n' || ext[2] == 'N') &&
                (ext[3] == 'g' || ext[3] == 'G')) {
                if (count == index) {
                    f_closedir(&dir);

                    // Load this file
                    FIL fil;
                    fr = f_open(&fil, fno.fname, FA_READ);
                    if (fr != FR_OK) {
                        printf("Failed to open: %s\n", fno.fname);
                        return;
                    }

                    printf("Loading: %s\n", fno.fname);

                    char buffer[256];
                    UINT bytes_read;
                    while (1) {
                        fr = f_read(&fil, buffer, sizeof(buffer), &bytes_read);
                        if (fr != FR_OK || bytes_read == 0) break;
                        tinybit_feed_cartridge((uint8_t*)buffer, bytes_read);
                    }

                    f_close(&fil);
                    return;
                }
                count++;
            }
        }
    }

    f_closedir(&dir);
}

void tinybit_poll_input(void) {
    // Update button states
    // Example: Read GPIO pins and update button_state array
    button_state[TB_BUTTON_A] = gpio_get(17); 
    button_state[TB_BUTTON_B] = gpio_get(16); 
    button_state[TB_BUTTON_UP] = gpio_get(21); 
    button_state[TB_BUTTON_DOWN] = gpio_get(19); 
    button_state[TB_BUTTON_LEFT] = gpio_get(18); 
    button_state[TB_BUTTON_RIGHT] = gpio_get(20); 
}

int to_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

void log_printf(const char* msg) {
    printf("%s", msg);
}

void sleep_ms_wrapper(int ms) {
    sleep_ms(ms);
}

// Audio queue callback - called from game loop after process_audio() fills the buffer
void audio_queue_handler(void) {
    // Only queue if I2S is ready (double-buffer not full)
    if (i2s_buffer_ready()) {
        i2s_queue_mono_samples(audio_buffer, TB_AUDIO_FRAME_SAMPLES);
    }
    // If not ready, this frame's audio is dropped (acceptable at 60fps)
}

int main() {
    stdio_init_all();

    if (!set_sys_clock_khz(200000, false))
      printf("system clock 200MHz failed\n");
    else
      printf("system clock now 200MHz\n");

    gpio_init(16);
    gpio_init(17);
    gpio_init(18);
    gpio_init(19);
    gpio_init(20);
    gpio_init(21);

    gpio_set_dir(16, GPIO_IN);
    gpio_set_dir(17, GPIO_IN);
    gpio_set_dir(18, GPIO_IN);
    gpio_set_dir(19, GPIO_IN);
    gpio_set_dir(20, GPIO_IN);
    gpio_set_dir(21, GPIO_IN);

    // Initialize LCD display
    lcd_init_display();

    // Initialize I2S audio output
    i2s_init();

    // Mount SD card filesystem (keep mounted for game loading)
    FRESULT fr = f_mount(&fs, "", 1);
    if (FR_OK != fr) {
        printf("f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
    } else {
        fs_mounted = true;
        printf("SD card mounted, found %d games\n", sd_gamecount());
    }

    // Set up TinyBit callbacks
    tinybit_log_cb(log_printf);
    tinybit_gamecount_cb(sd_gamecount);
    tinybit_gameload_cb(sd_gameload);
    tinybit_render_cb(render_frame);
    tinybit_poll_input_cb(tinybit_poll_input);
    tinybit_sleep_cb(sleep_ms_wrapper);
    tinybit_get_ticks_ms_cb(to_ms);
    tinybit_audio_queue_cb(audio_queue_handler);

    // Initialize TinyBit (starts game selector menu)
    tinybit_init(&tb_mem, button_state, audio_buffer);

    // Launch core1 for LCD output
    printf("Starting core1 for LCD rendering...\n");
    multicore_launch_core1(core1_lcd_loop);

    // Start I2S audio output
    i2s_start();

    // Start game loop on core0
    tinybit_start();
    tinybit_loop();
    return 0;
}
