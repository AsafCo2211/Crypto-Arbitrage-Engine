#pragma once

/**
 * @file SimulationEngine.hpp
 * @brief Slippage-aware execution simulation for arbitrage routes.
 *
 * When Bellman-Ford detects a profitable cycle, it computes a **theoretical**
 * profit based on the best available bid/ask price — assuming that any trade
 * size can be filled at that price. This is not realistic.
 *
 * SimulationEngine answers the more honest question:
 * > "If I enter this trade with $X, how much do I actually exit with,
 * >  after walking through the real order book level by level?"
 *
 * ### Slippage explained
 *
 * Each price level in the order book has a limited quantity. Once that
 * quantity is exhausted, the next order must be filled at a worse price.
 * For a buy order eating through the ask side:
 *
 * @code
 * Level 1: ask=3200.00, qty=0.5 ETH → can fill $1600 here
 * Level 2: ask=3200.50, qty=1.2 ETH → remaining fills here at worse price
 * Level 3: ask=3201.00, qty=0.8 ETH → ...
 * @endcode
 *
 * The average fill price worsens with trade size. SimulationEngine captures
 * this effect for each notional size defined in config.json.
 */

#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include "OrderBook.hpp"

/**
 * @brief Result of simulating one complete arbitrage route at one notional size.
 */
struct SimResult {
    double notional;             ///< Input amount in source currency (e.g. 100.0 USDT)
    double outputAmount;         ///< Amount received after completing the full route
    double netProfitPct;         ///< Net profit as a percentage: (output/input - 1) × 100
    bool   viable;               ///< true if netProfitPct >= minProfitPct
    bool   insufficientLiquidity;///< true if order book ran out before the order was filled
};

/**
 * @brief Simulates executing arbitrage routes against real order book depth.
 *
 * Given a list of (from, to) currency conversion steps and a set of notional
 * sizes, walks each notional through every step of the route, consuming order
 * book levels as it goes. Returns a SimResult per notional size and prints
 * a formatted breakdown to stdout.
 *
 * SimulationEngine is stateless between calls — it reads from OrderBookStore
 * at call time and does not hold any mutable state of its own.
 */
class SimulationEngine {
private:
    const OrderBookStore& store; ///< Source of order book snapshots (shared, read-only)
    double fee;                  ///< Taker fee applied to each leg (from Config)

    /**
     * @brief Simulates a single trade leg against one side of an order book.
     *
     * Iterates through the price levels in order, spending @p amountIn until
     * it is exhausted or the book runs out.
     *
     * **Buy mode** (@p useAsks = true):
     * - Spends @p amountIn units of quote currency
     * - Returns base currency received (after fee)
     *
     * **Sell mode** (@p useAsks = false):
     * - Spends @p amountIn units of base currency
     * - Returns quote currency received (after fee)
     *
     * @param levels              Price levels to consume (asks or bids)
     * @param useAsks             true = buy mode, false = sell mode
     * @param amountIn            Amount of input currency to spend
     * @param insufficientLiquidity Set to true if book depth was exhausted before fill
     * @return Amount of output currency received after fee
     */
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
                // Buying base with quote: capacity = qty × price (in quote)
                double capacityInQuote = level.qty * level.price;
                double quoteToSpend    = std::min(remaining, capacityInQuote);
                double baseBought      = quoteToSpend / level.price;
                received  += baseBought * (1.0 - fee);
                remaining -= quoteToSpend;
            } else {
                // Selling base for quote: capacity = qty (in base)
                double baseToSell  = std::min(remaining, level.qty);
                double quoteGained = baseToSell * level.price;
                received  += quoteGained * (1.0 - fee);
                remaining -= baseToSell;
            }
        }

        // If any input remains unspent, the book did not have enough depth
        if (remaining > 1e-12) {
            insufficientLiquidity = true;
        }

        return received;
    }

public:
    /**
     * @brief Constructs a SimulationEngine with a shared order book store.
     *
     * @param store Reference to the shared OrderBookStore (must outlive this object)
     * @param fee   Taker fee to apply to each trade leg (e.g. 0.001 = 0.1%)
     */
    explicit SimulationEngine(const OrderBookStore& store, double fee)
        : store(store), fee(fee) {}

    /**
     * @brief Simulates a full arbitrage route at multiple notional sizes.
     *
     * For each notional in @p notionals, walks the full route step by step,
     * calling simulateLeg() at each hop. Prints a formatted result table
     * to stdout and returns all results as a vector.
     *
     * If any step's order book is missing (not yet received from Binance),
     * that notional is skipped and no SimResult is produced for it.
     *
     * @param steps        Ordered list of (from, to) currency pairs, e.g.
     *                     {{"USDT","ETH"}, {"ETH","BTC"}, {"BTC","USDT"}}
     * @param notionals    Trade sizes to simulate (in source currency)
     * @param minProfitPct Threshold used to set SimResult::viable
     * @return             One SimResult per successfully simulated notional
     *
     * @par Example output
     * @code
     * [SIM] Notional $  100  →  net: +0.17%  ✓ viable
     * [SIM] Notional $  500  →  net: +0.09%  ✓ viable
     * [SIM] Notional $ 1000  →  net: -0.03%  ✗ not viable (book depth exhausted)
     * @endcode
     */
    std::vector<SimResult> simulate(
        const std::vector<std::pair<std::string, std::string>>& steps,
        const std::vector<double>& notionals,
        double minProfitPct) const {

        std::vector<SimResult> results;
        results.reserve(notionals.size());

        for (double notional : notionals) {
            double amount          = notional;
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
            results.push_back({notional, amount, netPct,
                                netPct >= minProfitPct, anyInsufficient});
        }

        // Print formatted results
        if (!results.empty()) {
            std::cout << std::fixed << std::setprecision(4);
            for (const auto& r : results) {
                std::cout << "[SIM] Notional $" << std::setw(7) << r.notional
                          << "  →  net: "
                          << (r.netProfitPct >= 0 ? "+" : "")
                          << r.netProfitPct << "%";
                std::cout << (r.viable ? "  ✓ viable" : "  ✗ not viable");
                if (r.insufficientLiquidity) std::cout << " (book depth exhausted)";
                std::cout << '\n';
            }
            std::cout << '\n';
        }

        return results;
    }
};