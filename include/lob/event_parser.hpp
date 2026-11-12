#pragma once

// Text event format, one event per line:
//
//   ADD    <id> <BUY|SELL> <price> <qty>     new GTC limit order
//   IOC    <id> <BUY|SELL> <price> <qty>     immediate-or-cancel limit
//   FOK    <id> <BUY|SELL> <price> <qty>     fill-or-kill limit
//   MARKET <id> <BUY|SELL> <qty>             market order
//   CANCEL <id>
//   MODIFY <id> <price> <qty>                cancel/replace
//
// Blank lines and lines starting with '#' are ignored. B / S are accepted as
// side abbreviations. Parsing uses std::from_chars: no iostreams, no locale,
// no allocation.

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

#include "lob/market_event.hpp"

namespace lob {

struct ParseError {
    std::string message;
};

class EventParser {
public:
    // Returns true and fills `out` on success. Returns false for a comment or
    // blank line (nothing to do) or sets `error` and returns false on a
    // malformed line. Check error.message.empty() to distinguish.
    static bool parse(std::string_view line, MarketEvent& out, ParseError& error) {
        error.message.clear();
        Tokenizer tok(line);

        std::string_view type;
        if (!tok.next(type) || type.front() == '#') return false;

        out = MarketEvent{};

        if (type == "ADD" || type == "IOC" || type == "FOK") {
            out.type = type == "ADD" ? EventType::Add : type == "IOC" ? EventType::Ioc : EventType::Fok;
            return read_id(tok, out, error) && read_side(tok, out, error) &&
                   read_price(tok, out, error) && read_qty(tok, out, error) && end(tok, error);
        }
        if (type == "MARKET") {
            out.type = EventType::Market;
            return read_id(tok, out, error) && read_side(tok, out, error) &&
                   read_qty(tok, out, error) && end(tok, error);
        }
        if (type == "CANCEL") {
            out.type = EventType::Cancel;
            return read_id(tok, out, error) && end(tok, error);
        }
        if (type == "MODIFY") {
            out.type = EventType::Modify;
            return read_id(tok, out, error) && read_price(tok, out, error) &&
                   read_qty(tok, out, error) && end(tok, error);
        }

        error.message = "unknown event type '" + std::string(type) + "'";
        return false;
    }

    static void format(const MarketEvent& e, std::string& out) {
        out.clear();
        out += to_string(e.type);
        out += ' ';
        out += std::to_string(e.order_id);
        switch (e.type) {
            case EventType::Add:
            case EventType::Ioc:
            case EventType::Fok:
                out += ' '; out += to_string(e.side);
                out += ' '; out += std::to_string(e.price);
                out += ' '; out += std::to_string(e.quantity);
                break;
            case EventType::Market:
                out += ' '; out += to_string(e.side);
                out += ' '; out += std::to_string(e.quantity);
                break;
            case EventType::Cancel:
                break;
            case EventType::Modify:
                out += ' '; out += std::to_string(e.price);
                out += ' '; out += std::to_string(e.quantity);
                break;
        }
    }

private:
    class Tokenizer {
    public:
        explicit Tokenizer(std::string_view s) : s_(s) {}

        bool next(std::string_view& tok) {
            while (pos_ < s_.size() && is_space(s_[pos_])) ++pos_;
            if (pos_ >= s_.size()) return false;
            const std::size_t start = pos_;
            while (pos_ < s_.size() && !is_space(s_[pos_])) ++pos_;
            tok = s_.substr(start, pos_ - start);
            return true;
        }

    private:
        static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ','; }
        std::string_view s_;
        std::size_t pos_ = 0;
    };

    template <typename T>
    static bool read_int(Tokenizer& tok, T& value, const char* what, ParseError& error) {
        std::string_view t;
        if (!tok.next(t)) {
            error.message = std::string("missing ") + what;
            return false;
        }
        const auto res = std::from_chars(t.data(), t.data() + t.size(), value);
        if (res.ec != std::errc{} || res.ptr != t.data() + t.size()) {
            error.message = std::string("invalid ") + what + " '" + std::string(t) + "'";
            return false;
        }
        return true;
    }

    static bool read_id(Tokenizer& tok, MarketEvent& e, ParseError& err)    { return read_int(tok, e.order_id, "order id", err); }
    static bool read_price(Tokenizer& tok, MarketEvent& e, ParseError& err) { return read_int(tok, e.price, "price", err); }
    static bool read_qty(Tokenizer& tok, MarketEvent& e, ParseError& err)   { return read_int(tok, e.quantity, "quantity", err); }

    static bool read_side(Tokenizer& tok, MarketEvent& e, ParseError& error) {
        std::string_view t;
        if (!tok.next(t)) {
            error.message = "missing side";
            return false;
        }
        if (t == "BUY" || t == "B") { e.side = Side::Buy; return true; }
        if (t == "SELL" || t == "S") { e.side = Side::Sell; return true; }
        error.message = "invalid side '" + std::string(t) + "'";
        return false;
    }

    static bool end(Tokenizer& tok, ParseError& error) {
        std::string_view t;
        if (tok.next(t) && t.front() != '#') {
            error.message = "unexpected trailing token '" + std::string(t) + "'";
            return false;
        }
        return true;
    }
};

}  // namespace lob
