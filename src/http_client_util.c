#include "http_client_util.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"

#include "root_ca_pem.h"
#include "secrets.h"

static const char *TAG = "http";

#define RESPONSE_INITIAL_CAP 2048
#define RESPONSE_MAX_CAP     (64 * 1024)

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    bool overflow;
} response_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    response_buf_t *resp = (response_buf_t *)evt->user_data;

    if (evt->event_id != HTTP_EVENT_ON_DATA || resp == NULL || evt->data_len <= 0) {
        return ESP_OK;
    }
    if (resp->overflow) {
        return ESP_OK;
    }

    size_t needed = resp->len + (size_t)evt->data_len + 1; // +1 for NUL
    if (needed > RESPONSE_MAX_CAP) {
        ESP_LOGE(TAG, "response too large, truncating");
        resp->overflow = true;
        return ESP_OK;
    }
    if (needed > resp->cap) {
        size_t new_cap = resp->cap ? resp->cap * 2 : RESPONSE_INITIAL_CAP;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        char *grown = realloc(resp->buf, new_cap);
        if (!grown) {
            ESP_LOGE(TAG, "out of memory growing response buffer");
            resp->overflow = true;
            return ESP_OK;
        }
        resp->buf = grown;
        resp->cap = new_cap;
    }
    memcpy(resp->buf + resp->len, evt->data, evt->data_len);
    resp->len += evt->data_len;
    resp->buf[resp->len] = '\0';
    return ESP_OK;
}

esp_err_t tbank_http_post_json(const char *url, const char *json_body,
                                char **out_body, size_t *out_len)
{
    response_buf_t resp = {0};

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .cert_pem = TBANK_ROOT_CA_PEM,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 15000,
        .buffer_size = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", SECRET_TBANK_TOKEN);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_body, (int)strlen(json_body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
        free(resp.buf);
        return err;
    }
    if (resp.overflow) {
        free(resp.buf);
        return ESP_ERR_NO_MEM;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "HTTP POST %s -> status %d, body: %s", url, status,
                 resp.buf ? resp.buf : "(empty)");
        free(resp.buf);
        return ESP_FAIL;
    }
    if (!resp.buf) {
        // Empty but successful response.
        resp.buf = calloc(1, 1);
    }

    *out_body = resp.buf;
    *out_len = resp.len;
    return ESP_OK;
}
