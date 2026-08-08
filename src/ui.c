#include "ui.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include "app_config.h"
#include "secrets.h"

#define CHART_W        (BOARD_LCD_H_RES - 12)
#define CHART_H        150
#define CHART_PAD      2
#define CHART_CONTENT_W (CHART_W - 2 * CHART_PAD)
#define CHART_CONTENT_H (CHART_H - 2 * CHART_PAD)

static lv_obj_t *s_price_label;
static lv_obj_t *s_updated_label;
static lv_obj_t *s_status_label;

static bool s_use_candles;

// Line mode.
static lv_obj_t *s_chart;
static lv_chart_series_t *s_chart_series;

// Candlestick mode: a plain container holding a fixed pool of wick/body
// rectangles, repositioned/resized/recolored on every ui_set_chart() call
// instead of being created and destroyed (avoids heap churn on-device).
static lv_obj_t *s_candle_area;
static lv_obj_t *s_candle_wicks[APP_CANDLES_MAX_POINTS];
static lv_obj_t *s_candle_bodies[APP_CANDLES_MAX_POINTS];

void ui_init(lv_display_t *disp)
{
    s_use_candles = (strcmp(SECRET_CHART_TYPE, "line") != 0);

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 4, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, APP_INSTRUMENT_LABEL);
    lv_obj_set_style_text_color(title, lv_color_hex(0x8899aa), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    s_price_label = lv_label_create(scr);
    lv_label_set_text(s_price_label, "---");
    lv_obj_set_style_text_color(s_price_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_price_label, &lv_font_montserrat_36, 0);

    s_updated_label = lv_label_create(scr);
    lv_label_set_text(s_updated_label, "waiting for data...");
    lv_obj_set_style_text_color(s_updated_label, lv_color_hex(0x8899aa), 0);
    lv_obj_set_style_text_font(s_updated_label, &lv_font_montserrat_14, 0);

    lv_obj_t *chart_title = lv_label_create(scr);
    char chart_title_buf[24];
    snprintf(chart_title_buf, sizeof(chart_title_buf), "last %dm", APP_CANDLES_LOOKBACK_MINUTES);
    lv_label_set_text(chart_title, chart_title_buf);
    lv_obj_set_style_text_color(chart_title, lv_color_hex(0x556677), 0);
    lv_obj_set_style_text_font(chart_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(chart_title, 6, 0);

    if (s_use_candles) {
        s_candle_area = lv_obj_create(scr);
        lv_obj_set_size(s_candle_area, CHART_W, CHART_H);
        lv_obj_set_style_bg_color(s_candle_area, lv_color_hex(0x101418), 0);
        lv_obj_set_style_border_width(s_candle_area, 0, 0);
        lv_obj_set_style_radius(s_candle_area, 0, 0);
        lv_obj_set_style_pad_all(s_candle_area, CHART_PAD, 0);
        lv_obj_clear_flag(s_candle_area, LV_OBJ_FLAG_SCROLLABLE);

        for (int i = 0; i < APP_CANDLES_MAX_POINTS; i++) {
            lv_obj_t *wick = lv_obj_create(s_candle_area);
            lv_obj_remove_style_all(wick);
            lv_obj_set_style_bg_opa(wick, LV_OPA_COVER, 0);
            lv_obj_add_flag(wick, LV_OBJ_FLAG_HIDDEN);
            s_candle_wicks[i] = wick;

            lv_obj_t *body = lv_obj_create(s_candle_area);
            lv_obj_remove_style_all(body);
            lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
            lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
            s_candle_bodies[i] = body;
        }
    } else {
        s_chart = lv_chart_create(scr);
        lv_obj_set_size(s_chart, CHART_W, CHART_H);
        lv_obj_set_style_bg_color(s_chart, lv_color_hex(0x101418), 0);
        lv_obj_set_style_border_width(s_chart, 0, 0);
        lv_obj_set_style_pad_all(s_chart, CHART_PAD, 0);
        lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
        lv_chart_set_div_line_count(s_chart, 3, 0);
        lv_chart_set_point_count(s_chart, APP_CANDLES_MAX_POINTS);
        s_chart_series = lv_chart_add_series(s_chart, lv_color_hex(0x00e676), LV_CHART_AXIS_PRIMARY_Y);
    }

    s_status_label = lv_label_create(scr);
    lv_label_set_text(s_status_label, "starting...");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x556677), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(s_status_label, 4, 0);
}

void ui_set_status(const char *text)
{
    if (s_status_label) {
        lv_label_set_text(s_status_label, text);
    }
}

void ui_set_price(double price, time_t updated_at)
{
    if (!s_price_label) {
        return;
    }
    char buf[32];
    if (fabs(price) >= 1000.0) {
        snprintf(buf, sizeof(buf), "%.0f", price);
    } else {
        snprintf(buf, sizeof(buf), "%.2f", price);
    }
    lv_label_set_text(s_price_label, buf);

    // Device clock is UTC; shift to Moscow time (UTC+3, no DST) for display.
    time_t msk_time = updated_at + 3 * 3600;
    struct tm tm;
    gmtime_r(&msk_time, &tm);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S MSK", &tm);
    lv_label_set_text(s_updated_label, time_buf);
}

static void render_line_chart(const tbank_candle_t *candles, int count)
{
    if (!s_chart || !s_chart_series) {
        return;
    }

    double min = candles[0].close;
    double max = candles[0].close;
    for (int i = 1; i < count; i++) {
        if (candles[i].close < min) min = candles[i].close;
        if (candles[i].close > max) max = candles[i].close;
    }
    if (max - min < 1.0) {
        // Flat/near-flat series: keep a little headroom so the line isn't
        // pinned to an axis.
        double pad = (max - min < 0.001) ? MAX(1.0, fabs(max) * 0.001) : 1.0;
        min -= pad;
        max += pad;
    }

    lv_chart_set_point_count(s_chart, count);
    for (int i = 0; i < count; i++) {
        lv_chart_set_value_by_id(s_chart, s_chart_series, i, (int32_t)lround(candles[i].close));
    }
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, (int32_t)floor(min), (int32_t)ceil(max));
    lv_chart_refresh(s_chart);
}

static void render_candlesticks(const tbank_candle_t *candles, int count)
{
    if (!s_candle_area) {
        return;
    }

    double min = candles[0].low;
    double max = candles[0].high;
    for (int i = 1; i < count; i++) {
        if (candles[i].low < min) min = candles[i].low;
        if (candles[i].high > max) max = candles[i].high;
    }
    if (max - min < 1.0) {
        double pad = (max - min < 0.001) ? MAX(1.0, fabs(max) * 0.001) : 1.0;
        min -= pad;
        max += pad;
    }

    int32_t slot_w = MAX(2, CHART_CONTENT_W / count);
    int32_t body_w = MAX(2, slot_w - 2);

    for (int i = 0; i < count; i++) {
        double o = candles[i].open;
        double c = candles[i].close;
        double h = candles[i].high;
        double l = candles[i].low;

        int32_t y_open = (int32_t)lround(CHART_CONTENT_H - (o - min) / (max - min) * CHART_CONTENT_H);
        int32_t y_close = (int32_t)lround(CHART_CONTENT_H - (c - min) / (max - min) * CHART_CONTENT_H);
        int32_t y_high = (int32_t)lround(CHART_CONTENT_H - (h - min) / (max - min) * CHART_CONTENT_H);
        int32_t y_low = (int32_t)lround(CHART_CONTENT_H - (l - min) / (max - min) * CHART_CONTENT_H);

        int32_t slot_x = i * slot_w;
        int32_t cx = slot_x + slot_w / 2;

        lv_color_t color = (c >= o) ? lv_color_hex(0x00e676) : lv_color_hex(0xff5252);

        lv_obj_t *wick = s_candle_wicks[i];
        lv_obj_set_style_bg_color(wick, color, 0);
        lv_obj_set_pos(wick, cx, y_high);
        lv_obj_set_size(wick, 1, MAX(1, y_low - y_high));
        lv_obj_clear_flag(wick, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *body = s_candle_bodies[i];
        lv_obj_set_style_bg_color(body, color, 0);
        int32_t body_top = MIN(y_open, y_close);
        int32_t body_h = MAX(2, MAX(y_open, y_close) - MIN(y_open, y_close));
        lv_obj_set_pos(body, slot_x + (slot_w - body_w) / 2, body_top);
        lv_obj_set_size(body, body_w, body_h);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = count; i < APP_CANDLES_MAX_POINTS; i++) {
        lv_obj_add_flag(s_candle_wicks[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_candle_bodies[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_set_chart(const tbank_candle_t *candles, int count)
{
    if (count <= 0) {
        return;
    }
    if (count > APP_CANDLES_MAX_POINTS) {
        count = APP_CANDLES_MAX_POINTS;
    }

    if (s_use_candles) {
        render_candlesticks(candles, count);
    } else {
        render_line_chart(candles, count);
    }
}
