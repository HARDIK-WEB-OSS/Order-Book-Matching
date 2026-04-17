/*
 * test_matching.cpp — unit tests using Catch2 (single-header, vendored)
 *
 * Build: see CMakeLists.txt target `tests`
 * Run:   ./build/tests/test_matching
 */

#define CATCH_CONFIG_MAIN
#include "../third_party/catch2/catch.hpp"

#include "../engine/order.hpp"
#include "../engine/order_book.hpp"
#include "../engine/matching_engine.hpp"

using namespace engine;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static std::shared_ptr<Order> limit_order(Side side, double price, double qty) {
    return std::make_shared<Order>(side, OrderType::LIMIT, qty,
                                   std::optional<double>{price});
}
static std::shared_ptr<Order> market_order(Side side, double qty) {
    return std::make_shared<Order>(side, OrderType::MARKET, qty, std::nullopt);
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. Basic limit order insertion
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("1. Basic limit order insertion — bid and ask appear in book") {
    SECTION("Bid appears at correct price") {
        MatchingEngine eng;
        auto o = limit_order(Side::BUY, 100.0, 10.0);
        eng.process_order(o);
        REQUIRE(eng.book().best_bid().has_value());
        REQUIRE(eng.book().best_bid().value() == Approx(100.0));
    }

    SECTION("Ask appears at correct price") {
        MatchingEngine eng;
        auto o = limit_order(Side::SELL, 105.0, 10.0);
        eng.process_order(o);
        REQUIRE(eng.book().best_ask().has_value());
        REQUIRE(eng.book().best_ask().value() == Approx(105.0));
    }

    SECTION("No-cross — no trades generated") {
        MatchingEngine eng;
        eng.process_order(limit_order(Side::BUY,  99.0, 5.0));
        eng.process_order(limit_order(Side::SELL, 101.0, 5.0));
        REQUIRE(eng.total_trades_executed() == 0);
        REQUIRE(eng.book().best_bid().value() == Approx(99.0));
        REQUIRE(eng.book().best_ask().value() == Approx(101.0));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Exact match
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("2. Exact match — bid and ask at same price, both fully filled") {
    MatchingEngine eng;
    auto sell = limit_order(Side::SELL, 100.0, 10.0);
    auto buy  = limit_order(Side::BUY,  100.0, 10.0);
    eng.process_order(sell);
    auto trades = eng.process_order(buy);

    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].price    == Approx(100.0));
    REQUIRE(trades[0].quantity == Approx(10.0));
    REQUIRE(buy->status  == OrderStatus::FILLED);
    REQUIRE(sell->status == OrderStatus::FILLED);
    REQUIRE_FALSE(eng.book().best_bid().has_value());
    REQUIRE_FALSE(eng.book().best_ask().has_value());

    SECTION("Trade records correct buy/sell ids") {
        REQUIRE(trades[0].buy_order_id  == buy->id);
        REQUIRE(trades[0].sell_order_id == sell->id);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. Partial fill
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("3. Partial fill — large resting order partially consumed") {
    SECTION("Resting order partially filled, remains in book") {
        MatchingEngine eng;
        auto big_sell = limit_order(Side::SELL, 100.0, 20.0);
        eng.process_order(big_sell);
        auto small_buy = limit_order(Side::BUY, 100.0, 5.0);
        eng.process_order(small_buy);

        REQUIRE(small_buy->status == OrderStatus::FILLED);
        REQUIRE(big_sell->status  == OrderStatus::PARTIAL);
        REQUIRE(big_sell->remaining() == Approx(15.0));
        REQUIRE(eng.book().best_ask().value() == Approx(100.0));
    }

    SECTION("Aggressor partially fills, remainder rests in book") {
        MatchingEngine eng;
        eng.process_order(limit_order(Side::SELL, 100.0, 3.0));
        auto big_buy = limit_order(Side::BUY, 100.0, 10.0);
        eng.process_order(big_buy);

        REQUIRE(big_buy->status   == OrderStatus::PARTIAL);
        REQUIRE(big_buy->remaining() == Approx(7.0));
        REQUIRE(eng.book().best_bid().value() == Approx(100.0));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. FIFO ordering
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("4. FIFO ordering — earlier order at same price fills first") {
    MatchingEngine eng;

    auto first_sell  = limit_order(Side::SELL, 100.0, 5.0);
    auto second_sell = limit_order(Side::SELL, 100.0, 5.0);

    // Ensure timestamps are ordered
    first_sell->timestamp  = 1000;
    second_sell->timestamp = 2000;

    eng.process_order(first_sell);
    eng.process_order(second_sell);

    auto buy = limit_order(Side::BUY, 100.0, 5.0);
    auto trades = eng.process_order(buy);

    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].sell_order_id == first_sell->id);
    REQUIRE(first_sell->status  == OrderStatus::FILLED);
    REQUIRE(second_sell->status == OrderStatus::OPEN);

    SECTION("Second order fills after first exhausted") {
        MatchingEngine eng2;
        auto s1 = limit_order(Side::SELL, 100.0, 3.0);
        auto s2 = limit_order(Side::SELL, 100.0, 7.0);
        s1->timestamp = 100; s2->timestamp = 200;
        eng2.process_order(s1);
        eng2.process_order(s2);

        auto b = limit_order(Side::BUY, 100.0, 10.0);
        auto t2 = eng2.process_order(b);

        REQUIRE(t2.size() == 2);
        REQUIRE(t2[0].sell_order_id == s1->id);
        REQUIRE(t2[1].sell_order_id == s2->id);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. Market order — consumes multiple price levels
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("5. Market order consumes multiple price levels") {
    MatchingEngine eng;
    eng.process_order(limit_order(Side::SELL, 100.0, 5.0));
    eng.process_order(limit_order(Side::SELL, 101.0, 5.0));
    eng.process_order(limit_order(Side::SELL, 102.0, 5.0));

    auto buy_mkt = market_order(Side::BUY, 12.0);
    auto trades  = eng.process_order(buy_mkt);

    REQUIRE(buy_mkt->status == OrderStatus::FILLED);
    REQUIRE(buy_mkt->filled_quantity == Approx(12.0));
    REQUIRE(trades.size() == 3);

    // Verify price levels consumed in order
    REQUIRE(trades[0].price == Approx(100.0));
    REQUIRE(trades[1].price == Approx(101.0));
    REQUIRE(trades[2].price == Approx(102.0));
    REQUIRE(trades[2].quantity == Approx(2.0));
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. Market order with insufficient liquidity
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("6. Market order insufficient liquidity — partial fill, remainder cancelled") {
    MatchingEngine eng;
    eng.process_order(limit_order(Side::SELL, 100.0, 3.0));

    auto buy_mkt = market_order(Side::BUY, 10.0);
    auto trades  = eng.process_order(buy_mkt);

    REQUIRE(buy_mkt->filled_quantity == Approx(3.0));
    REQUIRE(buy_mkt->status == OrderStatus::CANCELLED);
    REQUIRE(trades.size() == 1);
    REQUIRE_FALSE(eng.book().best_ask().has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. Cancel existing order
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("7. Cancel existing order — removed from book") {
    MatchingEngine eng;
    auto o = limit_order(Side::BUY, 99.0, 10.0);
    eng.process_order(o);

    REQUIRE(eng.book().best_bid().has_value());

    bool result = eng.cancel_by_id(o->id);

    REQUIRE(result == true);
    REQUIRE(o->status == OrderStatus::CANCELLED);
    REQUIRE_FALSE(eng.book().best_bid().has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. Cancel non-existent order
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("8. Cancel non-existent order — returns false, no crash") {
    MatchingEngine eng;
    bool result = eng.cancel_by_id("does-not-exist-00000000");
    REQUIRE(result == false);
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. Spread calculation
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("9. Spread calculation — correct after various states") {
    SECTION("Spread between resting bid and ask") {
        MatchingEngine eng;
        eng.process_order(limit_order(Side::BUY,  99.0, 5.0));
        eng.process_order(limit_order(Side::SELL, 101.0, 5.0));
        REQUIRE(eng.book().spread().has_value());
        REQUIRE(eng.book().spread().value() == Approx(2.0));
    }

    SECTION("No spread when one side empty") {
        MatchingEngine eng;
        eng.process_order(limit_order(Side::BUY, 99.0, 5.0));
        REQUIRE_FALSE(eng.book().spread().has_value());
    }

    SECTION("No spread when book fully cleared") {
        MatchingEngine eng;
        eng.process_order(limit_order(Side::SELL, 100.0, 5.0));
        eng.process_order(limit_order(Side::BUY,  100.0, 5.0));
        REQUIRE_FALSE(eng.book().best_bid().has_value());
        REQUIRE_FALSE(eng.book().best_ask().has_value());
        REQUIRE_FALSE(eng.book().spread().has_value());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 10. Trade log integrity
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("10. Trade log — every match generates exactly one Trade with correct fields") {
    SECTION("Single match — correct fields") {
        MatchingEngine eng;
        auto sell = limit_order(Side::SELL, 200.0, 7.0);
        auto buy  = limit_order(Side::BUY,  200.0, 7.0);
        eng.process_order(sell);
        eng.process_order(buy);

        REQUIRE(eng.total_trades_executed() == 1);
        auto trades = eng.recent_trades(1);
        REQUIRE(trades.size() == 1);
        REQUIRE(trades[0].price          == Approx(200.0));
        REQUIRE(trades[0].quantity       == Approx(7.0));
        REQUIRE(trades[0].buy_order_id   == buy->id);
        REQUIRE(trades[0].sell_order_id  == sell->id);
        REQUIRE_FALSE(trades[0].trade_id.empty());
        REQUIRE(trades[0].timestamp > 0);
    }

    SECTION("Multiple levels → multiple trades") {
        MatchingEngine eng;
        for (double p : {100.0, 101.0, 102.0}) {
            eng.process_order(limit_order(Side::SELL, p, 2.0));
        }
        auto mkt = market_order(Side::BUY, 6.0);
        auto trades = eng.process_order(mkt);
        REQUIRE(trades.size() == 3);
        REQUIRE(eng.total_trades_executed() == 3);
    }

    SECTION("No trade without price cross") {
        MatchingEngine eng;
        eng.process_order(limit_order(Side::BUY,  98.0, 5.0));
        eng.process_order(limit_order(Side::SELL, 102.0, 5.0));
        REQUIRE(eng.total_trades_executed() == 0);
    }
}
