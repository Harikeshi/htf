// Stage 1: тесты корректности + микробенчмарк order book v1.
#include "order_book_v1.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace ob;

#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

static void test_correctness() {
    OrderBookV1 b;
    std::vector<Trade> t;

    b.add_limit({1, 100, 10, Side::Sell}, t);
    b.add_limit({2, 100, 5, Side::Sell}, t);
    b.add_limit({3, 101, 7, Side::Sell}, t);
    CHECK(t.empty() && b.best_ask() == 100);

    // price-time priority: сначала заявка 1, потом 2, потом 3
    b.add_limit({4, 101, 20, Side::Buy}, t);
    CHECK(t.size() == 3);
    CHECK(t[0].maker == 1 && t[0].qty == 10 && t[0].price == 100);
    CHECK(t[1].maker == 2 && t[1].qty == 5);
    CHECK(t[2].maker == 3 && t[2].qty == 5 && t[2].price == 101);
    CHECK(b.best_ask() == 101 && b.order_count() == 1); // остаток заявки 3 (2 лота)

    // остаток лимитной заявки 4 нет: qty 20 = 10+5+5, полностью исполнена
    t.clear();
    b.add_limit({5, 99, 4, Side::Buy}, t);
    CHECK(t.empty() && b.best_bid() == 99);

    CHECK(b.cancel(5));
    CHECK(!b.cancel(5));
    CHECK(b.best_bid() == 0);

    // market с остатком: отбрасывается
    t.clear();
    b.add_market(6, Side::Buy, 100, t);
    CHECK(t.size() == 1 && t[0].qty == 2);
    CHECK(b.order_count() == 0 && b.level_count() == 0);
    std::puts("correctness: OK");
}

struct Op {
    enum Kind : std::uint8_t {
        Limit,
        Market,
        Cancel
    } kind;

    Order o;
    OrderId target;
};

static void benchmark() {
    constexpr std::size_t N = 2'000'000;
    std::mt19937_64 rng(42);
    std::vector<Op> ops;
    ops.reserve(N);
    OrderId next_id = 1;
    constexpr Price mid = 100'000;

    for (std::size_t i = 0; i < N; ++i) {
        const auto r = rng() % 100;
        const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
        if (r < 60) {
            // 60% лимитки в окне +-50 тиков от mid
            Price off = static_cast<Price>(rng() % 50);
            Price px = side == Side::Buy ? mid - off : mid + off;
            ops.push_back({Op::Limit, {next_id++, px, static_cast<Qty>(1 + rng() % 100), side}, 0});
        } else if (r < 90) {
            // 30% отмен случайной ранее выставленной
            ops.push_back({Op::Cancel, {}, 1 + rng() % std::max<OrderId>(1, next_id - 1)});
        } else {
            // 10% маркет
            ops.push_back({Op::Market, {next_id++, 0, static_cast<Qty>(1 + rng() % 200), side}, 0});
        }
    }

    OrderBookV1 book;
    std::vector<Trade> trades;
    trades.reserve(64);
    std::vector<double> lat;
    lat.reserve(N);
    std::uint64_t trade_cnt = 0;

    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();
    for (const Op &op: ops) {
        trades.clear();
        const auto a = clk::now();
        switch (op.kind) {
            case Op::Limit: book.add_limit(op.o, trades);
                break;
            case Op::Market: book.add_market(op.o.id, op.o.side, op.o.qty, trades);
                break;
            case Op::Cancel: book.cancel(op.target);
                break;
        }
        const auto b = clk::now();
        lat.push_back(std::chrono::duration<double, std::nano>(b - a).count());
        trade_cnt += trades.size();
    }
    const double total_s = std::chrono::duration<double>(clk::now() - t0).count();

    std::sort(lat.begin(), lat.end());
    auto pct = [&](double p) { return lat[static_cast<std::size_t>(p * (lat.size() - 1))]; };
    std::printf("ops=%zu trades=%llu live_orders=%zu levels=%zu\n",
                N, static_cast<unsigned long long>(trade_cnt), book.order_count(), book.level_count());
    std::printf("throughput: %.2f Mops/s\n", N / total_s / 1e6);
    std::printf("latency ns (включая ~20-30ns накладных clock::now): p50=%.0f p90=%.0f p99=%.0f p99.9=%.0f max=%.0f\n",
                pct(0.50), pct(0.90), pct(0.99), pct(0.999), lat.back());
}

int main() {
    test_correctness();
    benchmark();
}
