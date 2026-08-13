#pragma once
#include <cstdint>

enum class Side{
    BUY,
    SELL
};

enum class OrderType{
    LIMIT,
    MARKET
};

struct Order{
    uint64_t id;
    Side side;
    OrderType ordertype;
    int64_t price;
    uint64_t quantity;
};