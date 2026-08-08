#include "market_task.h"

#include <stdbool.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "display.h"
#include "secrets.h"
#include "tbank_api.h"
#include "ui.h"

static const char *TAG = "market";

#define TASK_STACK_SIZE (6 * 1024)
#define TASK_PRIORITY    3

static void set_status(const char *text)
{
    ESP_LOGI(TAG, "%s", text);
    display_lvgl_lock();
    ui_set_status(text);
    display_lvgl_unlock();
}

static void market_task(void *arg)
{
    char figi[TBANK_FIGI_LEN] = {0};

    set_status("resolving " SECRET_INSTRUMENT_TICKER "...");
    while (tbank_find_figi(SECRET_INSTRUMENT_TICKER, figi, sizeof(figi)) != ESP_OK) {
        set_status("instrument lookup failed, retrying...");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    ESP_LOGI(TAG, "using figi=%s", figi);

    TickType_t last_price_poll = 0;
    TickType_t last_candles_poll = 0;
    bool first_loop = true;

    while (1) {
        TickType_t now = xTaskGetTickCount();

        if (first_loop ||
            (now - last_price_poll) >= pdMS_TO_TICKS(APP_PRICE_POLL_INTERVAL_MS)) {
            double price = 0.0;
            if (tbank_get_last_price(figi, &price) == ESP_OK) {
                display_lvgl_lock();
                ui_set_price(price, time(NULL));
                ui_set_status("ok");
                display_lvgl_unlock();
            } else {
                set_status("price fetch failed, will retry");
            }
            last_price_poll = now;
        }

        if (first_loop ||
            (now - last_candles_poll) >= pdMS_TO_TICKS(APP_CANDLES_POLL_INTERVAL_MS)) {
            static tbank_candle_t candles[APP_CANDLES_MAX_POINTS];
            int count = 0;
            if (tbank_get_candles(figi, candles, APP_CANDLES_MAX_POINTS, &count) == ESP_OK) {
                display_lvgl_lock();
                ui_set_chart(candles, count);
                display_lvgl_unlock();
            } else {
                ESP_LOGW(TAG, "candles fetch failed, keeping previous chart");
            }
            last_candles_poll = now;
        }

        first_loop = false;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void market_task_start(void)
{
    xTaskCreate(market_task, "market", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL);
}
