#include <iostream>
#include "../include/Graph.hpp"
#include "../include/BinanceClient.hpp" 

int main() {
    std::cout << "[SYSTEM] Crypto Arbitrage Engine Initialized." << std::endl;
    
    // 1. יצירת הגרף הבסיסי
    Graph market;
    market.addCurrency("USDT");
    market.addCurrency("BTC");
    market.addCurrency("ETH");
    // market.addExchangeRate("USD", "BTC", 0.00001, 0.001);
    // market.addExchangeRate("BTC", "ETH", 25.0, 0.002);
    // market.addExchangeRate("ETH", "USD", 4100.0, 0.001);
    // market.printGraph();

    // 2. חיבור חי לבורסה!
    BinanceClient binance;
    // ערוץ העדכונים הרציף של BTC/USDT ב-Binance
    std::string streams = "btcusdt@bookTicker/ethbtc@bookTicker/ethusdt@bookTicker";
    std::string url = "wss://stream.binance.com:9443/stream?streams=" + streams;
    // binance.connect("wss://stream.binance.com:9443/ws/btcusdt@bookTicker", market);
    binance.connect(url, market);
    // 3. תקיעת התוכנית כדי שהרקע ימשיך לרוץ
    std::cout << "\n[SYSTEM] Engine is running. Press ENTER to stop..." << std::endl;
    std::cin.get(); 

    return 0;
}