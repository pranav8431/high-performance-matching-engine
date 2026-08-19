#pragma once

#include <cstdint>
#include "order.hpp"

enum class MarketEventType{
    ADD,
    CANCEL,
    MODIFY
};



struct MarketEvent{
    MarketEventType type;
    uint64_t order_id;
    Side side;
    int64_t Price;
    uint64_t quantity;
};