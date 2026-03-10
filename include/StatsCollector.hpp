#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

using SysClock = std::chrono::system_clock;

// Thread-safe collector for runtime statistics and CSV export.
//
// Maintains two sets of counters:
//   - "window"     counters: reset every 30s summary (shows rate per window)
//   - "cumulative" counters: never reset (shows totals for the whole run)
//
// Two threads write to this object:
//   - WebSocket callback  → recordTick(), recordRejectedQuote()
//   - Scanner thread      → recordScan(), recordOpportunity()
class StatsCollector {
private:
    mutable std::mutex mutex;

    // ── Per-window counters (reset on each printSummary call) ─────────────────
    long long wTicks         = 0;
    long long wRejected      = 0;
    long long wScans         = 0;
    long long wOpportunities = 0;
    double    wBestProfit    = 0.0;
    double    wTotalProfit   = 0.0;

    // ── Cumulative counters (never reset) ─────────────────────────────────────
    long long cTicks         = 0;
    long long cRejected      = 0;
    long long cScans         = 0;
    long long cOpportunities = 0;
    double    cBestProfit    = 0.0;
    double    cTotalProfit   = 0.0;

    // ── CSV ───────────────────────────────────────────────────────────────────
    std::ofstream csvFile;
    bool          csvOpen = false;

    static std::string wallClockNow() {
        auto now   = SysClock::now();
        auto timer = SysClock::to_time_t(now);
        std::tm bt{};
#ifdef _WIN32
        localtime_s(&bt, &timer);
#else
        localtime_r(&timer, &bt);
#endif
        std::ostringstream oss;
        oss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

public:
    // Opens (or creates) the CSV file and writes the header row.
    // Prints the absolute path so the user knows exactly where to find it.
    void initCsv(const std::string& path = "opportunities.csv") {
        std::lock_guard<std::mutex> lock(mutex);

        csvFile.open(path, std::ios::out | std::ios::app);
        if (!csvFile.is_open()) {
            std::cerr << "[STATS] Could not open CSV file: " << path << '\n';
            return;
        }

        // Write header only if the file is empty
        csvFile.seekp(0, std::ios::end);
        if (csvFile.tellp() == 0) {
            csvFile << "timestamp,route,profit_pct\n";
        }

        csvOpen = true;

        // Print absolute path so there's no confusion about where the file is
        std::error_code ec;
        auto abs = std::filesystem::absolute(path, ec);
        std::cout << "[STATS] CSV logging to: "
                  << (ec ? path : abs.string()) << '\n';
    }

    // ── Recording methods ─────────────────────────────────────────────────────

    void recordTick() {
        std::lock_guard<std::mutex> lock(mutex);
        ++wTicks; ++cTicks;
    }

    // Called when a quote with ask<=0 or bid<=0 is received and discarded.
    // In normal market conditions this count stays at 0 — a non-zero value
    // signals a data quality issue worth investigating.
    void recordRejectedQuote() {
        std::lock_guard<std::mutex> lock(mutex);
        ++wRejected; ++cRejected;
    }

    void recordScan() {
        std::lock_guard<std::mutex> lock(mutex);
        ++wScans; ++cScans;
    }

    void recordOpportunity(const std::string& route, double profitPct) {
        std::lock_guard<std::mutex> lock(mutex);

        ++wOpportunities; ++cOpportunities;
        wTotalProfit += profitPct; cTotalProfit += profitPct;
        if (profitPct > wBestProfit) wBestProfit = profitPct;
        if (profitPct > cBestProfit) cBestProfit = profitPct;

        if (csvOpen) {
            csvFile << wallClockNow() << ','
                    << route          << ','
                    << std::fixed << std::setprecision(4)
                    << profitPct      << '\n';
            csvFile.flush();
        }
    }

    // ── Periodic summary ──────────────────────────────────────────────────────
    // Prints per-window stats (last 30s) and cumulative totals (whole run),
    // then resets only the window counters.
    void printSummary() {
        std::lock_guard<std::mutex> lock(mutex);

        double wAvg = (wOpportunities > 0) ? wTotalProfit / wOpportunities : 0.0;
        double cAvg = (cOpportunities > 0) ? cTotalProfit / cOpportunities : 0.0;

        std::cout << "\n[STATS] ──────────── Last 30s ────────────\n";
        std::cout << "[STATS] Ticks received:      " << wTicks         << '\n';
        std::cout << "[STATS] Quotes rejected:     " << wRejected      << '\n';
        std::cout << "[STATS] Bellman-Ford scans:  " << wScans         << '\n';
        std::cout << "[STATS] Opportunities found: " << wOpportunities << '\n';
        if (wOpportunities > 0) {
            std::cout << std::fixed << std::setprecision(4);
            std::cout << "[STATS] Best profit:         " << wBestProfit << "%\n";
            std::cout << "[STATS] Avg  profit:         " << wAvg        << "%\n";
        }

        std::cout << "[STATS] ─────────── Total run ───────────\n";
        std::cout << "[STATS] Ticks received:      " << cTicks         << '\n';
        std::cout << "[STATS] Quotes rejected:     " << cRejected      << '\n';
        std::cout << "[STATS] Bellman-Ford scans:  " << cScans         << '\n';
        std::cout << "[STATS] Opportunities found: " << cOpportunities << '\n';
        if (cOpportunities > 0) {
            std::cout << std::fixed << std::setprecision(4);
            std::cout << "[STATS] Best profit:         " << cBestProfit << "%\n";
            std::cout << "[STATS] Avg  profit:         " << cAvg        << "%\n";
        }
        std::cout << "[STATS] ─────────────────────────────────\n\n";

        // Reset window counters only
        wTicks = wRejected = wScans = wOpportunities = 0;
        wBestProfit = wTotalProfit = 0.0;
    }
};