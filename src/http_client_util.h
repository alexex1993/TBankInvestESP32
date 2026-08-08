#pragma once

#include <stddef.h>
#include "esp_err.h"

// POSTs a JSON body to `url` with `Authorization: Bearer <SECRET_TBANK_TOKEN>`
// and `Content-Type: application/json`, validating the server certificate
// against the embedded Russian Trusted Root CA.
//
// On ESP_OK, *out_body is a null-terminated heap buffer with the response
// body (caller must free() it) and *out_len is its length excluding the
// terminator.
esp_err_t tbank_http_post_json(const char *url, const char *json_body,
                                char **out_body, size_t *out_len);
