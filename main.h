#ifndef MAIN_H
#define MAIN_H

#include <stdbool.h>
#include "TinyBit-lib/tinybit.h"

// TinyBit memory and state
extern struct TinyBitMemory tb_mem;
extern bool button_state[TB_BUTTON_COUNT];

// Callback functions for TinyBit
void tinybit_poll_input(void);
int to_ms(void);
void log_printf(const char* msg);
void sleep_ms_wrapper(int ms);

// LCD functions (implemented in st7789_lcd.c)
void lcd_init_display(void);
void render_frame(void);

#endif // MAIN_H
