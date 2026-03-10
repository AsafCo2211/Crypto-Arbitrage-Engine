#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <chrono>
#include <cmath>
#include <limits>
#include <algorithm>
#include "StatsCollector.hpp"
#include "SimulationEngine.hpp"

using Clock     = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

// Represents a directed exchange-rate edge between two currencies.
struct Edge {
    int    source;      // Numeric ID of the currency being converted from
    int    destination; // Numeric ID of the currency being converted to
    double weight;      // Raw exchange rate (e.g. 1 ETH = 3200 USDT → weight = 3200)
    double fee;         // Taker fee for this leg (e.g. 0.001 = 0.1%)
    TimePoint lastUpdated; // When this rate was last received from the exchange
};

class Graph {
private:
    int numVertices;
    std::vector<Edge> edges;
    std::unordered_map<std::string, size_t> edgeIndex;

    std::unordered_map<std::string, int>         currencyToIndex;
    std::vector<std::string>                     indexToCurrency;

    // Dedup: maps a cycle signature → time it was last reported.
    // Prevents the same route from flooding the terminal every 100ms.
    std::unordered_map<std::string, TimePoint>   lastReported;

    StatsCollector*   stats = nullptr;
    SimulationEngine* sim   = nullptr;

    static constexpr bool DEBUG_GRAPH_UPDATES = false;

    std::string makeEdgeKey(int src, int dst) const {
        return std::to_string(src) + "->" + std::to_string(dst);
    }

    // Builds a stable string signature for a cycle so we can dedup it.
    // e.g.  USDT->ETH->BTC->USDT
    std::string cycleSignature(const std::vector<int>& cycle) const {
        std::string sig;
        for (int i = static_cast<int>(cycle.size()) - 1; i >= 0; --i) {
            sig += indexToCurrency[cycle[i]];
            if (i > 0) sig += "->";
        }
        return sig;
    }

public:
    Graph() : numVertices(0) {}

    void attachStats(StatsCollector& collector) { stats = &collector; }
    void attachSim(SimulationEngine& engine)    { sim   = &engine;    }

    // Adds a new currency to the graph if it does not already exist.
    int addCurrency(const std::string& currency) {
        if (currencyToIndex.find(currency) == currencyToIndex.end()) {
            currencyToIndex[currency] = numVertices;
            indexToCurrency.push_back(currency);
            numVertices++;
        }
        return currencyToIndex[currency];
    }

    // Adds or updates a directed exchange-rate edge.
    // The timestamp is always refreshed to now so freshness checks work correctly.
    void updateExchangeRate(const std::string& from, const std::string& to,
                            double rate, double fee = 0.0) {
        int u = addCurrency(from);
        int v = addCurrency(to);

        std::string key = makeEdgeKey(u, v);
        auto it = edgeIndex.find(key);

        if (it != edgeIndex.end()) {
            Edge& edge    = edges[it->second];
            edge.weight      = rate;
            edge.fee         = fee;
            edge.lastUpdated = Clock::now();

            if (DEBUG_GRAPH_UPDATES) {
                std::cout << "[GRAPH] Updated: " << from << " -> " << to
                          << " | rate=" << rate << " | fee=" << fee << '\n';
            }
            return;
        }

        edges.push_back({u, v, rate, fee, Clock::now()});
        edgeIndex[key] = edges.size() - 1;

        if (DEBUG_GRAPH_UPDATES) {
            std::cout << "[GRAPH] Added: " << from << " -> " << to
                      << " | rate=" << rate << " | fee=" << fee << '\n';
        }
    }

    int getNumVertices()                       const { return numVertices; }
    const std::vector<Edge>& getEdges()        const { return edges; }
    const std::vector<std::string>& getIndexToCurrency() const { return indexToCurrency; }

    void printGraph() const {
        std::cout << "\n--- Market Graph ---\n";
        for (const auto& edge : edges) {
            std::cout << indexToCurrency[edge.source] << " -> "
                      << indexToCurrency[edge.destination]
                      << " (Rate: " << edge.weight << ")\n";
        }
        std::cout << "--------------------\n\n";
    }

    // Runs Bellman-Ford from sourceCurrency and reports any arbitrage cycles found.
    //
    // Parameters:
    //   sourceCurrency   – starting node (e.g. "USDT")
    //   minProfitPercent – only report cycles with net profit above this threshold (%)
    //   maxQuoteAgeMs    – skip edges whose quote is older than this many milliseconds
    //   cooldownSeconds  – suppress re-reporting the same cycle within this window (seconds)
    void findArbitrage(const std::string& sourceCurrency,
                       double minProfitPercent = 0.1,
                       int    maxQuoteAgeMs    = 3000,
                       int    cooldownSeconds  = 10,
                       const std::vector<double>& simNotionals = {}) {

        if (currencyToIndex.find(sourceCurrency) == currencyToIndex.end()) {
            std::cerr << "[ERROR] Source currency not found in graph: " << sourceCurrency << '\n';
            return;
        }

        if (stats) stats->recordScan();

        const int sourceNode = currencyToIndex[sourceCurrency];
        const auto now       = Clock::now();

        // ── Step 1: Relax edges V-1 times ───────────────────────────────────────
        std::vector<double> dist(numVertices, std::numeric_limits<double>::infinity());
        std::vector<int>    pred(numVertices, -1);
        dist[sourceNode] = 0.0;

        for (int i = 0; i < numVertices - 1; ++i) {
            for (const auto& edge : edges) {

                // Freshness check: ignore stale quotes.
                // A stale rate could make a dead opportunity look live.
                auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now - edge.lastUpdated).count();
                if (ageMs > maxQuoteAgeMs) continue;

                double actualRate = edge.weight * (1.0 - edge.fee);

                // Guard: log(0) = -inf, log(negative) = NaN — both corrupt dist[].
                if (actualRate <= 0.0) continue;

                double w = -std::log(actualRate);

                if (dist[edge.source] != std::numeric_limits<double>::infinity() &&
                    dist[edge.source] + w < dist[edge.destination]) {
                    dist[edge.destination] = dist[edge.source] + w;
                    pred[edge.destination] = edge.source;
                }
            }
        }

        // ── Step 2: Detect negative-weight cycles ───────────────────────────────
        for (const auto& edge : edges) {

            auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - edge.lastUpdated).count();
            if (ageMs > maxQuoteAgeMs) continue;

            double actualRate = edge.weight * (1.0 - edge.fee);
            if (actualRate <= 0.0) continue;

            double w = -std::log(actualRate);

            if (dist[edge.source] == std::numeric_limits<double>::infinity() ||
                dist[edge.source] + w >= dist[edge.destination]) continue;

            // ── Cycle extraction ────────────────────────────────────────────────
            int curr = edge.destination;

            // Walk back V times to ensure we land inside the cycle,
            // not on a path leading into it.
            for (int i = 0; i < numVertices; ++i) {
                if (pred[curr] == -1) {
                    std::cerr << "[WARN] Cycle extraction aborted: broken predecessor chain.\n";
                    return;
                }
                curr = pred[curr];
            }

            const int      cycleStart = curr;
            std::vector<int> cycle;
            int            maxSteps  = numVertices;

            do {
                cycle.push_back(curr);
                if (pred[curr] == -1) {
                    std::cerr << "[WARN] Cycle extraction aborted mid-walk.\n";
                    return;
                }
                curr = pred[curr];
                --maxSteps;
            } while (curr != cycleStart && maxSteps > 0);

            if (curr != cycleStart) {
                std::cerr << "[WARN] Cycle extraction aborted: start node not reached.\n";
                return;
            }
            cycle.push_back(cycleStart);

            // ── Profit calculation ──────────────────────────────────────────────
            double profitMultiplier = 1.0;
            bool   edgeMissing      = false;

            for (int i = static_cast<int>(cycle.size()) - 1; i > 0; --i) {
                int from = cycle[i];
                int to   = cycle[i - 1];

                auto it = edgeIndex.find(makeEdgeKey(from, to));
                if (it == edgeIndex.end()) {
                    std::cerr << "[WARN] Profit calc aborted: missing edge "
                              << indexToCurrency[from] << " -> " << indexToCurrency[to] << '\n';
                    edgeMissing = true;
                    break;
                }
                const Edge& e = edges[it->second];
                profitMultiplier *= e.weight * (1.0 - e.fee);
            }

            if (edgeMissing) return;

            double profitPct = (profitMultiplier - 1.0) * 100.0;

            // ── Profit threshold ────────────────────────────────────────────────
            // Skip tiny noise that would disappear after real execution costs.
            if (profitPct < minProfitPercent) return;

            // ── Dedup / cooldown ────────────────────────────────────────────────
            // Build a stable route string and check when we last reported it.
            std::string sig = cycleSignature(cycle);
            auto dedupIt    = lastReported.find(sig);

            if (dedupIt != lastReported.end()) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                   now - dedupIt->second).count();
                if (elapsed < cooldownSeconds) return; // still in cooldown
            }
            lastReported[sig] = now;

            // ── Report ─────────────────────────────────────────────────────────
            std::cout << "\n[$$$] ARBITRAGE OPPORTUNITY DETECTED! [$$$]\n";
            std::cout << "[ROUTE]  " << sig << '\n';
            std::cout << "[PROFIT] Theoretical: " << profitPct << "%\n\n";

            // ── Slippage simulation ─────────────────────────────────────────────
            // Walk the cycle as (from, to) pairs and run each notional size
            // through the actual order book to get a realistic net profit.
            if (sim && !simNotionals.empty()) {
                std::vector<std::pair<std::string, std::string>> steps;
                for (int i = static_cast<int>(cycle.size()) - 1; i > 0; --i) {
                    steps.emplace_back(indexToCurrency[cycle[i]],
                                       indexToCurrency[cycle[i - 1]]);
                }
                sim->simulate(steps, simNotionals, minProfitPercent);
            }

            std::cout << "-------------------------------------------\n\n";

            if (stats) stats->recordOpportunity(sig, profitPct);

            return;
        }
    }
};