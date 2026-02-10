#ifndef MAIN_H
#define MAIN_H

#include <stdbool.h>
#include "TinyBit-lib/tinybit.h"

// TinyBit memory and state
extern struct TinyBitMemory tb_mem;
extern bool button_state[TB_BUTTON_COUNT];

extern volatile bool frame_ready;
extern volatile bool audio_ready;

extern uint8_t temp_frame_buffer[TB_SCREEN_WIDTH * TB_SCREEN_HEIGHT * 2];
extern int16_t temp_audio_buffer[TB_AUDIO_FRAME_SAMPLES];

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
