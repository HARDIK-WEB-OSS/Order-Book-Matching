#include "order_book.hpp"
#include <stdexcept>

namespace engine {

// ─────────────────────────────────────────────────────────────────────────────
// add
// ─────────────────────────────────────────────────────────────────────────────
void OrderBook::add(std::shared_ptr<Order> order) {
    if (order->type != OrderType::LIMIT) {
        throw std::invalid_argument("Only LIMIT orders can rest in the book.");
    }
    if (!order->price.has_value()) {
        throw std::invalid_argument("LIMIT order must have a price.");
    }

    IndexEntry entry;
    if (order->side == Side::BUY) {
        auto key = make_bid_key(*order);
        _bids.emplace(key, order);
        entry = { true, key, {} };
    } else {
        auto key = make_ask_key(*order);
        _asks.emplace(key, order);
        entry = { false, {}, key };
    }
    _index.emplace(order->id, entry);
}

// ─────────────────────────────────────────────────────────────────────────────
// remove (by id, does NOT change order status)
// ─────────────────────────────────────────────────────────────────────────────
void OrderBook::remove(const std::string& order_id) {
    auto it = _index.find(order_id);
    if (it == _index.end()) return;

    const auto& entry = it->second;
    if (entry.is_bid) {
        _bids.erase(entry.bid_key);
    } else {
        _asks.erase(entry.ask_key);
    }
    _index.erase(it);
}

// ─────────────────────────────────────────────────────────────────────────────
// cancel (by id, marks order CANCELLED)
// ─────────────────────────────────────────────────────────────────────────────
bool OrderBook::cancel(const std::string& order_id) {
    auto it = _index.find(order_id);
    if (it == _index.end()) return false;

    const auto& entry = it->second;
    if (entry.is_bid) {
        auto map_it = _bids.find(entry.bid_key);
        if (map_it != _bids.end()) {
            map_it->second->status = OrderStatus::CANCELLED;
            _bids.erase(map_it);
        }
    } else {
        auto map_it = _asks.find(entry.ask_key);
        if (map_it != _asks.end()) {
            map_it->second->status = OrderStatus::CANCELLED;
            _asks.erase(map_it);
        }
    }
    _index.erase(it);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// best_bid_order / best_ask_order
// ─────────────────────────────────────────────────────────────────────────────
Order* OrderBook::best_bid_order() const {
    if (_bids.empty()) return nullptr;
    return _bids.begin()->second.get();
}

Order* OrderBook::best_ask_order() const {
    if (_asks.empty()) return nullptr;
    return _asks.begin()->second.get();
}

// ─────────────────────────────────────────────────────────────────────────────
// best_bid / best_ask / spread
// ─────────────────────────────────────────────────────────────────────────────
std::optional<double> OrderBook::best_bid() const {
    if (_bids.empty()) return std::nullopt;
    return -std::get<0>(_bids.begin()->first);
}

std::optional<double> OrderBook::best_ask() const {
    if (_asks.empty()) return std::nullopt;
    return std::get<0>(_asks.begin()->first);
}

std::optional<double> OrderBook::spread() const {
    auto bid = best_bid();
    auto ask = best_ask();
    if (!bid || !ask) return std::nullopt;
    return *ask - *bid;
}

// ─────────────────────────────────────────────────────────────────────────────
// depth_snapshot
// ─────────────────────────────────────────────────────────────────────────────
DepthSnapshot OrderBook::depth_snapshot(int levels) const {
    DepthSnapshot snap;

    // Bids — iterate ascending by key (-price), so first = best (highest real price)
    {
        std::optional<double> cur_price;
        double total_qty = 0.0;
        int    count     = 0;
        for (auto& [key, order] : _bids) {
            double price = -std::get<0>(key);
            if (!cur_price.has_value() || price != *cur_price) {
                if (cur_price.has_value()) {
                    snap.bids.push_back({ *cur_price, total_qty, count });
                    if ((int)snap.bids.size() >= levels) break;
                }
                cur_price = price;
                total_qty = order->remaining();
                count     = 1;
            } else {
                total_qty += order->remaining();
                ++count;
            }
        }
        if (cur_price.has_value() && (int)snap.bids.size() < levels) {
            snap.bids.push_back({ *cur_price, total_qty, count });
        }
    }

    // Asks — iterate ascending by key (+price)
    {
        std::optional<double> cur_price;
        double total_qty = 0.0;
        int    count     = 0;
        for (auto& [key, order] : _asks) {
            double price = std::get<0>(key);
            if (!cur_price.has_value() || price != *cur_price) {
                if (cur_price.has_value()) {
                    snap.asks.push_back({ *cur_price, total_qty, count });
                    if ((int)snap.asks.size() >= levels) break;
                }
                cur_price = price;
                total_qty = order->remaining();
                count     = 1;
            } else {
                total_qty += order->remaining();
                ++count;
            }
        }
        if (cur_price.has_value() && (int)snap.asks.size() < levels) {
            snap.asks.push_back({ *cur_price, total_qty, count });
        }
    }

    return snap;
}

} // namespace engine
