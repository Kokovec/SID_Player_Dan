#pragma once
#include <stdint.h>
#include <stdbool.h>

#define TFT_W 160
#define TFT_H 128

#define COLOR_BLUE  0x001Fu
#define COLOR_WHITE 0xFFFFu
#define COLOR_CYAN  0x07FFu
#define COLOR_BLACK 0x0000u

void tft_init(void);
void tft_fill(uint16_t color);
void tft_fill_rect(int x, int y, int w, int h, uint16_t color);
void tft_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);
void tft_draw_string_centered(int y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);
void tft_show_track(const char *path, const char *title, const char *author,
                    const char *released, uint16_t songs, uint16_t start_song,
                    bool ntsc, uint32_t play_hz);
