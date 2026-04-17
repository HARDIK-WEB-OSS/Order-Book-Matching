/*
 * stress_test.cpp — 100 000-order benchmark
 *
 * Build: see CMakeLists.txt target `benchmark`
 * Run:   ./build/benchmark/stress_test
 */

#include "../engine/order.hpp"
#include "../engine/order_book.hpp"
#include "../engine/matching_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Config
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int    TOTAL_ORDERS    = 100'000;
static constexpr double PCT_LIMIT       = 0.60;
static constexpr double PCT_MARKET      = 0.30;
// PCT_CANCEL = 0.10 (implied)
static constexpr double MID_PRICE       = 1000.0;
static constexpr double PRICE_RANGE_PCT = 0.02;
static constexpr double MIN_QTY         = 1.0;
static constexpr double MAX_QTY         = 100.0;
static constexpr uint32_t SEED          = 42;

// ─────────────────────────────────────────────────────────────────────────────
// Order spec (pre-generated)
// ─────────────────────────────────────────────────────────────────────────────
struct OrderSpec {
    enum class Kind { LIMIT, MARKET, CANCEL } kind;
    engine::Side side;
    double price = 0.0;
    double qty   = 0.0;
    int    cancel_idx = -1;  // index into placed_ids list
};

static std::vector<OrderSpec> generate_specs() {
    std::mt19937 rng(SEED);
    std::uniform_real_distribution<double> r01(0.0, 1.0);
    std::uniform_real_distribution<double> price_dist(
        MID_PRICE * (1.0 - PRICE_RANGE_PCT),
        MID_PRICE * (1.0 + PRICE_RANGE_PCT));
    std::uniform_real_distribution<double> qty_dist(MIN_QTY, MAX_QTY);

    std::vector<OrderSpec> specs;
    specs.reserve(TOTAL_ORDERS);
    int placed = 0;

    for (int i = 0; i < TOTAL_ORDERS; ++i) {
        double roll = r01(rng);
        OrderSpec spec;

        if (roll < PCT_LIMIT) {
            spec.kind  = OrderSpec::Kind::LIMIT;
            spec.side  = (r01(rng) < 0.5) ? engine::Side::BUY : engine::Side::SELL;
            spec.price = std::round(price_dist(rng) * 100.0) / 100.0;
            spec.qty   = std::round(qty_dist(rng) * 10000.0) / 10000.0;
            ++placed;
        } else if (roll < PCT_LIMIT + PCT_MARKET) {
            spec.kind = OrderSpec::Kind::MARKET;
            spec.side = (r01(rng) < 0.5) ? engine::Side::BUY : engine::Side::SELL;
            spec.qty  = std::round(qty_dist(rng) * 10000.0) / 10000.0;
        } else {
            spec.kind = OrderSpec::Kind::CANCEL;
            if (placed > 0) {
                std::uniform_int_distribution<int> idx_dist(0, placed - 1);
                spec.cancel_idx = idx_dist(rng);
            }
        }
        specs.push_back(spec);
    }
    return specs;
}

// ─────────────────────────────────────────────────────────────────────────────
// Percentile helper
// ─────────────────────────────────────────────────────────────────────────────
static double percentile(std::vector<double>& sorted_data, double p) {
    if (sorted_data.empty()) return 0.0;
    double idx = p / 100.0 * (sorted_data.size() - 1);
    auto   lo  = (size_t)idx;
    double frac = idx - lo;
    if (lo + 1 >= sorted_data.size()) return sorted_data.back();
    return sorted_data[lo] * (1.0 - frac) + sorted_data[lo + 1] * frac;
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    auto specs = generate_specs();

    engine::MatchingEngine eng;
    std::vector<std::string> placed_ids;
    placed_ids.reserve(TOTAL_ORDERS);

    std::vector<double> latencies_us;
    latencies_us.reserve(TOTAL_ORDERS);

    std::cout << "\n"
              << "══════════════════════════════════════════════════════\n"
              << "  Order Book Matching Engine — Stress Test (C++17)\n"
              << "  " << TOTAL_ORDERS << " orders  |  seed=" << SEED << "\n"
              << "══════════════════════════════════════════════════════\n"
              << "  Running... " << std::flush;

    auto t_start = std::chrono::high_resolution_clock::now();

    for (auto& spec : specs) {
        auto t0 = std::chrono::high_resolution_clock::now();

        if (spec.kind == OrderSpec::Kind::LIMIT) {
            auto order = std::make_shared<engine::Order>(
                spec.side, engine::OrderType::LIMIT, spec.qty,
                std::optional<double>{spec.price});
            eng.process_order(order);
            placed_ids.push_back(order->id);
        } else if (spec.kind == OrderSpec::Kind::MARKET) {
            auto order = std::make_shared<engine::Order>(
                spec.side, engine::OrderType::MARKET, spec.qty,
                std::nullopt);
            eng.process_order(order);
        } else {
            // CANCEL
            if (spec.cancel_idx >= 0 && spec.cancel_idx < (int)placed_ids.size()) {
                eng.cancel_by_id(placed_ids[spec.cancel_idx]);
            } else {
                eng.cancel_by_id("nonexistent-id");
            }
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        latencies_us.push_back(us);
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t_end - t_start).count();
    double throughput = TOTAL_ORDERS / elapsed;

    std::sort(latencies_us.begin(), latencies_us.end());
    double avg = std::accumulate(latencies_us.begin(), latencies_us.end(), 0.0)
                 / latencies_us.size();
    double p50 = percentile(latencies_us, 50.0);
    double p95 = percentile(latencies_us, 95.0);
    double p99 = percentile(latencies_us, 99.0);

    std::cout << "done.\n\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  Total elapsed      : " << elapsed   << " s\n";
    std::cout << std::setprecision(0);
    std::cout << "  Throughput         : " << throughput << " orders/sec\n";
    std::cout << std::setprecision(3);
    std::cout << "  Avg latency        : " << avg       << " µs\n";
    std::cout << "  p50 latency        : " << p50       << " µs\n";
    std::cout << "  p95 latency        : " << p95       << " µs\n";
    std::cout << "  p99 latency        : " << p99       << " µs\n";
    std::cout << "  Trades executed    : " << eng.total_trades_executed()  << "\n";
    std::cout << std::setprecision(2);
    std::cout << "  Volume traded      : " << eng.total_volume_traded()    << "\n";
    std::cout << "══════════════════════════════════════════════════════\n\n";

    // ── Save results.json ─────────────────────────────────────────────────
    std::ostringstream js;
    js << std::fixed;
    js << "{\n"
       << "  \"total_orders\": "               << TOTAL_ORDERS                    << ",\n"
       << "  \"elapsed_seconds\": "            << std::setprecision(4) << elapsed  << ",\n"
       << "  \"throughput_orders_per_sec\": "  << std::setprecision(2) << throughput << ",\n"
       << "  \"avg_latency_us\": "             << std::setprecision(3) << avg       << ",\n"
       << "  \"p50_latency_us\": "             << p50                               << ",\n"
       << "  \"p95_latency_us\": "             << p95                               << ",\n"
       << "  \"p99_latency_us\": "             << p99                               << ",\n"
       << "  \"total_trades_executed\": "      << eng.total_trades_executed()       << ",\n"
       << "  \"total_volume_traded\": "        << std::setprecision(2)
                                               << eng.total_volume_traded()         << "\n"
       << "}\n";

    std::ofstream out("benchmark/results.json");
    out << js.str();
    std::cout << "  Results saved to benchmark/results.json\n\n";

    return 0;
}
