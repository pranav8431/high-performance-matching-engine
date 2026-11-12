// Replay a text event file through the matching engine.
//
//   replay <events.txt> [--min-price P] [--max-price P] [--trades out.txt] [--depth N] [--quiet]
//
// The file is parsed fully into memory first, then processed with the timer
// running, so the reported throughput is engine time only. Prints relay
// statistics and the top N levels of the final book.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "lob/event_parser.hpp"
#include "lob/market_relay.hpp"
#include "lob/matching_engine.hpp"

using namespace lob;

namespace {

struct Options {
    const char* path      = nullptr;
    const char* trade_out = nullptr;
    Price       min_price = 1;
    Price       max_price = 1'000'000;
    int         depth     = 5;
    bool        quiet     = false;
};

bool parse_args(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        auto value = [&](Price& dst) { if (++i >= argc) return false; dst = std::strtoll(argv[i], nullptr, 10); return true; };
        if (std::strcmp(a, "--min-price") == 0) { if (!value(o.min_price)) return false; }
        else if (std::strcmp(a, "--max-price") == 0) { if (!value(o.max_price)) return false; }
        else if (std::strcmp(a, "--depth") == 0) { if (++i >= argc) return false; o.depth = std::atoi(argv[i]); }
        else if (std::strcmp(a, "--trades") == 0) { if (++i >= argc) return false; o.trade_out = argv[i]; }
        else if (std::strcmp(a, "--quiet") == 0) { o.quiet = true; }
        else if (a[0] == '-') { std::fprintf(stderr, "unknown option %s\n", a); return false; }
        else o.path = a;
    }
    return o.path != nullptr;
}

bool load_events(const char* path, std::vector<MarketEvent>& events) {
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return false;
    }
    std::string line;
    std::size_t line_no = 0;
    MarketEvent e{};
    ParseError err;
    while (std::getline(in, line)) {
        ++line_no;
        if (EventParser::parse(line, e, err)) {
            events.push_back(e);
        } else if (!err.message.empty()) {
            std::fprintf(stderr, "%s:%zu: %s\n", path, line_no, err.message.c_str());
            return false;
        }
    }
    return true;
}

void print_depth(const OrderBook& book, int depth) {
    std::vector<const PriceLevel*> bids, asks;
    book.for_each_level(Side::Buy,  [&](const PriceLevel& l) { bids.push_back(&l); return static_cast<int>(bids.size()) < depth; });
    book.for_each_level(Side::Sell, [&](const PriceLevel& l) { asks.push_back(&l); return static_cast<int>(asks.size()) < depth; });
    std::printf("\n  %10s %12s | %-12s %10s\n", "bid qty", "bid", "ask", "ask qty");
    std::printf("  --------------------------------------------------\n");
    for (std::size_t i = 0; i < std::max(bids.size(), asks.size()); ++i) {
        if (i < bids.size()) std::printf("  %10llu %12lld | ", (unsigned long long)bids[i]->total_quantity, (long long)bids[i]->price);
        else                 std::printf("  %10s %12s | ", "", "");
        if (i < asks.size()) std::printf("%-12lld %10llu\n", (long long)asks[i]->price, (unsigned long long)asks[i]->total_quantity);
        else                 std::printf("\n");
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) {
        std::fprintf(stderr, "usage: replay <events.txt> [--min-price P] [--max-price P] [--depth N] [--trades out.txt] [--quiet]\n");
        return 2;
    }

    std::vector<MarketEvent> events;
    if (!load_events(opt.path, events)) return 1;

    OrderBook book(opt.min_price, opt.max_price, std::max<std::size_t>(1024, events.size() / 4));
    MatchingEngine engine(book);

    std::FILE* trade_file = opt.trade_out ? std::fopen(opt.trade_out, "w") : nullptr;
    if (opt.trade_out && !trade_file) {
        std::fprintf(stderr, "cannot open %s for writing\n", opt.trade_out);
        return 1;
    }
    auto sink = [trade_file](const Trade& t) {
        if (trade_file) {
            std::fprintf(trade_file, "TRADE %llu %llu %lld %u %s\n", (unsigned long long)t.buy_order_id,
                         (unsigned long long)t.sell_order_id, (long long)t.price, t.quantity, to_string(t.aggressor));
        }
    };
    MarketRelay<decltype(sink)> relay(engine, sink);

    const auto start = std::chrono::steady_clock::now();
    for (const MarketEvent& e : events) relay.process(e);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (trade_file) std::fclose(trade_file);

    const RelayStats& s = relay.stats();
    std::printf("Replayed %zu events from %s in %.4f s  (%.2f M events/s)\n", events.size(), opt.path, secs,
                secs > 0 ? static_cast<double>(events.size()) / secs / 1e6 : 0.0);
    if (!opt.quiet) {
        std::printf("  adds %llu  ioc %llu  fok %llu  market %llu  cancels %llu (misses %llu)  modifies %llu (misses %llu)  rejects %llu\n",
                    (unsigned long long)s.adds, (unsigned long long)s.iocs, (unsigned long long)s.foks,
                    (unsigned long long)s.markets, (unsigned long long)s.cancels, (unsigned long long)s.cancel_misses,
                    (unsigned long long)s.modifies, (unsigned long long)s.modify_misses, (unsigned long long)s.rejects);
        std::printf("  trades %llu  traded quantity %llu  resting orders %zu  levels bid/ask %zu/%zu\n",
                    (unsigned long long)s.trades, (unsigned long long)s.traded_quantity, book.order_count(),
                    book.level_count(Side::Buy), book.level_count(Side::Sell));
        if (book.has_bids()) std::printf("  best bid %lld", (long long)book.best_bid_price()); else std::printf("  no bids");
        if (book.has_asks()) std::printf("  best ask %lld\n", (long long)book.best_ask_price()); else std::printf("  no asks\n");
        print_depth(book, opt.depth);
    }
    return 0;
}
