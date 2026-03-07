#include <iostream>
#include "../include/Graph.hpp"

int main() {
    std::cout << "[SYSTEM] Crypto Arbitrage Engine Initialized." << std::endl;
    
    // 1. יצירת הגרף (שוק המטבעות שלנו)
    Graph market;

    // 2. הוספת שערי החליפין (בדיוק לפי הדוגמה שעשינו)
    // USD -> BTC 
    market.addExchangeRate("USD", "BTC", 0.00001, 0.001);
    
    // BTC -> ETH 
    market.addExchangeRate("BTC", "ETH", 25.0, 0.002);
    
    // ETH -> USD 
    market.addExchangeRate("ETH", "USD", 4100.0, 0.001);

    // 3. הדפסת הגרף כדי לוודא שהנתונים נכנסו נכון
    market.printGraph();

    std::cout << "[SYSTEM] Graph is ready for Bellman-Ford algorithm!" << std::endl;

    // מריצים את החיפוש! נתחיל את הבדיקה מהדולר (USD)
    market.findArbitrage("USD");
    
    return 0;
}