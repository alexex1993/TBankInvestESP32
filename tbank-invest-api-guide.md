# T-Bank Invest API — Practical Guide

A standalone, language-agnostic-overview + Python-focused guide for working with the
**T-Bank (formerly Tinkoff) Invest API**. It covers authentication, the official Python
SDK, the REST gateway, the data model, common tasks, and the practical "gotchas" that
are easy to miss.

> The API is provided "as is" by T-Bank. Market data methods, several instrument methods,
> and some naming are marked `@deprecated` in the current SDK but remain fully functional.
> This guide uses the methods that work today.

---

## Table of Contents

1. [Architecture at a Glance](#1-architecture-at-a-glance)
2. [Authentication](#2-authentication)
3. [Installation (Python SDK)](#3-installation-python-sdk)
4. [TLS & Root Certificates (Important)](#4-tls--root-certificates-important)
5. [Connecting: `Client` and `AsyncClient`](#5-connecting-client-and-asyncclient)
6. [Core Concepts](#6-core-concepts)
7. [Services Overview](#7-services-overview)
8. [Common Recipes](#8-common-recipes)
9. [Bond-Specific Notes](#9-bond-specific-notes)
10. [Using REST Instead of gRPC](#10-using-rest-instead-of-grpc)
11. [Errors, Retries & Rate Limits](#11-errors-retries--rate-limits)
12. [Streaming](#12-streaming)
13. [Sandbox](#13-sandbox)
14. [Tips & Gotchas Summary](#14-tips--gotchas-summary)
15. [References](#15-references)

---

## 1. Architecture at a Glance

The Invest API is a **gRPC API** with an official **Python SDK** and a **JSON/REST
gateway** on top.

| Layer | Endpoint / package | Use when |
|------|--------------------|----------|
| gRPC (primary) | `invest-public-api.tbank.ru:443` | Anything; lowest latency, streaming |
| Python SDK | `t-tech-investments` (`t_tech.invest`) | Python apps; wraps gRPC |
| REST/JSON gateway | `https://invest-public-api.tinkoff.ru/rest/...` | Quick scripts, non-Python, curl |

- **gRPC target:** `invest-public-api.tbank.ru` (sandbox: `sandbox-invest-public-api.tbank.ru`).
- The older domain `invest-public-api.tinkoff.ru` still resolves to the same infra and is
  used by the REST gateway.
- Services are organized into groups: `instruments`, `market_data`, `users`, `operations`,
  `orders`, `stop_orders`, `sandbox`, etc.

---

## 2. Authentication

- Get a token in the T-Investments app: **Profile → Settings → Invest API token**.
- The token encodes **access scopes**. Choose a token with only the rights you need:
  - **Read-only** (accounts, portfolio, market data, instruments) — safe for analytics.
  - **Trading** (place/cancel orders) — required for order execution.
  - **Sandbox** — for the sandbox service.
- The token is passed:
  - **gRPC/SDK:** as the first argument to `Client(token)`.
  - **REST:** as `Authorization: Bearer <token>` header.

> Treat the token as a secret. Store it in an environment variable or a secret manager,
> never commit it.

---

## 3. Installation (Python SDK)

The current official package is **`t-tech-investments`**, distributed from T-Bank's
package registry:

```bash
pip install t-tech-investments \
  --index-url https://opensource.tbank.ru/api/v4/projects/238/packages/pypi/simple
```

Import path is **`t_tech.invest`** (note: the module name differs from the package name):

```python
from t_tech.invest import Client, InstrumentStatus
```

Dependencies pulled in: `grpcio`, `protobuf`, `cachetools`, `deprecation`,
`python-dateutil`, `iprotopy`, `sentry-sdk`. Python 3.8+ is supported (works on 3.12–3.14).

> The legacy community package `tinkoff-invest` (import `tinkoff.invest`) has a nearly
> identical API surface but is **quarantined/no longer updated**. Prefer
> `t-tech-investments` for new code.

---

## 4. TLS & Root Certificates (Important)

The API server certificate chains to the **Russian Trusted Root CA** (Минцифры), which is
**not present in default trust stores** on macOS/Windows/Linux outside Russia. To handle
this, the SDK **bundles** `RussianTrustedRootCA.pem` and uses it by default.

Control this with the environment variable **`SSL_TBANK_VERIFY`**:

| Value | Behavior |
|-------|----------|
| `true` (default) | Validate against the bundled **Russian Trusted Root CA**. ✅ recommended |
| `false` | Use the **system default** trust store (will fail outside RU unless the Russian root is installed system-wide). |

```bash
# Default — works everywhere thanks to the bundled root:
export TBANK_TOKEN="t...."
# (no SSL_TBANK_VERIFY needed)
```

If you use **REST/`requests`** instead of the SDK, you must either add the Russian root to
your trust store or pass `verify=False` (and suppress the warning) — the SDK's bundled cert
is not used by `requests`.

---

## 5. Connecting: `Client` and `AsyncClient`

```python
import os
from t_tech.invest import Client

TOKEN = os.environ["TBANK_TOKEN"]

with Client(TOKEN, app_name="my-app") as client:
    accounts = client.users.get_accounts()
    print(accounts)
```

- `Client` is **synchronous** (context manager). `AsyncClient` is the async equivalent.
- `app_name` is optional but recommended — it shows up in T-Bank's request tracking.
- The context manager returns a `Services` object exposing all service groups
  (`client.instruments`, `client.market_data`, `client.users`, `client.operations`,
  `client.orders`, …).

Async example:

```python
import asyncio
from t_tech.invest import AsyncClient

async def main():
    async with AsyncClient(TOKEN) as client:
        print(await client.users.get_accounts())

asyncio.run(main())
```

---

## 6. Core Concepts

### 6.1 Instrument identifiers

| ID | Scope | Example | Notes |
|----|-------|---------|-------|
| `figi` | Global (per instrument) | `BBG004730N88` | The classic identifier; unique worldwide |
| `ticker` | Per exchange class | `SBER`, `SU26238RMFS4` | Not unique without `class_code` |
| `class_code` | Trading mode | `TQBR`, `TQCB`, `TQOB` | Required together with `ticker` |
| `uid` | API-internal, instrument | `40d89385-...` | Stable; used by risk rates, etc. |
| `asset_uid` | Underlying entity | `40d89385-...` | Groups instruments of one issuer/type |
| `isin` | International | `RU000A0JP2V3` | ISIN code |
| `position_uid` | Position | `...` | For portfolio positions |

Common **`class_code`** values:

| Code | Meaning |
|------|---------|
| `TQBR` / `TQTF` | MOEX / SPB shares |
| `TQOB` | Government bonds (**OFZ**) |
| `TQCB` | Corporate bonds |
| `TQIR` | Exchange repo |
| `CETS` | FX |

### 6.2 Money model: `MoneyValue` and `Quotation`

Monetary amounts and prices use a fixed-point representation with two integer fields:

```python
def to_float(v) -> float:
    return float(v.units) + float(v.nano) / 1e9
```

- `units`: integer part (can be negative).
- `nano`: fractional part in nano-units (0–999_999_999).
- `MoneyValue` additionally has a `currency` field; `Quotation` does not.

### 6.3 Enums

Enums are protobuf-style. Useful ones:

- `InstrumentStatus`: `INSTRUMENT_STATUS_UNSPECIFIED=0`, `_BASE=1` (tradable now),
  `_ALL=2` (everything, including unlisted/delisted). Use `_ALL` for full listings.
- `InstrumentIdType`: `_UNSPECIFIED=0`, `_TICKER=1`, `_FIGI=2`, `_UID=4`, …
- `CandleInterval`: `_1_MIN`, `_5_MIN`, `_HOUR`, `_DAY`, `_WEEK`, `_MONTH`.
- `CouponType`: `_CONSTANT=1`, `_FLOATING=2`, `_DISCOUNT=3`, `_MORTGAGE=4`, `_FIX=5`,
  `_VARIABLE=6`, `_OTHER=7`.
- `RiskLevel`: `_UNSPECIFIED=0`, `_LOW=1`, `_MODERATE=2`, `_HIGH=3`.

Compare enums by identity or `int(x)`.

### 6.4 Timestamps

All `datetime` fields are **timezone-aware UTC**. Always compare with tz-aware datetimes
(`datetime.now(tz=timezone.utc)`).

---

## 7. Services Overview

| Service group | Representative methods |
|---------------|------------------------|
| `client.instruments` | `shares`, `bonds`, `etfs`, `currencies`, `futures`, `*_by(...)`, `find_instrument`, `get_bond_coupons`, `get_asset_fundamentals`, `get_risk_rates`, `get_dividends` |
| `client.market_data` | `get_last_prices`, `get_candles`, `get_all_candles`, `get_order_book`, `get_trading_status`, `get_last_trades`, `get_close_prices` |
| `client.market_data_stream` | Bid/ask/candle/trade streaming |
| `client.users` | `get_accounts`, `get_info`, `get_margin_attributes` |
| `client.operations` | `get_portfolio`, `get_positions`, `get_operations`, `get_operations_by_cursor`, `get_withdraw_limits` |
| `client.orders` | `post_order`, `cancel_order`, `get_orders` |
| `client.stop_orders` | `post_stop_order`, `cancel_stop_order` |
| `client.sandbox` | `open_sandbox_account`, `sandbox_pay_in`, `close_sandbox_account` |

---

## 8. Common Recipes

### 8.1 Accounts

```python
with Client(TOKEN) as c:
    resp = c.users.get_accounts()
    for a in resp.accounts:
        print(a.id, a.name, a.type, a.status)
```

### 8.2 List instruments

```python
from t_tech.invest import Client, InstrumentStatus

with Client(TOKEN) as c:
    bonds = c.instruments.bonds(
        instrument_status=InstrumentStatus.INSTRUMENT_STATUS_ALL
    ).instruments
    print(len(bonds), "bonds")
    for b in bonds[:5]:
        print(b.ticker, b.class_code, b.name)
```

Same pattern for `shares()`, `etfs()`, `currencies()`, `futures()`.

### 8.3 Get one instrument by id

```python
from t_tech.invest import InstrumentIdType

with Client(TOKEN) as c:
    one = c.instruments.bond_by(
        id_type=InstrumentIdType.INSTRUMENT_ID_TYPE_FIGI,
        class_code="TQCB",
        id="TCS00A10F397",
    ).instrument
    print(one.ticker, one.maturity_date, one.nominal)
```

### 8.4 Find an instrument by name/ticker/ISIN

```python
with Client(TOKEN) as c:
    r = c.instruments.find_instrument(query="SBER")
    for i in r.instruments:
        print(i.instrument_type, i.ticker, i.name, i.figi, i.uid, i.class_code)
```

`find_instrument` is the easiest way to resolve a user-typed string to a `figi`/`uid`.

### 8.5 Last prices (batch)

```python
def to_float(v):
    return float(v.units) + float(v.nano) / 1e9

with Client(TOKEN) as c:
    figis = ["BBG004730N88", "BBG004730ZJ9"]
    r = c.market_data.get_last_prices(instrument_id=figis)
    for lp in r.last_prices:
        print(lp.figi, to_float(lp.price), lp.time)
```

> For **bonds**, the returned `price` is in **percent of nominal** (e.g. `98.5` = 98.5% of
> face value). See [§9](#9-bond-specific-notes).

### 8.6 Historical candles

```python
from datetime import timedelta
from t_tech.invest import CandleInterval
from t_tech.invest.utils import now

with Client(TOKEN) as c:
    for candle in c.get_all_candles(
        instrument_id="BBG004730N88",
        from_=now() - timedelta(days=30),
        interval=CandleInterval.CANDLE_INTERVAL_HOUR,
    ):
        print(candle.time, to_float(candle.close), candle.volume)
```

`get_all_candles` auto-paginates. `get_candles` returns one page with pagination cursors.

### 8.7 Order book & trading status

```python
with Client(TOKEN) as c:
    ob = c.market_data.get_order_book(figi="BBG004730N88", depth=10)
    ts = c.market_data.get_trading_status(figi="BBG004730N88")
    print(ob.bids[:3], ob.asks[:3], ts.market_order_available_flag)
```

### 8.8 Bond coupon schedule

```python
from datetime import datetime, timezone

now = datetime.now(tz=timezone.utc)
with Client(TOKEN) as c:
    bond = c.instruments.bond_by(
        id_type=InstrumentIdType.INSTRUMENT_ID_TYPE_TICKER,
        class_code="TQOB",
        id="SU26238RMFS4",
    ).instrument
    resp = c.instruments.get_bond_coupons(
        figi=bond.figi, from_=now, to=bond.maturity_date
    )
    for ev in resp.events:
        if ev.coupon_date.replace(tzinfo=timezone.utc) > now:
            print(ev.coupon_date.date(), to_float(ev.pay_one_bond), ev.coupon_type)
```

> ⚠️ If you omit `to=`, you get only the **near-term** coupons, not the full schedule.
> Always pass `to=bond.maturity_date` (and `from_=now` to skip history).

### 8.9 Asset fundamentals (EBITDA, leverage, etc.)

```python
from t_tech.invest.schemas import GetAssetFundamentalsRequest

with Client(TOKEN) as c:
    share = c.instruments.shares().instruments[0]
    r = c.instruments.get_asset_fundamentals(
        request=GetAssetFundamentalsRequest(assets=[share.asset_uid])
    )
    for s in r.fundamentals:
        print(s.market_capitalization, s.ebitda_ttm,
              s.net_debt_to_ebitda, s.roe)
```

> Fundamentals are attached to **equity assets** (stocks), not bond assets. To analyze a
> bond's issuer, resolve the issuer to its public stock first (e.g. by name), then query
> that stock's `asset_uid`. Many bond issuers are private (no fundamentals).

### 8.10 Risk rates

```python
from t_tech.invest.schemas import RiskRatesRequest

with Client(TOKEN) as c:
    r = c.instruments.get_risk_rates(
        request=RiskRatesRequest(instrument_id=[some_uid])
    )
    for rr in r.instrument_risk_rates:
        print(rr.instrument_uid,
              rr.short_risk_rate, rr.long_risk_rate, rr.error)
```

Coverage is sparse for many instruments — treat as a secondary signal.

### 8.11 Portfolio & operations

```python
with Client(TOKEN) as c:
    account_id = c.users.get_accounts().accounts[0].id
    print(c.operations.get_portfolio(account_id=account_id))
    print(c.operations.get_positions(account_id=account_id))
    print(c.operations.get_operations(
        account_id=account_id,
        from_=datetime(2025, 1, 1),
        to=datetime(2025, 12, 31),
    ))
```

### 8.12 Place and cancel an order

```python
from decimal import Decimal
from t_tech.invest import OrderDirection, OrderType
from t_tech.invest.utils import decimal_to_quotation

with Client(TOKEN) as c:
    account_id = c.users.get_accounts().accounts[0].id
    resp = c.orders.post_order(
        figi="BBG004730N88",
        quantity=1,
        price=decimal_to_quotation(Decimal("300.00")),
        direction=OrderDirection.ORDER_DIRECTION_BUY,
        account_id=account_id,
        order_type=OrderType.ORDER_TYPE_LIMIT,
        order_id="my-idempotency-key-1",
    )
    print(resp.order_id)
    c.orders.cancel_order(account_id=account_id, order_id=resp.order_id)
```

Use `order_id` for **idempotency** — retrying with the same value will not place a second
order.

---

## 9. Bond-Specific Notes

Bonds need extra care. The `Bond` message has the key fields: `ticker`, `class_code`,
`isin`, `nominal`, `currency`, `coupon_quantity_per_year`, `maturity_date`, `aci_value`,
`floating_coupon_flag`, `perpetual_flag`, `amortization_flag`, `risk_level`, `call_date`,
`asset_uid`, `sector`, `country_of_risk`.

### 9.1 Price is a percent of nominal

`get_last_prices` returns bond prices as **% of face value**. Convert to rubles:

```python
clean_price_rub = price_pct / 100.0 * to_float(bond.nominal)
dirty_price     = clean_price_rub + to_float(bond.aci_value)   # + НКД
```

### 9.2 Yield is NOT provided — compute YTM yourself

The API does not return effective yield. Build cash flows and solve the IRR:

```python
def xirr(amounts, day_offsets):
    def npv(r):
        return sum(a / (1 + r) ** (d / 365.0) for a, d in zip(amounts, day_offsets))
    lo, hi = -0.9999, 10.0
    if npv(lo) * npv(hi) > 0:
        return None
    for _ in range(200):
        mid = (lo + hi) / 2
        hi = mid if npv(lo) * npv(mid) <= 0 else hi
        lo = mid if npv(lo) * npv(mid) > 0 else lo
    return (lo + hi) / 2
```

Inputs: dirty price today (outflow), future coupons (`pay_one_bond`), and nominal at
maturity (inflow). Use actual day offsets for XIRR-style annualization.

### 9.3 Offer / call date sentinel

`call_date` is populated for every bond. The **epoch value `1970-01-01` means "no offer"**.
A real offer exists only when the date is a genuine future date **earlier than maturity**:

```python
def has_real_offer(bond) -> bool:
    cd = bond.call_date
    if not cd or cd.year <= 2000:        # epoch sentinel == no offer
        return False
    return cd.replace(tzinfo=timezone.utc) < bond.maturity_date.replace(tzinfo=timezone.utc)
```

### 9.4 Bond structural flags

| Flag | Meaning | Effect on analysis |
|------|---------|--------------------|
| `floating_coupon_flag` | Floater (linked to key rate/RUONIA) | Future coupons unknown — skip or model separately |
| `perpetual_flag` | Perpetual / subordinated | No fixed maturity — YTM undefined |
| `amortization_flag` | Principal repaid in parts | Do **not** add full nominal at maturity; use the amortization schedule |
| `weekend_flag` | Weekend bonds | Special settlement |

For comparable fixed-coupon, bullet bonds, filter:

```python
def is_plain_fixed_bullet(b):
    return (b.currency == "rub"
            and not b.floating_coupon_flag
            and not b.perpetual_flag
            and not b.amortization_flag)
```

### 9.5 Coupon schedule needs `to=maturity_date`

As noted in [§8.8](#88-bond-coupon-schedule), the default horizon is short. Always pass
`to=bond.maturity_date` to receive every remaining coupon.

---

## 10. Using REST Instead of gRPC

The JSON gateway mirrors the gRPC services. Pattern:

```
POST https://invest-public-api.tinkoff.ru/rest/<fully.qualified.Service>/<Method>
Authorization: Bearer <TOKEN>
Content-Type: application/json

{ "requestField": "VALUE" }      # camelCase field names
```

Example — list all bonds:

```bash
curl -s -X POST \
  "https://invest-public-api.tinkoff.ru/rest/tinkoff.public.invest.api.contract.v1.InstrumentsService/Bonds" \
  -H "Authorization: Bearer $TBANK_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"instrumentStatus":"INSTRUMENT_STATUS_ALL"}'
```

Key differences from the SDK:

- Field names are **camelCase** (`instrumentStatus`, `lastPrices`) vs snake_case in Python.
- The service path is the fully-qualified gRPC name
  (`tinkoff.public.invest.api.contract.v1.<Service>`).
- For outside-Russia environments, add the Russian Trusted Root CA to your trust store or
  use `curl -k` / `requests verify=False`.

---

## 11. Errors, Retries & Rate Limits

- SDK exceptions: `RequestError` (sync), `AioRequestError` (async), base `InvestError`.
  `RequestError` carries `.code`, `.metadata.tracking_id`.
- gRPC status codes you'll see: `UNAVAILABLE` (transient network/TLS), `RESOURCE_EXHAUSTED`
  (rate limit — retry with backoff), `INVALID_ARGUMENT`, `PERMISSION_DENIED` (token scope).
- **Rate limits:** the API enforces per-method request quotas (RPS/requests-per-minute).
  Retry `RESOURCE_EXHAUSTED`/HTTP 429 with exponential backoff. See the official docs for
  current numeric limits.
- For heavy workloads, the SDK provides retrying clients:
  `t_tech.invest.retrying.client.RetryingClient` /
  `AsyncRetryingClient` with `RetryClientSettings`.

```python
import logging
logging.basicConfig(level=logging.INFO)
from t_tech.invest.retrying.settings import RetryClientSettings
from t_tech.invest.retrying.sync.client import RetryingClient

settings = RetryClientSettings(use_retry=True, max_retry_attempt=3)
with RetryingClient(TOKEN, settings=settings) as c:
    ...
```

---

## 12. Streaming

Real-time data uses server streaming over gRPC (best with `AsyncClient`):

- **Market data:** candles, order book, trades, last prices.
- **Portfolio/positions:** changes to your account.

```python
import asyncio
from t_tech.invest import (
    AsyncClient, CandleInstrument, MarketDataRequest,
    SubscribeCandlesRequest, SubscriptionAction, SubscriptionInterval,
)

async def main():
    async with AsyncClient(TOKEN) as c:
        async def reqs():
            yield MarketDataRequest(subscribe_candles_request=SubscribeCandlesRequest(
                subscription_action=SubscriptionAction.SUBSCRIPTION_ACTION_SUBSCRIBE,
                instruments=[CandleInstrument(
                    figi="BBG004730N88",
                    interval=SubscriptionInterval.SUBSCRIPTION_INTERVAL_ONE_MINUTE,
                )],
            ))
            while True:
                await asyncio.sleep(1)
        async for msg in c.market_data_stream.market_data_stream(reqs()):
            print(msg)

asyncio.run(main())
```

---

## 13. Sandbox

The sandbox lets you test trading with virtual money (no real orders).

```python
from t_tech.invest import MoneyValue
from t_tech.invest.sandbox.client import SandboxClient

with SandboxClient(TOKEN) as c:
    acc = c.sandbox.open_sandbox_account()
    c.sandbox.sandbox_pay_in(
        account_id=acc.account_id,
        amount=MoneyValue(units=1_000_000, nano=0, currency="rub"),
    )
    # ... use c.orders, c.operations exactly like a real account ...
    c.sandbox.close_sandbox_account(account_id=acc.account_id)
```

A sandbox token can only access sandbox endpoints.

---

## 14. Tips & Gotchas Summary

1. **Install** `t-tech-investments` from T-Bank's registry; import as `t_tech.invest`.
2. **TLS:** rely on the bundled Russian root (default). Only set `SSL_TBANK_VERIFY=false`
   if your system already trusts the Russian CA.
3. **Token scopes:** use read-only tokens for analytics; never expose trading tokens.
4. **Money:** always convert `units`/`nano` to float with the `units + nano/1e9` formula.
5. **Datetimes:** UTC and tz-aware everywhere.
6. **Instrument IDs:** `figi` is globally unique; `ticker` needs `class_code`; many calls
   accept `instrument_id` (figi or uid).
7. **Bonds:** price is `% of nominal`; YTM must be computed; coupons need `to=maturity`;
   `call_date == 1970` means no offer; watch the structural flags.
8. **Fundamentals:** keyed to **equity** assets, not bond assets — map issuers to stocks.
9. **Deprecation warnings:** many methods print `DeprecatedWarning` but still work; suppress
   with `warnings.filterwarnings("ignore")` if noisy.
10. **Batch where possible:** `get_last_prices`, `get_asset_fundamentals`, `get_risk_rates`
    accept lists — prefer one batched call over N individual calls. For per-instrument calls
    (e.g. `get_bond_coupons`), parallelize with a thread pool — the gRPC channel is
    thread-safe.
11. **REST:** use the JSON gateway for quick scripts; remember camelCase fields.
12. **Idempotency:** pass `order_id` when placing orders to make retries safe.

---

## 15. References

- T-Bank Invest dev portal: <https://developer.tbank.ru/invest>
- API intro: <https://developer.tbank.ru/invest/intro/intro>
- Python SDK FAQ: <https://developer.tbank.ru/invest/sdk/python_sdk/faq_python>
- gRPC contract & Swagger (community mirror): <https://russianinvestments.github.io/invest-api/>
- Python SDK examples (community mirror): <https://russianinvestments.github.io/invest-python/examples/>

---

*This guide is for educational purposes and describes a third-party API. Verify current
behavior against the official documentation, as methods, fields, and limits may change.*
