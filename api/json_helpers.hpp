#pragma once

// Minimal JSON builder — no external dependency needed.
// We hand-roll JSON strings; for a production system nlohmann/json
// would be included, but this keeps the project dependency-free.

#include "../engine/order.hpp"
#include "../engine/order_book.hpp"
#include "../engine/matching_engine.hpp"

#include <sstream>
#include <string>
#include <optional>
#include <vector>

namespace api {

// ─────────────────────────────────────────────────────────────────────────────
// Primitive helpers
// ─────────────────────────────────────────────────────────────────────────────
inline std::string jstr(const std::string& s) {
    return "\"" + s + "\"";
}
inline std::string jdbl(double v) {
    std::ostringstream ss;
    ss << std::fixed;
    ss.precision(8);
    ss << v;
    return ss.str();
}
inline std::string jopt(const std::optional<double>& v) {
    return v ? jdbl(*v) : "null";
}
inline std::string ju64(uint64_t v) {
    return std::to_string(v);
}
inline std::string ji64(int64_t v) {
    return std::to_string(v);
}

// ─────────────────────────────────────────────────────────────────────────────
// Order response
// ─────────────────────────────────────────────────────────────────────────────
inline std::string order_to_json(const engine::Order& o) {
    std::ostringstream s;
    s << "{"
      << "\"order_id\":"  << jstr(o.id)                      << ","
      << "\"status\":"    << jstr(engine::to_string(o.status))
      << "}";
    return s.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Trade
// ─────────────────────────────────────────────────────────────────────────────
inline std::string trade_to_json(const engine::Trade& t) {
    std::ostringstream s;
    s << "{"
      << "\"trade_id\":"      << jstr(t.trade_id)      << ","
      << "\"buy_order_id\":"  << jstr(t.buy_order_id)  << ","
      << "\"sell_order_id\":" << jstr(t.sell_order_id) << ","
      << "\"price\":"         << jdbl(t.price)         << ","
      << "\"quantity\":"      << jdbl(t.quantity)      << ","
      << "\"timestamp\":"     << ji64(t.timestamp)
      << "}";
    return s.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// PriceLevel
// ─────────────────────────────────────────────────────────────────────────────
inline std::string level_to_json(const engine::PriceLevel& l) {
    std::ostringstream s;
    s << "{"
      << "\"price\":"          << jdbl(l.price)          << ","
      << "\"total_quantity\":" << jdbl(l.total_quantity) << ","
      << "\"order_count\":"    << l.order_count
      << "}";
    return s.str();
}

inline std::string levels_to_json(const std::vector<engine::PriceLevel>& levels) {
    std::string out = "[";
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (i) out += ",";
        out += level_to_json(levels[i]);
    }
    out += "]";
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// /book endpoint
// ─────────────────────────────────────────────────────────────────────────────
inline std::string book_to_json(engine::MatchingEngine& eng) {
    auto& book  = eng.book();
    auto  depth = book.depth_snapshot(5);
    std::ostringstream s;
    s << "{"
      << "\"best_bid\":"  << jopt(book.best_bid()) << ","
      << "\"best_ask\":"  << jopt(book.best_ask()) << ","
      << "\"spread\":"    << jopt(book.spread())   << ","
      << "\"bids\":"      << levels_to_json(depth.bids) << ","
      << "\"asks\":"      << levels_to_json(depth.asks)
      << "}";
    return s.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// /trades endpoint
// ─────────────────────────────────────────────────────────────────────────────
inline std::string trades_to_json(const std::vector<engine::Trade>& trades) {
    std::string out = "[";
    for (std::size_t i = 0; i < trades.size(); ++i) {
        if (i) out += ",";
        out += trade_to_json(trades[i]);
    }
    out += "]";
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// /stats endpoint
// ─────────────────────────────────────────────────────────────────────────────
inline std::string stats_to_json(engine::MatchingEngine& eng) {
    std::ostringstream s;
    s << "{"
      << "\"total_orders_processed\":"  << ju64(eng.total_orders_processed()) << ","
      << "\"total_trades_executed\":"   << ju64(eng.total_trades_executed())  << ","
      << "\"total_volume_traded\":"     << jdbl(eng.total_volume_traded())    << ","
      << "\"uptime_seconds\":"          << jdbl(eng.uptime_seconds())
      << "}";
    return s.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// WebSocket snapshot payload
// ─────────────────────────────────────────────────────────────────────────────
inline std::string ws_snapshot_json(engine::MatchingEngine& eng) {
    auto& book  = eng.book();
    auto  depth = book.depth_snapshot(5);
    auto  bid   = book.best_bid();
    auto  ask   = book.best_ask();

    std::string spread_pct = "null";
    if (bid && ask && *bid > 0.0) {
        std::ostringstream tmp;
        tmp << std::fixed;
        tmp.precision(6);
        tmp << ((*ask - *bid) / *bid * 100.0);
        spread_pct = tmp.str();
    }

    std::string last_trade_json = "null";
    auto last = eng.last_trade();
    if (last) {
        std::ostringstream lt;
        lt << "{"
           << "\"price\":"     << jdbl(last->price)     << ","
           << "\"qty\":"       << jdbl(last->quantity)  << ","
           << "\"timestamp\":" << ji64(last->timestamp)
           << "}";
        last_trade_json = lt.str();
    }

    std::ostringstream s;
    s << "{"
      << "\"timestamp\":"   << jdbl(engine::now_unix()) << ","
      << "\"best_bid\":"    << jopt(bid)                 << ","
      << "\"best_ask\":"    << jopt(ask)                 << ","
      << "\"spread_pct\":"  << spread_pct                << ","
      << "\"bid_depth\":"   << levels_to_json(depth.bids) << ","
      << "\"ask_depth\":"   << levels_to_json(depth.asks) << ","
      << "\"last_trade\":"  << last_trade_json
      << "}";
    return s.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Simple JSON value parser (enough for our POST /order body)
// ─────────────────────────────────────────────────────────────────────────────
inline std::string parse_string_field(const std::string& json, const std::string& key) {
    // Finds "key":"value" and returns value
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos);
    if (pos == std::string::npos) return "";
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

inline std::optional<double> parse_double_field(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size()) return std::nullopt;
    // null check
    if (json.substr(pos, 4) == "null") return std::nullopt;
    try {
        std::size_t consumed = 0;
        double val = std::stod(json.substr(pos), &consumed);
        return val;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace api
