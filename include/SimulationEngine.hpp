#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include "OrderBook.hpp"

// Result of simulating one notional size through a full arbitrage route.
struct SimResult {
    double notional;      // input amount in source currency (e.g. 100 USDT)
    double outputAmount;  // how much we get back after full route
    double netProfitPct;  // (output/input - 1) * 100
    bool   viable;        // true if netProfitPct >= minProfitPct
    bool   insufficientLiquidity; // true if the book ran out before we filled
};

class SimulationEngine {
private:
    const OrderBookStore& store;
    double fee;

    // Simulates a single trade leg against one side of an order book.
    //
    // buy mode  (useAsks=true):  spends `amountIn` of quote, returns base received
    // sell mode (useAsks=false): spends `amountIn` of base,  returns quote received
    //
    // `insufficientLiquidity` is set to true if the book depth ran out.
    double simulateLeg(const std::vector<PriceLevel>& levels,
                       bool   useAsks,
                       double amountIn,
                       bool&  insufficientLiquidity) const {
        double remaining = amountIn;
        double received  = 0.0;

        for (const auto& level : levels) {
            if (remaining <= 0.0) break;
            if (level.price <= 0.0 || level.qty <= 0.0) continue;

            if (useAsks) {
                // Buying base with quote.
                // capacity = how much quote this level can absorb
                double capacityInQuote = level.qty * level.price;
                double quoteToSpend    = std::min(remaining, capacityInQuote);
                double baseBought      = quoteToSpend / level.price;
                received  += baseBought * (1.0 - fee);
                remaining -= quoteToSpend;
            } else {
                // Selling base for quote.
                // capacity = how much base this level can absorb
                double baseToSell  = std::min(remaining, level.qty);
                double quoteGained = baseToSell * level.price;
                received  += quoteGained * (1.0 - fee);
                remaining -= baseToSell;
            }
        }

        if (remaining > 1e-12) {
            insufficientLiquidity = true;
        }

        return received;
    }

public:
    explicit SimulationEngine(const OrderBookStore& store, double fee)
        : store(store), fee(fee) {}

    // Simulates executing `route` (as a list of currency-pair steps) at each
    // notional size. Prints a per-notional breakdown and returns the results.
    //
    // `steps` is a list of (from, to) pairs in trade order,
    //  e.g. [("USDT","ETH"), ("ETH","BTC"), ("BTC","USDT")]
    std::vector<SimResult> simulate(
        const std::vector<std::pair<std::string, std::string>>& steps,
        const std::vector<double>& notionals,
        double minProfitPct) const {

        std::vector<SimResult> results;
        results.reserve(notionals.size());

        for (double notional : notionals) {
            double amount = notional;
            bool   anyInsufficient = false;
            bool   bookMissing     = false;

            for (const auto& [from, to] : steps) {
                EdgeBookInfo info;
                if (!store.findEdgeInfo(from, to, info)) {
                    bookMissing = true;
                    break;
                }

                OrderBook book;
                if (!store.getBook(info.symbol, book)) {
                    bookMissing = true;
                    break;
                }

                const auto& levels = info.useAsks ? book.asks : book.bids;
                bool insufficient  = false;
                amount = simulateLeg(levels, info.useAsks, amount, insufficient);
                if (insufficient) anyInsufficient = true;
            }

            if (bookMissing) continue;

            double netPct = (amount / notional - 1.0) * 100.0;
            results.push_back({
                notional,
                amount,
                netPct,
                netPct >= minProfitPct,
                anyInsufficient
            });
        }

        // ── Print simulation results ───────────────────────────────────────────
        if (!results.empty()) {
            std::cout << std::fixed << std::setprecision(4);
            for (const auto& r : results) {
                std::cout << "[SIM] Notional $" << std::setw(7) << r.notional
                          << "  →  net: "
                          << (r.netProfitPct >= 0 ? "+" : "")
                          << r.netProfitPct << "%";

                if (r.viable) {
                    std::cout << "  ✓ viable";
                } else {
                    std::cout << "  ✗ not viable";
                }

                if (r.insufficientLiquidity) {
                    std::cout << " (book depth exhausted)";
                }
                std::cout << '\n';
            }
            std::cout << '\n';
        }

        return results;
    }
};