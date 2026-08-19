#pragma once 

#include "market_event.hpp"
#include "matching_engine.hpp"

class MarketRelay{
    private:
        OrderBook & order_book;
        MatchingEngine matching_engine;
    public:
        explicit MarketRelay(OrderBook & order_book) : order_book(order_book) , matching_engine(order_book){}
        
        void process_event(const MarketEvent &event){
            switch(event.type){
                case MarketEventType :: ADD:{
                    Order order{
                        event.order_id,
                        event.side,
                        OrderType :: LIMIT,
                        event.Price,
                        event.quantity
                    };
                    matching_engine.process_order(order);
                    break;
                }
            }
        }
};