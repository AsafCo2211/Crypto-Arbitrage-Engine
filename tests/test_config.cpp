#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include "Config.hpp"

// ── Helpers ───────────────────────────────────────────────────────────────────

// Writes a JSON string to a temp file and returns its path.
std::string writeTempConfig(const std::string& content) {
    auto path = std::filesystem::temp_directory_path() / "test_config.json";
    std::ofstream f(path);
    f << content;
    return path.string();
}

const std::string validJson = R"({
    "fee": 0.001,
    "scanIntervalMs": 100,
    "minProfitPercent": 0.1,
    "maxQuoteAgeMs": 3000,
    "cooldownSeconds": 10,
    "sourceCurrency": "USDT",
    "simulationNotionals": [100, 500, 1000],
    "symbols": [
        { "stream": "ethusdt", "base": "ETH", "quote": "USDT" },
        { "stream": "ethbtc",  "base": "ETH", "quote": "BTC"  }
    ]
})";

// ── Valid config tests ────────────────────────────────────────────────────────

TEST(ConfigTest, LoadsValidConfigSuccessfully) {
    auto path = writeTempConfig(validJson);
    Config cfg;
    EXPECT_NO_THROW(cfg = Config::load(path));
}

TEST(ConfigTest, CorrectValuesLoaded) {
    auto path = writeTempConfig(validJson);
    auto cfg  = Config::load(path);

    EXPECT_DOUBLE_EQ(cfg.fee,              0.001);
    EXPECT_EQ       (cfg.scanIntervalMs,   100);
    EXPECT_DOUBLE_EQ(cfg.minProfitPercent, 0.1);
    EXPECT_EQ       (cfg.maxQuoteAgeMs,    3000);
    EXPECT_EQ       (cfg.cooldownSeconds,  10);
    EXPECT_EQ       (cfg.sourceCurrency,   "USDT");
}

TEST(ConfigTest, SymbolsLoadedCorrectly) {
    auto path = writeTempConfig(validJson);
    auto cfg  = Config::load(path);

    ASSERT_EQ(cfg.symbols.size(), 2u);
    EXPECT_EQ(cfg.symbols[0].stream, "ethusdt");
    EXPECT_EQ(cfg.symbols[0].base,   "ETH");
    EXPECT_EQ(cfg.symbols[0].quote,  "USDT");
}

TEST(ConfigTest, SimulationNotionalsLoaded) {
    auto path = writeTempConfig(validJson);
    auto cfg  = Config::load(path);

    ASSERT_EQ(cfg.simulationNotionals.size(), 3u);
    EXPECT_DOUBLE_EQ(cfg.simulationNotionals[0], 100.0);
    EXPECT_DOUBLE_EQ(cfg.simulationNotionals[1], 500.0);
    EXPECT_DOUBLE_EQ(cfg.simulationNotionals[2], 1000.0);
}

// ── Error handling tests ──────────────────────────────────────────────────────

TEST(ConfigTest, ThrowsOnMissingFile) {
    EXPECT_THROW(Config::load("/nonexistent/path/config.json"), std::runtime_error);
}

TEST(ConfigTest, ThrowsOnMalformedJson) {
    auto path = writeTempConfig("{ this is not valid json }");
    EXPECT_THROW(Config::load(path), std::runtime_error);
}

TEST(ConfigTest, ThrowsOnNegativeFee) {
    auto path = writeTempConfig(R"({
        "fee": -0.001,
        "scanIntervalMs": 100, "minProfitPercent": 0.1,
        "maxQuoteAgeMs": 3000, "cooldownSeconds": 10,
        "sourceCurrency": "USDT", "simulationNotionals": [100],
        "symbols": [{ "stream": "ethusdt", "base": "ETH", "quote": "USDT" }]
    })");
    EXPECT_THROW(Config::load(path), std::runtime_error);
}

TEST(ConfigTest, ThrowsOnEmptySymbols) {
    auto path = writeTempConfig(R"({
        "fee": 0.001, "scanIntervalMs": 100, "minProfitPercent": 0.1,
        "maxQuoteAgeMs": 3000, "cooldownSeconds": 10,
        "sourceCurrency": "USDT", "simulationNotionals": [100],
        "symbols": []
    })");
    EXPECT_THROW(Config::load(path), std::runtime_error);
}

TEST(ConfigTest, ThrowsOnMissingField) {
    // "fee" is missing entirely.
    // nlohmann::json throws out_of_range (a subclass of std::exception)
    // when a required key is absent via j.at().
    auto path = writeTempConfig(R"({
        "scanIntervalMs": 100, "minProfitPercent": 0.1,
        "maxQuoteAgeMs": 3000, "cooldownSeconds": 10,
        "sourceCurrency": "USDT", "simulationNotionals": [100],
        "symbols": [{ "stream": "ethusdt", "base": "ETH", "quote": "USDT" }]
    })");
    EXPECT_THROW(Config::load(path), std::exception);
}