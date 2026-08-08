#pragma once

#include "lvgl.h"
#include "tbank_api.h"

// Builds the screen (title, price, status line, chart). Call once after
// display_init(), while holding display_lvgl_lock().
void ui_init(lv_display_t *disp);

// All ui_set_* functions call lv_* APIs and must be called while holding
// display_lvgl_lock()/display_lvgl_unlock().
void ui_set_status(const char *text);
void ui_set_price(double price, time_t updated_at);
void ui_set_chart(const tbank_candle_t *candles, int count);
