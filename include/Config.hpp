#pragma once

/**
 * @file Config.hpp
 * @brief Runtime configuration loader for the Crypto Arbitrage Engine.
 *
 * All tunable parameters live in config/config.json. This file defines
 * the data structures that hold them and the loader that parses, validates,
 * and exposes them to the rest of the engine.
 *
 * Design principle: nothing is hardcoded. Every threshold, interval, fee,
 * or symbol list can be changed without recompiling.
 */

#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

/**
 * @brief Describes a single trading pair to subscribe to.
 *
 * Each entry maps directly to one Binance WebSocket stream and tells
 * the engine which currencies are involved in that stream.
 *
 * Example (from config.json):
 * @code
 * { "stream": "ethbtc", "base": "ETH", "quote": "BTC" }
 * @endcode
 */
struct SymbolConfig {
    std::string stream; ///< Lowercase stream name, e.g. "ethbtc"
    std::string base;   ///< Base currency of the pair, e.g. "ETH"
    std::string quote;  ///< Quote currency of the pair, e.g. "BTC"
};

/**
 * @brief Holds all runtime parameters for the engine.
 *
 * Loaded once at startup via Config::load(). After construction, this struct
 * is read-only — no component modifies it at runtime.
 *
 * ### Parameter reference
 *
 * | Field                 | Type     | Description |
 * |-----------------------|----------|-------------|
 * | fee                   | double   | Taker fee per trade leg, e.g. 0.001 = 0.1% |
 * | scanIntervalMs        | int      | Milliseconds between Bellman-Ford scans |
 * | minProfitPercent      | double   | Minimum net profit to report (%) |
 * | maxQuoteAgeMs         | int      | Reject quotes older than this (ms) |
 * | cooldownSeconds       | int      | Suppress re-reporting the same route (s) |
 * | sourceCurrency        | string   | Starting node for Bellman-Ford, e.g. "USDT" |
 * | simulationNotionals   | double[] | Trade sizes to simulate in source currency |
 * | symbols               | array    | Trading pairs to subscribe to |
 */
struct Config {
    double      fee;              ///< Taker fee per trade leg (e.g. 0.001 = 0.1%)
    int         scanIntervalMs;   ///< How often the scanner thread runs Bellman-Ford (ms)
    double      minProfitPercent; ///< Minimum net profit to report an opportunity (%)
    int         maxQuoteAgeMs;    ///< Reject quotes older than this from the graph (ms)
    int         cooldownSeconds;  ///< Seconds before re-reporting the same route
    std::string sourceCurrency;   ///< Starting node for Bellman-Ford (e.g. "USDT")

    /**
     * @brief Notional sizes to simulate when an opportunity is found.
     *
     * For each value N, the SimulationEngine will answer:
     * "If I entered this trade with N units of sourceCurrency,
     *  how much would I exit with after walking the real order book?"
     *
     * Example: [100, 500, 1000] → simulate $100, $500, $1000
     */
    std::vector<double> simulationNotionals;

    std::vector<SymbolConfig> symbols; ///< Trading pairs to subscribe to

    /**
     * @brief Loads and validates the engine configuration from a JSON file.
     *
     * Parses config/config.json (or the given path), maps every field to the
     * corresponding struct member, and runs basic sanity checks. Fails loudly
     * on any error so the engine never starts in an inconsistent state.
     *
     * @param path Path to the JSON config file (default: "config/config.json")
     * @return Populated and validated Config struct
     * @throws std::runtime_error if the file is missing, malformed, or fails validation
     *
     * @par Example
     * @code
     * Config cfg = Config::load("config/config.json");
     * std::cout << "Fee: " << cfg.fee << "\n";
     * @endcode
     */
    static Config load(const std::string& path = "config/config.json") {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("[CONFIG] Cannot open config file: " + path);
        }

        nlohmann::json j;
        try {
            file >> j;
        } catch (const nlohmann::json::parse_error& e) {
            throw std::runtime_error(std::string("[CONFIG] JSON parse error: ") + e.what());
        }

        Config cfg;
        cfg.fee                 = j.at("fee").get<double>();
        cfg.scanIntervalMs      = j.at("scanIntervalMs").get<int>();
        cfg.minProfitPercent    = j.at("minProfitPercent").get<double>();
        cfg.maxQuoteAgeMs       = j.at("maxQuoteAgeMs").get<int>();
        cfg.cooldownSeconds     = j.at("cooldownSeconds").get<int>();
        cfg.sourceCurrency      = j.at("sourceCurrency").get<std::string>();
        cfg.simulationNotionals = j.at("simulationNotionals").get<std::vector<double>>();

        for (const auto& s : j.at("symbols")) {
            cfg.symbols.push_back({
                s.at("stream").get<std::string>(),
                s.at("base").get<std::string>(),
                s.at("quote").get<std::string>()
            });
        }

        // Sanity checks — fail loudly rather than silently run with bad config
        if (cfg.fee < 0.0 || cfg.fee >= 1.0)
            throw std::runtime_error("[CONFIG] fee must be in [0, 1)");
        if (cfg.scanIntervalMs <= 0)
            throw std::runtime_error("[CONFIG] scanIntervalMs must be > 0");
        if (cfg.minProfitPercent < 0.0)
            throw std::runtime_error("[CONFIG] minProfitPercent must be >= 0");
        if (cfg.maxQuoteAgeMs <= 0)
            throw std::runtime_error("[CONFIG] maxQuoteAgeMs must be > 0");
        if (cfg.cooldownSeconds < 0)
            throw std::runtime_error("[CONFIG] cooldownSeconds must be >= 0");
        if (cfg.symbols.empty())
            throw std::runtime_error("[CONFIG] symbols list is empty");

        return cfg;
    }
};