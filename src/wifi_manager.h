#pragma once

#include "esp_err.h"

// Starts the WiFi driver in station mode and blocks until either an IP
// address is obtained or connection permanently fails after retries.
// Safe to call once from app_main before any networking is attempted.
esp_err_t wifi_manager_connect_blocking(void);
