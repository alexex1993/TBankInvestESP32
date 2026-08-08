# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for a Waveshare **ESP32-C6-LCD-1.47** board (ESP32-C6-WROOM-1, 4 MB flash, 172x320
ST7789 LCD on SPI2). It connects to WiFi, syncs time via SNTP, resolves a configured ticker
(default: `SiU6`, a MOEX USD/RUB futures contract) to a `figi` via the T-Bank Invest API, then
polls the last price and hourly candles and renders them on the LCD with LVGL. Built with
PlatformIO on the `espidf` framework.

`tbank-invest-api-guide.md` at the repo root is a general reference for the T-Bank Invest API
(Python SDK, REST gateway, data model). This firmware only uses the plain REST/JSON gateway
directly over `esp_http_client` — there is no gRPC or SDK usage on-device; consult that guide
for the semantics of the endpoints being called (money/quotation encoding, instrument IDs,
candle intervals, etc).

## Build, flash, monitor

Standard PlatformIO commands, run from the repo root (env name is `esp32-c6-lcd-1_47`):

```bash
pio run                 # build
pio run -t upload       # build + flash
pio run -t upload -t monitor   # flash and open serial monitor
pio device monitor      # serial monitor only (115200 baud, exception decoder enabled)
pio run -t clean        # clean build artifacts
```

There is no test suite (`test/` only has PlatformIO's placeholder README) and no lint config —
don't invent test/lint commands.

### Secrets

Every build runs `tools/gen_secrets.py` (wired in via `platformio.ini`'s `extra_scripts`)
*before* compiling. It reads `.env` at the repo root and generates `src/secrets.h` with
`SECRET_TBANK_TOKEN`, `SECRET_WIFI_SSID`, `SECRET_WIFI_PASSWORD` as C string macros. Both
`.env` and `src/secrets.h` are gitignored and must never be committed. To build locally, copy
`example.env` to `.env` and fill in real values; the build fails fast with a clear message if
any required key is missing.

## Architecture

Startup sequence, all orchestrated from `src/main.c`:

1. `nvs_flash_init()` (erase+reinit on version mismatch)
2. `display_init()` — brings up SPI2, the ST7789 panel via `esp_lcd`, and LVGL; starts the
   LVGL timer-handler task. Returns the `lv_display_t*` used to build the UI.
3. `ui_init(disp)` — builds the static screen layout (title, price label, status line, chart).
4. `wifi_manager_connect_blocking()` — blocks until WiFi is up or retries are exhausted
   (reboots after a delay on failure).
5. SNTP sync (`esp_netif_sntp_*`, best-effort — continues with unsynced clock on timeout).
6. `market_task_start()` — spawns the long-running FreeRTOS task that does everything else.

### The market task (`src/market_task.c`)

A single background task, once per boot:
- Resolves `APP_INSTRUMENT_TICKER` (`src/app_config.h`) to a `figi` via
  `tbank_find_figi()`, retrying every 10s until it succeeds.
- Then loops forever: polls last price every `APP_PRICE_POLL_INTERVAL_MS` and hourly candles
  every `APP_CANDLES_POLL_INTERVAL_MS` (both defined in `app_config.h`), pushing results into
  the UI. A failed candle fetch keeps the previous chart rather than clearing it.

### LVGL thread safety

LVGL is single-threaded. `display_lvgl_lock()`/`display_lvgl_unlock()` (`src/display.c`, a
`_lock_t` mutex) must wrap *every* `lv_*` call made from outside the LVGL task — this includes
all `ui_set_*` calls from `market_task` and `main.c`. `ui.c`'s public functions assume the
caller already holds the lock; they don't take it themselves.

### T-Bank API client (`src/tbank_api.c` + `src/http_client_util.c`)

- `http_client_util` is the transport: POSTs JSON to a URL with a Bearer token
  (`SECRET_TBANK_TOKEN` from generated `secrets.h`) and validates the server cert against the
  embedded **Russian Trusted Root CA** (`src/root_ca_pem.h`, generated from
  `src/russian_trusted_root_ca.pem` — this is the actual trust anchor
  `invest-public-api.tinkoff.ru` chains to; there's no bundled OS trust store equivalent on
  ESP-IDF). Response body is heap-allocated and null-terminated; caller frees it.
- `tbank_api` builds/parses requests against the REST/JSON gateway
  (`TBANK_REST_BASE_URL` in `app_config.h`), one function per gRPC method used:
  `InstrumentsService/FindInstrument`, `MarketDataService/GetLastPrices`,
  `MarketDataService/GetCandles`. Field names are camelCase (REST gateway convention, not
  Python SDK snake_case) — see `tbank-invest-api-guide.md` §10 if adding new calls.
- Money/quotation fields (`{"units": "...", "nano": ...}`) are parsed by `parse_money()`,
  tolerant of `units` being either a JSON string or number (gateways aren't consistent here).
- Timestamps are RFC3339 UTC strings, parsed/formatted assuming the device TZ is UTC (true by
  ESP-IDF default).

### Display internals (`src/display.c`)

- Panel RAM is 240 columns wide but this panel's glass is only 172px, centered in it — every
  draw needs the `BOARD_LCD_GAP_X = 34` column offset (`app_config.h`). Get this wrong and
  everything renders shifted/clipped.
- Framebuffer is RGB565; LVGL's software renderer produces little-endian pixels but the SPI
  panel expects big-endian, so `lvgl_flush_cb` manually byte-swaps before pushing to the
  panel. Because of this, `CONFIG_LV_COLOR_16_SWAP` must stay **off** in `sdkconfig.defaults`
  (enabling it would double-swap).
- All board/pin/timing constants live in `app_config.h`, not `display.c`.

### Configuration surface (`src/app_config.h`)

Single place for: which instrument to track (ticker + class-code hint, used to disambiguate
`FindInstrument` results), poll intervals, candle lookback window, REST base URL, WiFi retry
count, and all LCD pin/timing constants. Changing the tracked instrument only requires editing
`APP_INSTRUMENT_TICKER`/`APP_INSTRUMENT_CLASS_HINT`/`APP_INSTRUMENT_LABEL` here.

## Dependencies

Managed via ESP-IDF's component manager (`src/idf_component.yml`), not PlatformIO's library
registry:
- `lvgl/lvgl ^9.2`
- `espressif/cjson ^1.7` — pulled explicitly because PlatformIO's `framework-espidf` package
  strips the built-in `json`/cJSON component.

`managed_components/` and `dependencies.lock` are gitignored/regenerated; don't hand-edit them.

## Hardware specifics worth knowing before touching board config

- Flash is 4 MB (not the DevKitC's 8 MB) — `board_upload.flash_size`/`maximum_size` in
  `platformio.ini` and `sdkconfig.defaults`' `CONFIG_ESPTOOLPY_FLASHSIZE*` must stay in sync.
- Partition table is custom (`partitions.csv`): the default single-app table only gives 1 MB
  to the factory app, which is too small once LVGL + mbedTLS + WiFi are all linked in; this
  table gives the factory app ~3000K of the 4 MB flash.
- `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=4096` and `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192` are
  bumped from ESP-IDF defaults for TLS to `invest-public-api.tinkoff.ru` and for
  `esp_http_client`/cJSON parsing headroom, respectively.
- Board only exposes ESP32-C6's built-in USB-Serial-JTAG, hence `upload_protocol = esptool`.
