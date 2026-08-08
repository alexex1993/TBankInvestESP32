#pragma once

#include <stddef.h>
#include <time.h>
#include "esp_err.h"

#define TBANK_FIGI_LEN 32

typedef struct {
    time_t time_unix;
    double open;
    double high;
    double low;
    double close;
} tbank_candle_t;

// Resolves `ticker` to a figi via InstrumentsService/FindInstrument (first
// API-tradable match wins). figi_out must be at least TBANK_FIGI_LEN bytes.
esp_err_t tbank_find_figi(const char *ticker, char *figi_out, size_t figi_out_len);

// Fetches the last traded price for `figi` via MarketDataService/GetLastPrices.
esp_err_t tbank_get_last_price(const char *figi, double *price_out);

// Fetches up to `max_out` candles for `figi` via MarketDataService/GetCandles,
// using the bucket size configured via SECRET_CANDLE_INTERVAL (CANDLE_INTERVAL
// in .env) and covering a lookback of `max_out` buckets. *count_out is set to
// the number of candles actually returned, oldest first.
esp_err_t tbank_get_candles(const char *figi,
                             tbank_candle_t *out, int max_out, int *count_out);
