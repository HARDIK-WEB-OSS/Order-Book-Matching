#pragma once

#include "order.hpp"

#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace engine {

// ─────────────────────────────────────────────────────────────────────────────
// Price level snapshot (for API / feed)
// ─────────────────────────────────────────────────────────────────────────────
struct PriceLevel {
    double price;
    double total_quantity;
    int    order_count;
};

struct DepthSnapshot {
    std::vector<PriceLevel> bids;   // best first (descending price)
    std::vector<PriceLevel> asks;   // best first (ascending price)
};

// ─────────────────────────────────────────────────────────────────────────────
// OrderBook
//
// Bid side  : std::map<BidKey, Order*>  key = (-price, timestamp, id)
//             → begin() == best bid (highest price, earliest time)
// Ask side  : std::map<AskKey, Order*>  key = (+price, timestamp, id)
//             → begin() == best ask (lowest price, earliest time)
//
// All Order objects are owned by the shared_ptr stored in _all_orders.
// The maps hold raw pointers for speed; ownership never escapes.
// ─────────────────────────────────────────────────────────────────────────────
class OrderBook {
public:
    // Composite sort keys
    using BidKey = std::tuple<double,   int64_t, std::string>; // (-price, ts, id)
    using AskKey = std::tuple<double,   int64_t, std::string>; // (+price, ts, id)

    // Reverse comparator so that the *most negative* price sorts first
    // → highest bid price first
    using BidMap = std::map<BidKey, std::shared_ptr<Order>>;
    using AskMap = std::map<AskKey, std::shared_ptr<Order>>;

    // ── Mutating operations ───────────────────────────────────────────────
    void add(std::shared_ptr<Order> order);
    void remove(const std::string& order_id);
    bool cancel(const std::string& order_id);

    // ── Best-price access (non-owning) ────────────────────────────────────
    Order* best_bid_order() const;
    Order* best_ask_order() const;

    std::optional<double> best_bid() const;
    std::optional<double> best_ask() const;
    std::optional<double> spread()   const;

    // ── Depth snapshot ────────────────────────────────────────────────────
    DepthSnapshot depth_snapshot(int levels = 5) const;

    // ── Size ──────────────────────────────────────────────────────────────
    std::size_t bid_count() const { return _bids.size(); }
    std::size_t ask_count() const { return _asks.size(); }

private:
    BidMap _bids;
    AskMap _asks;

    // order_id → pointer to owning map entry
    struct IndexEntry {
        bool is_bid;
        BidKey bid_key;
        AskKey ask_key;
    };
    std::unordered_map<std::string, IndexEntry> _index;

    static BidKey make_bid_key(const Order& o) {
        return { -o.price.value(), o.timestamp, o.id };
    }
    static AskKey make_ask_key(const Order& o) {
        return { o.price.value(), o.timestamp, o.id };
    }
};

} // namespace engine
