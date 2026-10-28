#pragma once
// Stage 1, v1: baseline order book. std::map по ценам + std::list заявок на уровне.
// Цель: простая и корректная реализация, от которой будем мерить улучшения.
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

namespace ob {
    using OrderId = std::uint64_t;
    using Price = std::int64_t; // в тиках, не double
    using Qty = std::uint32_t;

    enum class Side : std::uint8_t {
        Buy,
        Sell
    };

    struct Order {
        OrderId id;
        Price price;
        Qty qty;
        Side side;
    };

    struct Trade {
        OrderId maker;
        OrderId taker;
        Price price;
        Qty qty;
    };

    class OrderBookV1 {
    public:
        // Limit: матчится по price-time priority, остаток встаёт в книгу.
        void add_limit(Order o, std::vector<Trade> &trades) {
            match(o, trades);
            if (o.qty > 0) rest(o);
        }

        // Market: матчится по любой цене, остаток отбрасывается.
        void add_market(OrderId id, Side side, Qty qty, std::vector<Trade> &trades) {
            Order o{
                id, side == Side::Buy
                        ? std::numeric_limits<Price>::max()
                        : std::numeric_limits<Price>::min(),
                qty, side
            };
            match(o, trades);
        }

        bool cancel(OrderId id) {
            auto it = index_.find(id);
            if (it == index_.end()) return false;
            const Locator loc = it->second;
            index_.erase(it);
            if (loc.side == Side::Buy) erase_from(bids_, loc);
            else erase_from(asks_, loc);
            return true;
        }

        [[nodiscard]] Price best_bid() const { return bids_.empty() ? 0 : bids_.begin()->first; }
        [[nodiscard]] Price best_ask() const { return asks_.empty() ? 0 : asks_.begin()->first; }
        [[nodiscard]] std::size_t order_count() const { return index_.size(); }
        [[nodiscard]] std::size_t level_count() const { return bids_.size() + asks_.size(); }

    private:
        using Level = std::list<Order>;
        using Bids = std::map<Price, Level, std::greater<> >; // лучшая цена = begin()
        using Asks = std::map<Price, Level>;

        struct Locator {
            Side side;
            Price price;
            Level::iterator pos;
        };

        void match(Order &o, std::vector<Trade> &trades) {
            if (o.side == Side::Buy)
                match_against(o, asks_, trades, [](Price lvl, Price lim) { return lvl <= lim; });
            else
                match_against(o, bids_, trades, [](Price lvl, Price lim) { return lvl >= lim; });
        }

        template<class Levels, class Crosses>
        void match_against(Order &o, Levels &levels, std::vector<Trade> &trades, Crosses crosses) {
            while (o.qty > 0 && !levels.empty()) {
                auto lvl = levels.begin();
                if (!crosses(lvl->first, o.price)) break;
                Level &q = lvl->second;
                while (o.qty > 0 && !q.empty()) {
                    Order &maker = q.front();
                    const Qty fill = std::min(o.qty, maker.qty);
                    trades.push_back({maker.id, o.id, lvl->first, fill});
                    o.qty -= fill;
                    maker.qty -= fill;
                    if (maker.qty == 0) {
                        index_.erase(maker.id);
                        q.pop_front();
                    }
                }
                if (q.empty()) levels.erase(lvl);
            }
        }

        void rest(const Order &o) {
            Level &q = (o.side == Side::Buy) ? bids_[o.price] : asks_[o.price];
            q.push_back(o);
            index_[o.id] = Locator{o.side, o.price, std::prev(q.end())};
        }

        template<class Levels>
        static void erase_from(Levels &levels, const Locator &loc) {
            auto lvl = levels.find(loc.price);
            lvl->second.erase(loc.pos);
            if (lvl->second.empty()) levels.erase(lvl);
        }

        Bids bids_;
        Asks asks_;
        std::unordered_map<OrderId, Locator> index_;
    };
} // namespace ob
