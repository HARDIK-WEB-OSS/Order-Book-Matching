/*
 * Order Book Matching Engine — HTTP + WebSocket API Server
 */

#include "../engine/order.hpp"
#include "../engine/order_book.hpp"
#include "../engine/matching_engine.hpp"
#include "json_helpers.hpp"
#include "../third_party/httplib.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <atomic>
#include <set>
#include <string>
#include <thread>

static engine::MatchingEngine g_engine;

static void add_cors(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin",  "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

static void json_response(httplib::Response& res, int status, const std::string& body) {
    add_cors(res);
    res.status = status;
    res.set_content(body, "application/json");
}

struct WsState {
    std::mutex  mtx;
    std::set<httplib::DataSink*> sinks;

    void add(httplib::DataSink* s) {
        std::lock_guard<std::mutex> lk(mtx);
        sinks.insert(s);
    }
    void remove(httplib::DataSink* s) {
        std::lock_guard<std::mutex> lk(mtx);
        sinks.erase(s);
    }
    void broadcast(const std::string& msg) {
        std::lock_guard<std::mutex> lk(mtx);
        std::set<httplib::DataSink*> dead;
        for (auto* sink : sinks) {
            if (!sink->write(msg.data(), msg.size())) {
                dead.insert(sink);
            }
        }
        for (auto* s : dead) sinks.erase(s);
    }
};
static WsState g_ws;

static void broadcaster_thread() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::string snap = api::ws_snapshot_json(g_engine) + "\n";
        g_ws.broadcast(snap);
    }
}

int main(int argc, char** argv) {
    int port = 8000;
    if (argc > 1) port = std::atoi(argv[1]);

    httplib::Server svr;

    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        add_cors(res);
        res.status = 204;
    });

    svr.Post("/order", [](const httplib::Request& req, httplib::Response& res) {
        auto& body = req.body;
        std::string side_str  = api::parse_string_field(body, "side");
        std::string type_str  = api::parse_string_field(body, "type");
        auto        price_opt = api::parse_double_field(body, "price");
        auto        qty_opt   = api::parse_double_field(body, "quantity");

        if (side_str.empty() || type_str.empty() || !qty_opt) {
            json_response(res, 400,
                R"({"error":"Missing required fields: side, type, quantity"})");
            return;
        }

        engine::Side      side;
        engine::OrderType type;
        try {
            side = engine::side_from_string(side_str);
            type = engine::type_from_string(type_str);
        } catch (const std::exception& e) {
            json_response(res, 400,
                std::string(R"({"error":")") + e.what() + "\"}");
            return;
        }

        if (type == engine::OrderType::CANCEL) {
            json_response(res, 400,
                R"({"error":"Use DELETE /order/:id to cancel"})");
            return;
        }
        if (type == engine::OrderType::LIMIT && !price_opt) {
            json_response(res, 400,
                R"({"error":"LIMIT orders require a price"})");
            return;
        }

        auto order = std::make_shared<engine::Order>(
            side, type, *qty_opt, price_opt
        );
        g_engine.process_order(order);
        json_response(res, 200, api::order_to_json(*order));
    });

    svr.Delete(R"(/order/([^/]+))", [](const httplib::Request& req,
                                        httplib::Response& res) {
        std::string order_id = req.matches[1];
        bool ok = g_engine.cancel_by_id(order_id);
        if (!ok) {
            json_response(res, 404,
                R"({"error":"Order not found or already filled"})");
            return;
        }
        json_response(res, 200,
            "{\"cancelled\":true,\"order_id\":" + api::jstr(order_id) + "}");
    });

    svr.Get("/book", [](const httplib::Request&, httplib::Response& res) {
        json_response(res, 200, api::book_to_json(g_engine));
    });

    svr.Get("/trades", [](const httplib::Request&, httplib::Response& res) {
        auto trades = g_engine.recent_trades(50);
        json_response(res, 200, api::trades_to_json(trades));
    });

    svr.Get("/stats", [](const httplib::Request&, httplib::Response& res) {
        json_response(res, 200, api::stats_to_json(g_engine));
    });

    svr.Get("/ws/feed", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_chunked_content_provider(
            "text/event-stream",
            [](size_t /*offset*/, httplib::DataSink& sink) {
                g_ws.add(&sink);
                while (true) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    std::lock_guard<std::mutex> lk(g_ws.mtx);
                    if (g_ws.sinks.find(&sink) == g_ws.sinks.end()) {
                        break;
                    }
                }
                return true;
            },
            [](bool /*success*/) {}
        );
    });

    std::thread bt(broadcaster_thread);
    bt.detach();

    std::cout << "┌─────────────────────────────────────────────┐\n"
              << "│  Order Book Matching Engine  (C++17)         │\n"
              << "│  Listening on http://0.0.0.0:" << port << "           │\n"
              << "│  POST /order  DELETE /order/:id              │\n"
              << "│  GET  /book   /trades  /stats  /ws/feed      │\n"
              << "└─────────────────────────────────────────────┘\n";

    svr.listen("0.0.0.0", port);
    return 0;
}
