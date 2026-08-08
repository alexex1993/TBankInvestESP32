#pragma once

// Starts the background task that resolves the SiU6 figi and then polls
// price/candles from the T-Bank Invest REST API, pushing updates into the
// LVGL UI. Call after display_init()+ui_init() and after WiFi/SNTP are up.
void market_task_start(void);
