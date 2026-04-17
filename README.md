# Order Book Matching Engine (C++17)

## What is an Order Book?

An **order book** is the core data structure of every electronic exchange — from Nasdaq to Binance to CME. It maintains two sorted lists of resting orders: the **bid side** (buyers, sorted descending by price) and the **ask side** (sellers, sorted ascending by price). When a new order arrives the engine looks for a counterparty on the opposite side that satisfies the price constraint. If one exists, a trade is executed at the resting order's price; if not, the new order joins the book and waits. The highest bid and lowest ask are called the *best bid* and *best ask*; the difference between them is the **spread**, a real-time proxy for market liquidity and transaction cost.

Matching engines enforce **price-time priority** (FIFO priority): among all resting orders the one with the best price wins; among orders at the *same* price the one submitted earliest wins. This discipline rewards early liquidity providers, tightens spreads, and is the foundation of fair market microstructure. Beyond matching, a production engine must handle partial fills (a large order split across many counterparties), market orders (execute immediately at whatever price is available), and cancellations — all at sub-microsecond latency under concurrent load.

---

## Quick Start

### Prerequisites
- C++17 compiler (GCC ≥ 9, Clang ≥ 9, or MSVC 2019+)
- CMake ≥ 3.16
- curl (for fetching single-header deps)
- Python 3.8+ with `pip` (dashboard only)

### One-command build + run
```bash
bash run.sh          # fetches deps, builds Release, starts server on :8000
```

### Manual steps
```bash
# 1. Fetch vendored single-header libraries (httplib + Catch2)
bash fetch_deps.sh

# 2. Configure and build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
cd ..

# 3. Start the server
./build/server              # listens on 0.0.0.0:8000
./build/server 9000         # custom port

# 4. Run benchmark (100 000 orders)
./build/benchmark/stress_test

# 5. Run unit tests
./build/test_matching
# OR via ctest:
cd build && ctest --output-on-failure

# 6. Live terminal dashboard (server must be running)
pip install rich requests
python dashboard/live.py
```

---

## REST API

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/order` | Submit a new order |
| `DELETE` | `/order/:id` | Cancel a resting order |
| `GET` | `/book` | Current best bid/ask + top-5 depth |
| `GET` | `/trades` | Last 50 executed trades |
| `GET` | `/stats` | Engine statistics |
| `GET` | `/ws/feed` | 200 ms SSE push stream |

### Submit a limit buy
```bash
curl -X POST http://localhost:8000/order \
  -H "Content-Type: application/json" \
  -d '{"side":"BUY","type":"LIMIT","price":999.5,"quantity":10}'
```

### Submit a market sell
```bash
curl -X POST http://localhost:8000/order \
  -H "Content-Type: application/json" \
  -d '{"side":"SELL","type":"MARKET","quantity":5}'
```

### Cancel an order
```bash
curl -X DELETE http://localhost:8000/order/<order_id>
```

### Watch the live feed
```bash
curl -N http://localhost:8000/ws/feed
```

---

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                    HTTP Server  (api/server.cpp)                  │
│                    cpp-httplib — single header, MIT               │
│                                                                   │
│  POST /order   DELETE /order/:id   GET /book   GET /trades        │
│       │                │                                          │
└───────┼────────────────┼──────────────────────────────────────────┘
        │                │
        ▼                ▼
┌──────────────────────────────────────────────────────────────────┐
│              MatchingEngine  (engine/matching_engine.cpp)         │
│              std::mutex — thread-safe                             │
│                                                                   │
│  process_order(shared_ptr<Order>)                                 │
│   ├── LIMIT  → _match_limit  → rest remainder in OrderBook        │
│   ├── MARKET → _match_market → cancel unfilled remainder          │
│   └── CANCEL → book.cancel(id)                                    │
│                                                                   │
│  trade_log: std::deque<Trade>  (newest first, cap=10 000)         │
└──────────────────────────┬───────────────────────────────────────┘
                           │
                           ▼
┌──────────────────────────────────────────────────────────────────┐
│                  OrderBook  (engine/order_book.cpp)               │
│                                                                   │
│  _bids: std::map<(-price, ts, id), shared_ptr<Order>>            │
│          begin() == best bid  (highest price, earliest time)      │
│                                                                   │
│  _asks: std::map<(+price, ts, id), shared_ptr<Order>>            │
│          begin() == best ask  (lowest price, earliest time)       │
│                                                                   │
│  _index: unordered_map<order_id, IndexEntry>  O(1) cancel lookup  │
└──────────────────────────────────────────────────────────────────┘
                           │
                 200 ms background thread
                           │
                           ▼
┌──────────────────────────────────────────────────────────────────┐
│             SSE Broadcaster  (api/server.cpp WsState)             │
│             GET /ws/feed → chunked text/event-stream              │
└──────────────────────────────────────────────────────────────────┘
                           │
                           ▼
              dashboard/live.py  (Python / rich)
```

---

## Design Decisions

### Why `std::map` instead of a heap?

A `std::priority_queue` (heap) provides O(log n) push/pop but only O(n) arbitrary removal — fatal for a cancel-heavy workload. `std::map` gives O(log n) insert, O(log n) deletion by key, and O(1) min access via `begin()`. The composite key `(-price, timestamp, order_id)` for bids and `(+price, timestamp, order_id)` for asks encodes price-time priority directly into the natural sort order, so `begin()` is always the best order with no additional work. The `_index` hash-map provides O(1) average-case lookup of any order by ID for cancellation.

### Why FIFO within a price level?

Price-time priority is the exchange industry standard. It encourages market makers to post orders early (improving spread) and is strategy-proof: there is no benefit to withdrawing and resubmitting an order at the same price. The timestamp is embedded in the sort key, so no secondary comparator is needed.

### How partial fills work

Every `Order` tracks `filled_quantity` separately from `quantity`. After each match the engine calls `order.fill(qty)`, which increments `filled_quantity` and transitions `status` to `PARTIAL` or `FILLED`. A resting `PARTIAL` order remains in the book under its original key; only `FILLED` orders are removed via `book.remove()`. The aggressor loops, calling `_execute_match()` against successive resting orders until its `remaining()` drops to zero or no eligible counterparty exists.

### Thread safety

A single `std::mutex` in `MatchingEngine` guards all book mutations and trade-log access. Stats counters use `std::atomic` so the HTTP `/stats` handler can read them without taking the lock. The broadcaster thread reads the engine outside the lock (best-effort snapshot) which is acceptable for a dashboard feed.

### Memory model

`Order` objects are owned by `shared_ptr` stored in the book maps. Raw pointers returned by `best_bid_order()` / `best_ask_order()` are valid only while the engine lock is held — the matching loop always holds it.

---

## File Structure

```
order-book-engine-cpp/
├── engine/
│   ├── order.hpp               # Order, Trade, enums, UUID, timestamps
│   ├── order_book.hpp/.cpp     # std::map bid/ask book
│   └── matching_engine.hpp/.cpp# Price-time priority matching
├── api/
│   ├── json_helpers.hpp        # Hand-rolled JSON serialisation
│   └── server.cpp              # cpp-httplib REST + SSE server
├── benchmark/
│   └── stress_test.cpp         # 100 k order benchmark
├── dashboard/
│   └── live.py                 # Python/rich terminal dashboard
├── tests/
│   └── test_matching.cpp       # Catch2 unit tests (10 test cases)
├── third_party/                # Populated by fetch_deps.sh
│   ├── httplib.h               # cpp-httplib (single header)
│   └── catch2/catch.hpp        # Catch2 v2 (single header)
├── CMakeLists.txt
├── fetch_deps.sh
├── run.sh
└── README.md
```

---

## Benchmark Results

Run `./build/benchmark/stress_test` and paste output here.

```
(placeholder — build and run the benchmark, then paste stdout)
```
