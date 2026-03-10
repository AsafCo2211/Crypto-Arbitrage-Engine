#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

// Holds the stream name and currency pair for one trading symbol.
struct SymbolConfig {
    std::string stream; // e.g. "ethbtc"
    std::string base;   // e.g. "ETH"
    std::string quote;  // e.g. "BTC"
};

// All runtime parameters loaded from config/config.json.
// Nothing in this struct is hardcoded — change the JSON file, rerun, done.
struct Config {
    double      fee;              // Taker fee per trade leg (e.g. 0.001 = 0.1%)
    int         scanIntervalMs;   // How often the scanner thread runs Bellman-Ford (ms)
    double      minProfitPercent; // Minimum net profit to report an opportunity (%)
    int         maxQuoteAgeMs;    // Reject quotes older than this from the graph (ms)
    int         cooldownSeconds;  // Seconds before re-reporting the same route
    std::string sourceCurrency;   // Starting node for Bellman-Ford (e.g. "USDT")

    // Notional sizes (in sourceCurrency) to simulate when an opportunity is found.
    // e.g. [100, 500, 1000] → "if I traded $100 / $500 / $1000, what would I net?"
    std::vector<double> simulationNotionals;

    std::vector<SymbolConfig> symbols;

    // Loads and validates config from the given file path.
    // Throws std::runtime_error if the file is missing or malformed.
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
        cfg.fee              = j.at("fee").get<double>();
        cfg.scanIntervalMs   = j.at("scanIntervalMs").get<int>();
        cfg.minProfitPercent = j.at("minProfitPercent").get<double>();
        cfg.maxQuoteAgeMs    = j.at("maxQuoteAgeMs").get<int>();
        cfg.cooldownSeconds  = j.at("cooldownSeconds").get<int>();
        cfg.sourceCurrency   = j.at("sourceCurrency").get<std::string>();
        cfg.simulationNotionals = j.at("simulationNotionals").get<std::vector<double>>();

        for (const auto& s : j.at("symbols")) {
            cfg.symbols.push_back({
                s.at("stream").get<std::string>(),
                s.at("base").get<std::string>(),
                s.at("quote").get<std::string>()
            });
        }

        // Basic sanity checks — fail loudly rather than run with bad config.
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