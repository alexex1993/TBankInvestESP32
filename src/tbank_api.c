#include "tbank_api.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

#include "app_config.h"
#include "http_client_util.h"

static const char *TAG = "tbank_api";

// A protobuf MoneyValue/Quotation serializes as {"units": "<int64 as string>", "nano": <int32>}.
// Some gateways emit units as a JSON number instead of a string, so accept both.
static double parse_money(const cJSON *obj)
{
    if (!obj) {
        return 0.0;
    }
    const cJSON *units = cJSON_GetObjectItemCaseSensitive(obj, "units");
    const cJSON *nano = cJSON_GetObjectItemCaseSensitive(obj, "nano");

    double units_val = 0.0;
    if (cJSON_IsString(units) && units->valuestring) {
        units_val = atof(units->valuestring);
    } else if (cJSON_IsNumber(units)) {
        units_val = units->valuedouble;
    }

    double nano_val = 0.0;
    if (cJSON_IsNumber(nano)) {
        nano_val = nano->valuedouble;
    } else if (cJSON_IsString(nano) && nano->valuestring) {
        nano_val = atof(nano->valuestring);
    }

    return units_val + nano_val / 1e9;
}

// Parses "2026-08-08T10:00:00Z" / "...T10:00:00.123456789Z" as UTC.
// ESP-IDF's default TZ is UTC, so mktime() on a UTC-filled tm is correct here.
static time_t parse_rfc3339_utc(const char *s)
{
    struct tm tm = {0};
    int y, mo, d, h, mi, se;
    if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) {
        return 0;
    }
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = se;
    tm.tm_isdst = 0;
    return mktime(&tm);
}

static void format_rfc3339_utc(time_t t, char *out, size_t out_len)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, out_len, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

static bool ci_equal(const char *a, const char *b)
{
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

esp_err_t tbank_find_figi(const char *ticker, const char *class_hint,
                           char *figi_out, size_t figi_out_len)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "query", ticker);
    char *req_str = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    char url[192];
    snprintf(url, sizeof(url), "%s.InstrumentsService/FindInstrument", TBANK_REST_BASE_URL);

    char *resp_body = NULL;
    size_t resp_len = 0;
    esp_err_t err = tbank_http_post_json(url, req_str, &resp_body, &resp_len);
    free(req_str);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_Parse(resp_body);
    free(resp_body);
    if (!root) {
        ESP_LOGE(TAG, "FindInstrument: bad JSON response");
        return ESP_FAIL;
    }

    const cJSON *instruments = cJSON_GetObjectItemCaseSensitive(root, "instruments");
    const cJSON *best = NULL;
    const cJSON *first = NULL;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, instruments) {
        const cJSON *item_ticker = cJSON_GetObjectItemCaseSensitive(item, "ticker");
        const cJSON *item_class = cJSON_GetObjectItemCaseSensitive(item, "classCode");
        if (!first) {
            first = item;
        }
        if (cJSON_IsString(item_ticker) && ci_equal(item_ticker->valuestring, ticker)) {
            if (cJSON_IsString(item_class) && class_hint &&
                strstr(item_class->valuestring, class_hint) != NULL) {
                best = item;
                break;
            }
            if (!best) {
                best = item;
            }
        }
    }
    if (!best) {
        best = first;
    }

    esp_err_t result = ESP_FAIL;
    if (best) {
        const cJSON *figi = cJSON_GetObjectItemCaseSensitive(best, "figi");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(best, "name");
        if (cJSON_IsString(figi) && figi->valuestring[0] != '\0') {
            strlcpy(figi_out, figi->valuestring, figi_out_len);
            ESP_LOGI(TAG, "resolved '%s' -> figi=%s (%s)", ticker, figi_out,
                     cJSON_IsString(name) ? name->valuestring : "?");
            result = ESP_OK;
        }
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "FindInstrument: no match for ticker '%s'", ticker);
    }

    cJSON_Delete(root);
    return result;
}

esp_err_t tbank_get_last_price(const char *figi, double *price_out)
{
    cJSON *req = cJSON_CreateObject();
    cJSON *ids = cJSON_AddArrayToObject(req, "instrumentId");
    cJSON_AddItemToArray(ids, cJSON_CreateString(figi));
    char *req_str = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    char url[192];
    snprintf(url, sizeof(url), "%s.MarketDataService/GetLastPrices", TBANK_REST_BASE_URL);

    char *resp_body = NULL;
    size_t resp_len = 0;
    esp_err_t err = tbank_http_post_json(url, req_str, &resp_body, &resp_len);
    free(req_str);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_Parse(resp_body);
    free(resp_body);
    if (!root) {
        ESP_LOGE(TAG, "GetLastPrices: bad JSON response");
        return ESP_FAIL;
    }

    esp_err_t result = ESP_FAIL;
    const cJSON *prices = cJSON_GetObjectItemCaseSensitive(root, "lastPrices");
    const cJSON *first = cJSON_GetArrayItem(prices, 0);
    if (first) {
        const cJSON *price = cJSON_GetObjectItemCaseSensitive(first, "price");
        *price_out = parse_money(price);
        result = ESP_OK;
    } else {
        ESP_LOGW(TAG, "GetLastPrices: empty lastPrices array");
    }

    cJSON_Delete(root);
    return result;
}

esp_err_t tbank_get_minute_candles(const char *figi, int lookback_minutes,
                                    tbank_candle_t *out, int max_out, int *count_out)
{
    *count_out = 0;

    time_t now = time(NULL);
    time_t from = now - (time_t)lookback_minutes * 60;
    char from_str[32], to_str[32];
    format_rfc3339_utc(from, from_str, sizeof(from_str));
    format_rfc3339_utc(now, to_str, sizeof(to_str));

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "instrumentId", figi);
    cJSON_AddStringToObject(req, "from", from_str);
    cJSON_AddStringToObject(req, "to", to_str);
    cJSON_AddStringToObject(req, "interval", "CANDLE_INTERVAL_1_MIN");
    char *req_str = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    char url[192];
    snprintf(url, sizeof(url), "%s.MarketDataService/GetCandles", TBANK_REST_BASE_URL);

    char *resp_body = NULL;
    size_t resp_len = 0;
    esp_err_t err = tbank_http_post_json(url, req_str, &resp_body, &resp_len);
    free(req_str);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_Parse(resp_body);
    free(resp_body);
    if (!root) {
        ESP_LOGE(TAG, "GetCandles: bad JSON response");
        return ESP_FAIL;
    }

    const cJSON *candles = cJSON_GetObjectItemCaseSensitive(root, "candles");
    const cJSON *item = NULL;
    int n = 0;
    cJSON_ArrayForEach(item, candles) {
        if (n >= max_out) {
            break;
        }
        const cJSON *open = cJSON_GetObjectItemCaseSensitive(item, "open");
        const cJSON *high = cJSON_GetObjectItemCaseSensitive(item, "high");
        const cJSON *low = cJSON_GetObjectItemCaseSensitive(item, "low");
        const cJSON *close = cJSON_GetObjectItemCaseSensitive(item, "close");
        const cJSON *time_field = cJSON_GetObjectItemCaseSensitive(item, "time");
        if (!cJSON_IsString(time_field)) {
            continue;
        }
        out[n].time_unix = parse_rfc3339_utc(time_field->valuestring);
        out[n].open = parse_money(open);
        out[n].high = parse_money(high);
        out[n].low = parse_money(low);
        out[n].close = parse_money(close);
        n++;
    }
    *count_out = n;

    cJSON_Delete(root);
    if (n == 0) {
        ESP_LOGW(TAG, "GetCandles: no candles returned");
        return ESP_FAIL;
    }
    return ESP_OK;
}
