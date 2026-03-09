#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <cmath>
#include <limits>

// Structure representing an exchange rate between two currencies
struct Edge {
    int source; // ID of the currency being converted from
    int destination; // ID of the currency being converted to
    double weight; // The actual exchange rate (e.g., 1 USD = 0.85 EUR would have a weight of 0.85)
    double fee; // Transaction fee (as a percentage, e.g., 0.1 for 0.1%)
};

class Graph {
    private:
        int numVertices; // Number of currencies (vertices)
        std::vector<Edge> edges; 

        // Fast lookup map to translate a currency name to 
        // into its internal numeric ID (e.g., "USD" -> 0, "EUR" -> 1)
        std::unordered_map<std::string, int> currencyToIndex;
        std::vector<std::string> indexToCurrency;

    public:
        Graph() : numVertices(0) {}

        // Adds a new currency to the graph and returns its ID
        int addCurrency(const std::string& currency) {
            if (currencyToIndex.find(currency) == currencyToIndex.end()) {
                currencyToIndex[currency] = numVertices;
                indexToCurrency.push_back(currency);
                numVertices++;
            }
            return currencyToIndex[currency];
        }

        // Adds an exchange rate (edge) to the graph
        void addExchangeRate(const std::string& from, const std::string& to, double rate, double fee = 0.0) {
            int u = addCurrency(from);
            int v = addCurrency(to);

            edges.push_back({u, v, rate, fee});
        }

        void updateExchangeRate(const std::string& from, const std::string& to, double rate, double fee = 0.0) {
            int u = addCurrency(from);
            int v = addCurrency(to);

            for (auto& edge : edges) {
                if (edge.source == u && edge.destination == v) {
                    edge.weight = rate;
                    edge.fee = fee;
                    return;
                }  
            }
        }

        int getNumVertices() const { return numVertices; }
        const std::vector<Edge>& getEdges() const { return edges; }
        const std::vector<std::string>& getIndexToCurrency() const { return indexToCurrency; }

        // Prints the graph for debugging purposes
        void printGraph() const {
            std::cout << "\n--- Market Graph ---" << std::endl;
            for (const auto& edge : edges) {
                std::cout << indexToCurrency[edge.source] << " -> " 
                          << indexToCurrency[edge.destination]
                          << " (Rate: " << edge.weight << ")" << std::endl;
            }
            std::cout << "--------------------\n" << std::endl;
        }

        // Runs the Bellman-Ford algorithm to search for arbitrage opportunities
        void findArbitrage(const std::string& sourceCurrency) {
            // Make sure the currency exists in the system
            if (currencyToIndex.find(sourceCurrency) == currencyToIndex.end()) {
                std::cout << "[ERROR] Currency not found in graph!" << std::endl;
                return;
            }
            int sourceNode = currencyToIndex[sourceCurrency];


            // Distance array (initialized to infinity)
            std::vector<double> minDistance(numVertices, std::numeric_limits<double>::infinity());
            // Predecessor array (used later to reconstruct the money path)
            std::vector<int> predecessor(numVertices, -1);

            minDistance[sourceNode] = 0.0;

            // Step 1: Relax all edges V-1 times (the core of the algorithm)
            for (int i = 0; i < numVertices - 1; ++i) {
                for (const auto& edge : edges) {
                    // Convert the exchange rate into a graph weight:
                    // negative logarithm of the rate
                    double actualRate = edge.weight * (1.0 - edge.fee);
                    double weight = -std::log(actualRate); 

                    if (minDistance[edge.source] !=  std::numeric_limits<double>::infinity() &&
                        minDistance[edge.source] +  weight < minDistance[edge.destination]) {
                            minDistance[edge.destination] = minDistance[edge.source] + weight;
                            predecessor[edge.destination] = edge.source;
                    }
                }
            }

            // Step 2: Check whether a negative-weight cycle exists
            // (which means guaranteed profit)
            for (const auto& edge : edges) {
                double actualRate = edge.weight * (1.0 - edge.fee);
                double weight = -std::log(actualRate);
                if (minDistance[edge.source] != std::numeric_limits<double>::infinity() &&
                    minDistance[edge.source] + weight < minDistance[edge.destination]) {
                    std::cout << "\n[$$$] ARBITRAGE OPPORTUNITY DETECTED! [$$$]" << std::endl;
                    
                    // --- Start of path extraction code ---
                    int curr = edge.destination;
                    // Move backward V times to guarantee that we are deep inside the cycle itself
                    for (int i = 0; i < numVertices; ++i) {
                        curr = predecessor[curr];
                    }

                    int  cycleStart = curr;
                    std::vector<int> cycle;
                    
                    // Collect the nodes (currencies) that belong to the cycle
                    do {
                        cycle.push_back(curr);
                        curr = predecessor[curr];
                    } while (curr  != cycleStart);
                    cycle.push_back(cycleStart); 
                    
                    // Print the route (it was collected from end to start, so print it in reverse)
                    std::cout << "[ROUTE] Execute Trade: ";
                    for (int i = cycle.size() - 1; i >= 0; --i) {
                        std::cout << indexToCurrency[cycle[i]];
                        if (i > 0) std::cout << " -> ";
                    }
                    std::cout << "\n" << std::endl;
                    // --- End of path extraction code ---

                    // --- Start of profit calculation code ---
                    double profitMultiplier = 1.0; // Start with "one unit" of the initial currency

                    // Traverse the route (again, from end to start because it is stored in reverse)
                    for (int i = cycle.size() - 1; i > 0; --i) {
                        int fromNode = cycle[i];
                        int toNode = cycle[i - 1];

                        // Find the specific edge in order to retrieve its rate and fee
                        for (const auto& e: edges) {
                            if (e.source ==  fromNode && e.destination == toNode) {
                                double actualRate = e.weight * (1.0 - e.fee);
                                profitMultiplier *= actualRate;
                                break;
                            }
                        }
                    }

                    // Compute the net profit percentage
                    // For example: 1.025 becomes 2.5%
                    double profitPercentage = (profitMultiplier - 1.0) * 100.0;
                    std::cout << "[PROFIT] Expected Arbitrage Profit: " << profitPercentage << "%" << std::endl;
                    std::cout << "-------------------------------------------\n" << std::endl;
                    // --- End of profit calculation ---
                    
                    return;
                }
            }
            std::cout << "\n[SYSTEM] No arbitrage found from " << sourceCurrency << "." << std::endl;
        }
};