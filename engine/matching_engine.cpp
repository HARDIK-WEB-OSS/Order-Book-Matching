#include "matching_engine.hpp"
#include <chrono>
#include <stdexcept>

namespace engine {

// ─────────────────────────────────────────────────────────────────────────────
MatchingEngine::MatchingEngine()
    : _start_time_unix(now_unix()) {}

// ─────────────────────────────────────────────────────────────────────────────
// process_order
// ─────────────────────────────────────────────────────────────────────────────
std::vector<Trade> MatchingEngine::process_order(std::shared_ptr<Order> order) {
    std::lock_guard<std::mutex> lk(_mtx);
    _total_orders.fetch_add(1, std::memory_order_relaxed);
    _orders[order->id] = order;

    switch (order->type) {
        case OrderType::LIMIT:
            return _match_limit(order);
        case OrderType::MARKET:
            return _match_market(order);
        case OrderType::CANCEL:
            _book.cancel(order->id);
            return {};
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// cancel_by_id
// ─────────────────────────────────────────────────────────────────────────────
bool MatchingEngine::cancel_by_id(const std::string& order_id) {
    std::lock_guard<std::mutex> lk(_mtx);
    return _book.cancel(order_id);
}

// ─────────────────────────────────────────────────────────────────────────────
// _match_limit
// ─────────────────────────────────────────────────────────────────────────────
std::vector<Trade> MatchingEngine::_match_limit(std::shared_ptr<Order> order) {
    std::vector<Trade> trades;

    while (order->remaining() > 1e-9) {
        Order* resting = nullptr;
        if (order->side == Side::BUY) {
            resting = _book.best_ask_order();
            if (!resting || resting->price.value() > order->price.value()) break;
        } else {
            resting = _book.best_bid_order();
            if (!resting || resting->price.value() < order->price.value()) break;
        }
        trades.push_back(_execute_match(*order, *resting));
    }

    // Rest remainder in book
    if (order->status == OrderStatus::OPEN || order->status == OrderStatus::PARTIAL) {
        if (order->remaining() > 1e-9) {
            _book.add(order);
        }
    }
    return trades;
}

// ─────────────────────────────────────────────────────────────────────────────
// _match_market
// ─────────────────────────────────────────────────────────────────────────────
std::vector<Trade> MatchingEngine::_match_market(std::shared_ptr<Order> order) {
    std::vector<Trade> trades;

    while (order->remaining() > 1e-9) {
        Order* resting = (order->side == Side::BUY)
            ? _book.best_ask_order()
            : _book.best_bid_order();
        if (!resting) break;
        trades.push_back(_execute_match(*order, *resting));
    }

    if (order->remaining() > 1e-9) {
        order->status = OrderStatus::CANCELLED;
    }
    return trades;
}

// ─────────────────────────────────────────────────────────────────────────────
// _execute_match
// ─────────────────────────────────────────────────────────────────────────────
Trade MatchingEngine::_execute_match(Order& aggressor, Order& resting) {
    double fill_qty   = std::min(aggressor.remaining(), resting.remaining());
    double fill_price = resting.price.value();

    aggressor.fill(fill_qty);
    resting.fill(fill_qty);

    if (resting.status == OrderStatus::FILLED) {
        _book.remove(resting.id);
    }

    std::string buy_id  = (aggressor.side == Side::BUY) ? aggressor.id : resting.id;
    std::string sell_id = (aggressor.side == Side::SELL) ? aggressor.id : resting.id;

    Trade t(std::move(buy_id), std::move(sell_id), fill_price, fill_qty);
    _record_trade(t);
    return t;
}

// ─────────────────────────────────────────────────────────────────────────────
// _record_trade
// ─────────────────────────────────────────────────────────────────────────────
void MatchingEngine::_record_trade(const Trade& t) {
    _trade_log.push_front(t);
    if (_trade_log.size() > TRADE_LOG_MAX) {
        _trade_log.pop_back();
    }
    _total_trades.fetch_add(1, std::memory_order_relaxed);

    // Atomic double add via compare-exchange
    double old_vol = _total_volume.load(std::memory_order_relaxed);
    double new_vol;
    do {
        new_vol = old_vol + t.price * t.quantity;
    } while (!_total_volume.compare_exchange_weak(
        old_vol, new_vol,
        std::memory_order_relaxed,
        std::memory_order_relaxed));
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────
std::optional<Trade> MatchingEngine::last_trade() const {
    std::lock_guard<std::mutex> lk(_mtx);
    if (_trade_log.empty()) return std::nullopt;
    return _trade_log.front();
}

std::vector<Trade> MatchingEngine::recent_trades(int n) const {
    std::lock_guard<std::mutex> lk(_mtx);
    int count = std::min(n, (int)_trade_log.size());
    return std::vector<Trade>(_trade_log.begin(), _trade_log.begin() + count);
}

double MatchingEngine::uptime_seconds() const {
    return now_unix() - _start_time_unix;
}

} // namespace engine
