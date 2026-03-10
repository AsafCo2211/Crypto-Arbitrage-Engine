#pragma once

#include <iostream>
#include <string>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>
#include "Graph.hpp"
#include "Config.hpp"
#include "StatsCollector.hpp"
#include "OrderBook.hpp"

// Maps an uppercase symbol string (e.g. "ETHBTC") to its base/quote pair.
struct SymbolInfo {
    std::string base;
    std::string quote;
};

class BinanceClient {
private:
    ix::WebSocket webSocket;

    static constexpr bool DEBUG_TICKS  = false;
    static constexpr bool DEBUG_ERRORS = true;

    std::unordered_map<std::string, SymbolInfo> symbolRegistry;

    double          fee;
    StatsCollector* stats = nullptr;
    OrderBookStore* bookStore = nullptr;

    // Parses a JSON array of [price, qty] string pairs into PriceLevels.
    static std::vector<PriceLevel> parseLevels(const nlohmann::json& arr) {
        std::vector<PriceLevel> levels;
        levels.reserve(arr.size());
        for (const auto& entry : arr) {
            double price = std::stod(entry[0].get<std::string>());
            double qty   = std::stod(entry[1].get<std::string>());
            if (price > 0.0 && qty > 0.0) {
                levels.push_back({price, qty});
            }
        }
        return levels;
    }

public:
    explicit BinanceClient(double fee) : fee(fee) {
        ix::initNetSystem();
    }

    void attachStats(StatsCollector& collector) { stats = &collector; }
    void attachOrderBookStore(OrderBookStore& store) { bookStore = &store; }

    void setSymbolRegistry(const std::unordered_map<std::string, SymbolInfo>& registry) {
        symbolRegistry = registry;
    }

    void connect(const std::string& url, Graph& market,
                 std::mutex& marketMutex, std::atomic<bool>& marketDirty) {

        webSocket.setUrl(url);

        webSocket.setOnMessageCallback(
            [this, &market, &marketMutex, &marketDirty](const ix::WebSocketMessagePtr& msg) {

            if (msg->type == ix::WebSocketMessageType::Message) {
                try {
                    auto j = nlohmann::json::parse(msg->str);
                    if (!j.contains("stream") || !j.contains("data")) return;

                    // Extract symbol from stream name: "ethusdt@depth5" → "ETHUSDT"
                    std::string streamName = j["stream"].get<std::string>();
                    auto atPos = streamName.find('@');
                    if (atPos == std::string::npos) return;

                    std::string symbolLower = streamName.substr(0, atPos);
                    std::string symbol;
                    symbol.reserve(symbolLower.size());
                    for (char c : symbolLower)
                        symbol += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

                    auto symIt = symbolRegistry.find(symbol);
                    if (symIt == symbolRegistry.end()) return;

                    const std::string& base  = symIt->second.base;
                    const std::string& quote = symIt->second.quote;

                    const auto& data = j["data"];
                    if (!data.contains("asks") || !data.contains("bids")) return;

                    auto asks = parseLevels(data["asks"]); // ascending  price
                    auto bids = parseLevels(data["bids"]); // descending price

                    if (asks.empty() || bids.empty()) return;

                    double bestAsk = asks.front().price;
                    double bestBid = bids.front().price;

                    // Validation: reject zero or negative best prices.
                    if (bestAsk <= 0.0 || bestBid <= 0.0) {
                        if (stats) stats->recordRejectedQuote();
                        return;
                    }

                    // Update order book store for slippage simulation
                    if (bookStore) {
                        bookStore->updateBook(symbol, std::move(asks), std::move(bids));
                    }

                    // Update graph with best bid/ask for Bellman-Ford
                    {
                        std::lock_guard<std::mutex> lock(marketMutex);
                        // quote→base: buying base costs ask  → rate = 1/ask
                        // base→quote: selling base yields bid → rate = bid
                        market.updateExchangeRate(quote, base, 1.0 / bestAsk, fee);
                        market.updateExchangeRate(base, quote, bestBid,        fee);
                    }
                    marketDirty.store(true, std::memory_order_relaxed);
                    if (stats) stats->recordTick();

                    if (DEBUG_TICKS) {
                        std::cout << "[TICK] " << symbol
                                  << " ask=" << bestAsk << " bid=" << bestBid << '\n';
                    }

                } catch (const std::exception& e) {
                    if (DEBUG_ERRORS) {
                        std::cerr << "[ERROR] " << e.what() << '\n';
                    }
                }

            } else if (msg->type == ix::WebSocketMessageType::Open) {
                std::cout << "[SYSTEM] Connected to Binance depth stream.\n";

            } else if (msg->type == ix::WebSocketMessageType::Error) {
                std::cerr << "[ERROR] WebSocket: " << msg->errorInfo.reason << '\n';
            }
        });

        std::cout << "[SYSTEM] Connecting to " << url << " ...\n";
        webSocket.start();
    }
};