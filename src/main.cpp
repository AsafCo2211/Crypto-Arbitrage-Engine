#include <iostream>
#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <unordered_map>
#include "Graph.hpp"
#include "BinanceClient.hpp"
#include "Config.hpp"
#include "StatsCollector.hpp"
#include "OrderBook.hpp"
#include "SimulationEngine.hpp"

int main() {
    std::cout << "[SYSTEM] Crypto Arbitrage Engine starting...\n";

    // ── Load config ──────────────────────────────────────────────────────────
    Config cfg;
    try {
        cfg = Config::load("config/config.json");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }

    std::cout << "[CONFIG] fee="        << cfg.fee
              << " | minProfit="        << cfg.minProfitPercent << "%"
              << " | maxQuoteAge="      << cfg.maxQuoteAgeMs    << "ms"
              << " | cooldown="         << cfg.cooldownSeconds  << "s"
              << " | scanInterval="     << cfg.scanIntervalMs   << "ms"
              << " | source="           << cfg.sourceCurrency
              << " | symbols="          << cfg.symbols.size()
              << " | simNotionals=";
    for (size_t i = 0; i < cfg.simulationNotionals.size(); ++i) {
        std::cout << "$" << cfg.simulationNotionals[i];
        if (i + 1 < cfg.simulationNotionals.size()) std::cout << "/";
    }
    std::cout << "\n\n";

    // ── Build symbol registry + register order book edges ────────────────────
    std::unordered_map<std::string, SymbolInfo> symbolRegistry;
    OrderBookStore bookStore;
    std::string    streams;

    for (size_t i = 0; i < cfg.symbols.size(); ++i) {
        const auto& s = cfg.symbols[i];

        std::string upper = s.stream;
        for (char& c : upper)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        symbolRegistry[upper] = {s.base, s.quote};

        // Register edge→book lookups so SimulationEngine can find the right side
        bookStore.registerSymbol(upper, s.base, s.quote);

        streams += s.stream + "@depth5";
        if (i < cfg.symbols.size() - 1) streams += '/';
    }

    // ── Wire components ──────────────────────────────────────────────────────
    StatsCollector   stats;
    SimulationEngine simEngine(bookStore, cfg.fee);
    Graph            market;
    BinanceClient    binance(cfg.fee);
    std::mutex       marketMutex;
    std::atomic<bool> marketDirty{false};
    std::atomic<bool> running{true};

    stats.initCsv("opportunities.csv");

    market.attachStats(stats);
    market.attachSim(simEngine);
    binance.attachStats(stats);
    binance.attachOrderBookStore(bookStore);
    binance.setSymbolRegistry(symbolRegistry);

    std::string url = "wss://stream.binance.com:9443/stream?streams=" + streams;
    binance.connect(url, market, marketMutex, marketDirty);

    // ── Scanner thread ───────────────────────────────────────────────────────
    std::thread scanner([&]() {
        while (running.load(std::memory_order_relaxed)) {
            if (marketDirty.exchange(false, std::memory_order_relaxed)) {
                std::lock_guard<std::mutex> lock(marketMutex);
                market.findArbitrage(
                    cfg.sourceCurrency,
                    cfg.minProfitPercent,
                    cfg.maxQuoteAgeMs,
                    cfg.cooldownSeconds,
                    cfg.simulationNotionals
                );
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.scanIntervalMs));
        }
    });

    // ── Summary thread ───────────────────────────────────────────────────────
    std::thread summaryThread([&]() {
        int ticksLeft = 300;
        while (running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!running.load(std::memory_order_relaxed)) break;
            if (--ticksLeft == 0) {
                stats.printSummary();
                ticksLeft = 300;
            }
        }
    });

    std::cout << "[SYSTEM] Engine running — listening to "
              << cfg.symbols.size() << " pairs.\n";
    std::cout << "[SYSTEM] Stats summary every 30s. Press Enter to stop.\n\n";

    std::cin.get();

    running.store(false, std::memory_order_relaxed);
    if (scanner.joinable())       scanner.join();
    if (summaryThread.joinable()) summaryThread.join();

    std::cout << "\n[SYSTEM] Final stats:\n";
    stats.printSummary();
    std::cout << "[SYSTEM] Engine stopped.\n";

    return 0;
}