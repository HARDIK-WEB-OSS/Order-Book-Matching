#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <sstream>
#include <iomanip>
#include <random>
#include <atomic>

namespace engine {

// ─────────────────────────────────────────────────────────────────────────────
// Enumerations
// ─────────────────────────────────────────────────────────────────────────────

enum class Side : uint8_t { BUY = 0, SELL = 1 };
enum class OrderType : uint8_t { LIMIT = 0, MARKET = 1, CANCEL = 2 };
enum class OrderStatus : uint8_t { OPEN = 0, PARTIAL = 1, FILLED = 2, CANCELLED = 3 };

// ─────────────────────────────────────────────────────────────────────────────
// UUID generator (simple thread-safe version)
// ─────────────────────────────────────────────────────────────────────────────
inline std::string generate_uuid() {
    static std::atomic<uint64_t> counter{0};
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;

    uint64_t a = dist(rng);
    uint64_t b = dist(rng);
    // Set version 4 and variant bits
    a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8)  << (uint32_t)(a >> 32)         << '-'
       << std::setw(4)  << (uint32_t)((a >> 16) & 0xFFFF) << '-'
       << std::setw(4)  << (uint32_t)(a & 0xFFFF)       << '-'
       << std::setw(4)  << (uint32_t)(b >> 48)          << '-'
       << std::setw(12) << (b & 0x0000FFFFFFFFFFFFULL);
    return ss.str();
}

inline int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

inline double now_unix() {
    return std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// ─────────────────────────────────────────────────────────────────────────────
// Order
// ─────────────────────────────────────────────────────────────────────────────
struct Order {
    std::string    id;
    Side           side;
    OrderType      type;
    std::optional<double> price;   // nullopt for MARKET orders
    double         quantity;
    double         filled_quantity = 0.0;
    int64_t        timestamp;      // nanoseconds (steady_clock)
    OrderStatus    status = OrderStatus::OPEN;

    Order(Side s, OrderType t, double qty,
          std::optional<double> p = std::nullopt)
        : id(generate_uuid()), side(s), type(t),
          price(p), quantity(qty),
          timestamp(now_ns()) {}

    double remaining() const { return quantity - filled_quantity; }

    void fill(double qty) {
        filled_quantity += qty;
        if (filled_quantity >= quantity - 1e-9) {
            filled_quantity = quantity;
            status = OrderStatus::FILLED;
        } else {
            status = OrderStatus::PARTIAL;
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Trade
// ─────────────────────────────────────────────────────────────────────────────
struct Trade {
    std::string trade_id;
    std::string buy_order_id;
    std::string sell_order_id;
    double      price;
    double      quantity;
    int64_t     timestamp;

    Trade(std::string buy_id, std::string sell_id, double p, double q)
        : trade_id(generate_uuid()),
          buy_order_id(std::move(buy_id)),
          sell_order_id(std::move(sell_id)),
          price(p), quantity(q),
          timestamp(now_ns()) {}
};

// ─────────────────────────────────────────────────────────────────────────────
// String helpers (for API serialisation)
// ─────────────────────────────────────────────────────────────────────────────
inline const char* to_string(Side s) {
    return s == Side::BUY ? "BUY" : "SELL";
}
inline const char* to_string(OrderType t) {
    switch (t) {
        case OrderType::LIMIT:  return "LIMIT";
        case OrderType::MARKET: return "MARKET";
        case OrderType::CANCEL: return "CANCEL";
    }
    return "UNKNOWN";
}
inline const char* to_string(OrderStatus s) {
    switch (s) {
        case OrderStatus::OPEN:      return "OPEN";
        case OrderStatus::PARTIAL:   return "PARTIAL";
        case OrderStatus::FILLED:    return "FILLED";
        case OrderStatus::CANCELLED: return "CANCELLED";
    }
    return "UNKNOWN";
}

inline Side side_from_string(const std::string& s) {
    if (s == "BUY") return Side::BUY;
    if (s == "SELL") return Side::SELL;
    throw std::invalid_argument("Unknown side: " + s);
}

inline OrderType type_from_string(const std::string& s) {
    if (s == "LIMIT")  return OrderType::LIMIT;
    if (s == "MARKET") return OrderType::MARKET;
    if (s == "CANCEL") return OrderType::CANCEL;
    throw std::invalid_argument("Unknown order type: " + s);
}

} // namespace engine
