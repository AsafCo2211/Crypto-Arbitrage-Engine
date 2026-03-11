# Crypto Arbitrage Engine

A real-time triangular arbitrage detection engine for cryptocurrency markets, written in C++20.

Connects to Binance via WebSocket, models the market as a directed weighted graph, and runs the **Bellman-Ford algorithm** on every price update to detect negative-weight cycles — which correspond to profitable arbitrage routes. When an opportunity is found, a **slippage simulation** walks the real order book to estimate the net profit at different trade sizes.

> **Disclaimer:** This project is built for learning and portfolio purposes. It does not execute real trades. In live markets, arbitrage opportunities at this scale are closed within milliseconds by co-located HFT systems.

---

## Demo

```
[SYSTEM] Engine running — listening to 20 pairs.
[STATS] ──────────── Last 30s ────────────
[STATS] Ticks received:      1,842
[STATS] Quotes rejected:     0
[STATS] Bellman-Ford scans:  287
[STATS] Opportunities found: 2
[STATS] Best profit seen:    0.23%
[STATS] ─────────── Total run ───────────
[STATS] Ticks received:      3,610
[STATS] Bellman-Ford scans:  541
[STATS] Opportunities found: 3

[$$$] ARBITRAGE OPPORTUNITY DETECTED! [$$$]
[ROUTE]  USDT->ETH->BTC->USDT
[PROFIT] Theoretical: 0.21%

[SIM] Notional $  100  →  net: +0.17%  ✓ viable
[SIM] Notional $  500  →  net: +0.09%  ✓ viable
[SIM] Notional $ 1000  →  net: -0.03%  ✗ not viable (book depth exhausted)
```

---

## How It Works

### 1. Graph Model

The market is represented as a directed weighted graph:
- Each **currency** (BTC, ETH, USDT...) is a node
- Each **trading pair** produces two directed edges:
  - `QUOTE → BASE` with rate `1/ask` (buying base)
  - `BASE → QUOTE` with rate `bid` (selling base)

### 2. Bellman-Ford & Negative-Weight Cycles

To detect arbitrage, edge weights are transformed using the **negative logarithm** of the exchange rate:

```
weight = -log(rate × (1 - fee))
```

Under this transformation:
- A path's total weight = `-log(product of all rates)`
- A **negative-weight cycle** = a sequence of trades whose product exceeds 1.0 = **profit**

Bellman-Ford detects such cycles in **O(V × E)** time.

### 3. Guard Rails

Raw market data is noisy. Several layers of protection prevent false positives:

| Guard | What it does |
|---|---|
| **Quote freshness** | Ignores edges not updated within `maxQuoteAgeMs` |
| **Profit threshold** | Only reports opportunities above `minProfitPercent` |
| **Dedup / cooldown** | Suppresses re-reporting the same route within `cooldownSeconds` |
| **Zero-rate guard** | Skips edges where `log(0)` or `log(negative)` would corrupt the distance array |

### 4. Slippage Simulation

The theoretical profit assumes infinite liquidity at the best price. In reality, large orders "eat through" the order book, causing the average fill price to worsen with trade size.

The engine subscribes to `@depth5` streams (5 price levels per side) and simulates each notional size against the real book:

```
Buying $500 of ETH at ask=3200.00 (0.5 ETH available):
  Level 1: spend $1,600 → receive 0.499 ETH (fee applied)
  Level 2: spend $3,200 → ...
  → Average fill price: 3201.20 (worse than best ask)
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                        main.cpp                         │
│         (wires components, manages threads)             │
└────────┬──────────────────┬───────────────┬─────────────┘
         │                  │               │
         ▼                  ▼               ▼
┌─────────────────┐ ┌──────────────┐ ┌──────────────────┐
│  BinanceClient  │ │    Graph     │ │  StatsCollector  │
│                 │ │              │ │                  │
│ WebSocket conn  │ │ Bellman-Ford │ │ ticks / scans /  │
│ depth5 parser   │ │ cycle detect │ │ opportunities    │
│ quote validator │ │ dedup/cooldn │ │ CSV export       │
└────────┬────────┘ └──────┬───────┘ └──────────────────┘
         │                  │
         ▼                  ▼
┌─────────────────┐ ┌──────────────────┐
│  OrderBookStore │ │ SimulationEngine │
│                 │ │                  │
│ 5-level book    │ │ walks book depth │
│ per symbol      │ │ per notional     │
└─────────────────┘ └──────────────────┘

         ▲ all parameters from ▼
         ┌──────────────────┐
         │   config.json    │
         └──────────────────┘
```

**Threading model:**

| Thread | Responsibility |
|---|---|
| WebSocket (IXWebSocket) | Receives price updates, updates graph + order book |
| Scanner | Wakes on `marketDirty` flag, runs Bellman-Ford |
| Summary | Prints stats every 30 seconds |

All shared state in the market **Graph** is protected by a single `marketMutex`. The `marketDirty` flag is an `std::atomic<bool>` to avoid locking on every tick.

**OrderBookStore** uses a `std::shared_mutex` instead of a plain mutex. Reads (`findEdgeInfo`, `getBook`) acquire a `std::shared_lock`, allowing the scanner thread to read multiple symbols concurrently. Writes (`updateBook`) acquire a `std::unique_lock` for exclusive access during the update.

---

## Project Structure

```
├── config/
│   └── config.json          # All runtime parameters
├── include/
│   ├── Config.hpp           # JSON config loader + validation
│   ├── Graph.hpp            # Bellman-Ford engine
│   ├── BinanceClient.hpp    # WebSocket client + depth5 parser
│   ├── StatsCollector.hpp   # Runtime metrics + CSV export
│   ├── OrderBook.hpp        # Order book store (thread-safe)
│   └── SimulationEngine.hpp # Slippage-aware execution simulation
├── src/
│   └── main.cpp
├── tests/
│   ├── test_graph.cpp       # Bellman-Ford unit tests
│   ├── test_simulation.cpp  # OrderBook + SimulationEngine tests
│   └── test_config.cpp      # Config loading + validation tests
└── .github/
    └── workflows/
        └── ci.yml           # GitHub Actions CI
```

---

## Getting Started

### Prerequisites

- CMake ≥ 3.16
- C++20 compiler (clang++ or g++)
- OpenSSL
- [nlohmann/json](https://github.com/nlohmann/json)
- [IXWebSocket](https://github.com/machinezone/IXWebSocket) (fetched as a git submodule)

On macOS:
```bash
brew install cmake openssl nlohmann-json
```

On Ubuntu:
```bash
sudo apt-get install cmake build-essential libssl-dev nlohmann-json3-dev
```

### Build

```bash
# Clone with submodules
git clone --recurse-submodules https://github.com/your-username/crypto-arbitrage-engine
cd crypto-arbitrage-engine

# Build
cmake -B build
cmake --build build --parallel
```

### Run

```bash
./build/ArbitrageEngine
```

The engine reads `config/config.json` on startup. Edit it to tune parameters without recompiling.

### Configuration

```json
{
    "fee": 0.001,              // Taker fee per leg (0.1%)
    "scanIntervalMs": 100,     // How often to run Bellman-Ford
    "minProfitPercent": 0.1,   // Minimum profit to report (%)
    "maxQuoteAgeMs": 3000,     // Reject quotes older than this
    "cooldownSeconds": 10,     // Suppress duplicate alerts
    "sourceCurrency": "USDT",  // Starting node for Bellman-Ford
    "simulationNotionals": [100, 500, 1000],  // Trade sizes to simulate ($)
    "symbols": [ ... ]         // Trading pairs to subscribe to
}
```

---

## Running Tests

```bash
cmake -B build -DBUILD_TESTS=ON
cmake --build build --parallel
cd build && ctest --output-on-failure
```

Expected output:
```
100% tests passed, 0 tests failed out of 28
```

Tests cover:
- Bellman-Ford correctly detects known arbitrage cycles
- No false positives on mathematically consistent market rates
- Fees and profit threshold correctly filter opportunities
- Order book lookup (ask/bid side selection)
- Slippage simulation with deep and shallow books
- `insufficient_liquidity` flag triggered correctly
- Config loading, validation, and all error cases

---

## Limitations

This engine is a **detection and simulation** tool, not a trading bot.

- **Latency:** WebSocket data arrives with tens of milliseconds of network delay. Real HFT systems co-locate servers inside exchanges for sub-millisecond access. By the time this engine detects an opportunity, it is almost certainly already closed.
- **Depth:** `@depth5` provides only 5 price levels. Real executions may need more depth for larger notionals.
- **Single exchange:** True arbitrage also exists across exchanges (e.g. Binance vs Kraken), which this engine does not model.
- **No execution:** The engine does not place orders. Connecting to a real trading API and managing order lifecycle is a significant additional layer.

---

## Roadmap

- [ ] Cross-exchange arbitrage (Binance + Kraken)
- [ ] Historical backtesting on tick data
- [ ] Deeper order book (`@depth20`)
- [ ] Web dashboard for live opportunity visualization
- [ ] Paper trading mode with simulated order execution

---

## Tech Stack

| | |
|---|---|
| Language | C++20 |
| Build | CMake + FetchContent |
| WebSocket | [IXWebSocket](https://github.com/machinezone/IXWebSocket) |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) |
| Testing | [GoogleTest](https://github.com/google/googletest) |
| CI | GitHub Actions |
| Data | Binance WebSocket API (`@depth5`) |