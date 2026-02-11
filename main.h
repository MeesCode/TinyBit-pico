#ifndef MAIN_H
#define MAIN_H

#include <stdbool.h>
#include "TinyBit-lib/tinybit.h"
#include "TinyBit-lib/cartridge.h"
#include "TinyBit-lib/memory.h"

// TinyBit memory and state
extern struct TinyBitMemory tb_mem;
extern uint8_t frame_buffer_copy[TB_SCREEN_WIDTH * TB_SCREEN_HEIGHT * 2];

// Callback functions for TinyBit
void tinybit_poll_input(void);
int to_ms(void);
void log_printf(const char* msg);
void sleep_ms_wrapper(int ms);

// LCD functions (implemented in st7789_lcd.c)
void lcd_init_display(void);
void render_frame(void);
void core1_lcd_loop(void);

#endif // MAIN_H
