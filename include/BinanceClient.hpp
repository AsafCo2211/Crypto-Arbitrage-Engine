#pragma once

#include <iostream>
#include <string>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>
#include "Graph.hpp"

class BinanceClient {
    private:
        ix::WebSocket webSocket;
      
    public:
        BinanceClient() {
            ix::initNetSystem();
        };

        void connect(const std::string& url, Graph& market) { 
            webSocket.setUrl(url);

            webSocket.setOnMessageCallback([&market](const ix::WebSocketMessagePtr& msg) {
                if (msg->type == ix::WebSocketMessageType::Message) {
                    try {
                        auto j  = nlohmann::json::parse(msg->str);

                        if (j.contains("data")) {
                            auto data = j["data"];
                            std::string symbol = j["s"];
                            double ask = std::stod(data["a"].get<std::string>());
                            double bid = std::stod(data["b"].get<std::string>());

                            std::string base, quote;

                            if (symbol == "BTCUSDT") { base = "BTC"; quote = "USDT"; }
                            else if (symbol == "ETHBTC") { base = "ETH"; quote = "BTC"; }
                            else if (symbol == "ETHUSDT") { base = "ETH"; quote = "USDT"; }

                            if (!base.empty() && !quote.empty()) {
                                market.updateExchangeRate(quote, base, 1.0 / ask, 0.001);
                                market.updateExchangeRate(base, quote, bid, 0.001);

                                market.findArbitrage("USDT");
                            }
                        }

                    } catch (const std::exception& e) {
                        
                    }
                }
                else if (msg->type == ix::WebSocketMessageType::Open) {
                    std::cout << "[SYSTEM] Connected to Binance Live Stream! Listening to the Triangle..." << std::endl;
                }   
                else if (msg->type == ix::WebSocketMessageType::Error) {
                    std::cout << "[ERROR] Connection error: " << msg->errorInfo.reason << std::endl;
                }             
            });
            std::cout << "[SYSTEM] Starting connection to " << url << "..." << std::endl;
            webSocket.start();
        }
};