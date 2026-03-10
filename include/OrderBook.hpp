#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

using Clock     = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

// A single price level in the order book.
// qty is always in base currency units.
struct PriceLevel {
    double price; // price in quote currency  (e.g. 3200.50 USDT per ETH)
    double qty;   // available quantity in base currency (e.g. 0.5 ETH)
};

// A snapshot of one symbol's order book (up to 5 levels per side).
//   asks: sorted ascending  — best (lowest) ask first
//   bids: sorted descending — best (highest) bid first
struct OrderBook {
    std::vector<PriceLevel> asks;
    std::vector<PriceLevel> bids;
    TimePoint lastUpdated;
};

// Describes which order book and which side to consume for a given graph edge.
struct EdgeBookInfo {
    std::string symbol;  // e.g. "ETHUSDT"
    bool        useAsks; // true  → buying base   (quote→base): eat through asks
                         // false → selling base  (base→quote): eat through bids
};

// Thread-safe store for all order books.
// Also holds an edge→book lookup table built once from the symbol registry.
class OrderBookStore {
private:
    mutable std::mutex mutex;

    // symbol (e.g. "ETHUSDT") → latest order book snapshot
    std::unordered_map<std::string, OrderBook> books;

    // "FROM->TO" (e.g. "USDT->ETH") → which book + which side
    std::unordered_map<std::string, EdgeBookInfo> edgeLookup;

public:
    // ── Setup ─────────────────────────────────────────────────────────────────

    // Called once at startup from BinanceClient::setSymbolRegistry.
    // For each symbol XYZABC (base=XYZ, quote=ABC) we register:
    //   ABC→XYZ  uses XYZABC asks  (buying base with quote)
    //   XYZ→ABC  uses XYZABC bids  (selling base for quote)
    void registerSymbol(const std::string& symbol,
                        const std::string& base,
                        const std::string& quote) {
        std::lock_guard<std::mutex> lock(mutex);
        edgeLookup[quote + "->" + base] = {symbol, true};  // asks
        edgeLookup[base  + "->" + quote] = {symbol, false}; // bids
    }

    // ── Updates (called from WebSocket thread) ────────────────────────────────

    void updateBook(const std::string& symbol,
                    std::vector<PriceLevel> asks,
                    std::vector<PriceLevel> bids) {
        std::lock_guard<std::mutex> lock(mutex);
        books[symbol] = {std::move(asks), std::move(bids), Clock::now()};
    }

    // ── Lookups (called from scanner thread) ──────────────────────────────────

    // Returns the EdgeBookInfo for a (from, to) currency pair.
    // Returns nullptr-equivalent (empty optional-like) if not found.
    bool findEdgeInfo(const std::string& from, const std::string& to,
                      EdgeBookInfo& out) const {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = edgeLookup.find(from + "->" + to);
        if (it == edgeLookup.end()) return false;
        out = it->second;
        return true;
    }

    // Returns a copy of the order book for the given symbol.
    // Returns false if the symbol has no book yet.
    bool getBook(const std::string& symbol, OrderBook& out) const {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = books.find(symbol);
        if (it == books.end()) return false;
        out = it->second;
        return true;
    }
};