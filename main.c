/**
 * Main application logic for TinyBit on Pico
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "main.h"
#include "cartridge.h"

#include "hw_config.h"
#include "f_util.h"
#include "ff.h"

struct TinyBitMemory tb_mem = {0};
bool button_state[TB_BUTTON_COUNT] = {0};

void tinybit_poll_input(void) {
    // Placeholder: No input handling in this demo
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

int main() {
    stdio_init_all();

    printf("TinyBit on ST7789 LCD Demo\n");

    sleep_ms(5000);

    // Initialize LCD display
    lcd_init_display();

    // Set up TinyBit callbacks
    tinybit_log_cb(log_printf);
    tinybit_gamecount_cb(NULL);
    tinybit_gameload_cb(NULL);
    tinybit_render_cb(render_frame);
    tinybit_poll_input_cb(tinybit_poll_input);
    tinybit_sleep_cb(sleep_ms_wrapper);
    tinybit_get_ticks_ms_cb(to_ms);

    // Initialize TinyBit and load cartridge
    tinybit_init(&tb_mem, button_state);

        // See FatFs - Generic FAT Filesystem Module, "Application Interface",
    // http://elm-chan.org/fsw/ff/00index_e.html
    FATFS fs;
    FRESULT fr = f_mount(&fs, "", 1);
    if (FR_OK != fr) {
        printf("f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
    }


    // Open a file and write to it
    FIL fil;
    const char* const filename = "flappy.png";
    fr = f_open(&fil, filename, FA_READ);
    if (FR_OK != fr && FR_EXIST != fr) {
        printf("f_open error: %s (%d)\n", FRESULT_str(fr), fr);
    }
    
    char buffer[256];
    UINT bytes_read;
    while(1) {
        fr = f_read(&fil, buffer, sizeof(buffer), &bytes_read);
        if (FR_OK != fr) {
            printf("f_read error: %s (%d)\n", FRESULT_str(fr), fr);
            break;
        }
        if (bytes_read == 0) {
            // End of file
            break;
        }
        // Process buffer data here (for demo, we just print the number of bytes read)
        tinybit_feed_cartridge(buffer, bytes_read);
        printf("Read %u bytes from %s\n", bytes_read, filename);
    }

    // Close the file
    fr = f_close(&fil);
    if (FR_OK != fr) {
        printf("f_close error: %s (%d)\n", FRESULT_str(fr), fr);
    }

    // Unmount the SD card
    f_unmount("");

    // int result = tinybit_feed_cartridge(games_flappy_tb_png, games_flappy_tb_png_len);

    // if (result < 0) {
    //     while (1) printf("Failed to load cartridge!\n");
    // }

    // Start game loop
    tinybit_start();
    tinybit_loop();
    return 0;
}
