cmake_minimum_required(VERSION 3.16)

project(CryptoArbitrageEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ─────────────────────────────────────────────────────────────

# OpenSSL: prefer Homebrew on macOS, fall back to system install elsewhere
if(APPLE)
    set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl@3)
endif()

find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(USE_TLS      ON CACHE BOOL "Enable TLS for IXWebSocket" FORCE)
set(USE_OPEN_SSL ON CACHE BOOL "Use OpenSSL"                FORCE)

add_subdirectory(external/IXWebSocket)

# ── Main engine ───────────────────────────────────────────────────────────────

add_executable(ArbitrageEngine src/main.cpp)

target_include_directories(ArbitrageEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ArbitrageEngine PRIVATE
    ixwebsocket::ixwebsocket
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
)

# ── Unit tests ────────────────────────────────────────────────────────────────
# Tests have no dependency on IXWebSocket or OpenSSL — they test pure logic only.
# Enable with: cmake -DBUILD_TESTS=ON ..

option(BUILD_TESTS "Build unit tests" OFF)

if(BUILD_TESTS)
    enable_testing()

    # Fetch GoogleTest automatically — no manual install required
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(ArbitrageTests
        tests/test_graph.cpp
        tests/test_simulation.cpp
        tests/test_config.cpp
    )

    target_include_directories(ArbitrageTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(ArbitrageTests PRIVATE
        GTest::gtest_main
        nlohmann_json::nlohmann_json
    )

    include(GoogleTest)
    gtest_discover_tests(ArbitrageTests)
endif()