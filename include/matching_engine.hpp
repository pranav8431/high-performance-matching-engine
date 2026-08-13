#pragma once
#include <cstdint>
#include "price_level.hpp"
#include "order_book.hpp"
#include <algorithm>
#include <vector>
#include "trade.hpp"
class MatchingEngine {
    private:
        OrderBook& order_book;
    public:
        explicit MatchingEngine(OrderBook& order_book) : order_book(order_book){}
    
    std::vector<Trade> process_order(Order order){
        std:: vector<Trade> trades;
        if(order.side == Side :: BUY){
            process_buy(order , trades);
        }
        else{
            process_sell(order , trades);
        }
        return trades;
    }
    private:
        void process_buy(Order& order ,std ::vector<Trade>& trades){
            while(order.quantity > 0 && order_book.has_asks()){
                PriceLevel& ask_level = order_book.best_ask_level();
                // LIMIT order can only be matched if the best ask price is less than or equal to the order price
                if(order.ordertype == OrderType::LIMIT && order.price < ask_level.get_price()){
                    break;
                }
                Order&  sell_order= ask_level.front();
                uint64_t traded_quantity = std:: min( order.quantity , sell_order.quantity);
                trades.push_back({
                    order.id,
                    sell_order.id,
                    ask_level.get_price(),
                    traded_quantity
                });
                order.quantity -= traded_quantity;
                ask_level.reduce_front(traded_quantity);
                if(ask_level.front().quantity == 0){
                    ask_level.remove_front();
                }
                if(ask_level.is_empty()){
                    order_book.remove_best_ask_level();
                }
            }
            //Reamaining quantity of the order is added to the order book if it is a LIMIT order
            if(order.quantity > 0 && order.ordertype == OrderType::LIMIT){
                order_book.add_order(order);
            }
        }

        void process_sell(Order& order ,std::vector<Trade>& trades){
            while(order.quantity > 0 && order_book.has_bids()){
                PriceLevel& bid_level = order_book.best_bid_level();
                // LIMIT order can only be matched if the best bid price is greater than or equal to the order price
                if(order.ordertype == OrderType::LIMIT && order.price > bid_level.get_price()){
                    break;
                }
                Order& buy_order = bid_level.front();
                uint64_t traded_quantity = std :: min(order.quantity , buy_order.quantity);

                trades.push_back({
                    buy_order.id,
                    order.id,
                    bid_level.get_price(),
                    traded_quantity
                });
                order.quantity -= traded_quantity;
                bid_level.reduce_front(traded_quantity);
                if(bid_level.front().quantity == 0){
                    bid_level.remove_front();
                }
                if(bid_level.is_empty()){
                    order_book.remove_best_bid_level();
                }
            }
            if(order.quantity > 0 && order.ordertype == OrderType :: LIMIT){
                order_book.add_order(order);
            }
        }


        // void match_buy_order(Order& order){
        //     while(order.quantity > 0 && order_book.has_asks() ){
        //         PriceLevel& best_ask_level  = order_book.best_ask_level();
        //         if(order.ordertype == OrderType:: LIMIT && order.price < best_ask_level.get_price()){
        //             break;
        //         }
        //         Order& best_ask_order = best_ask_level.front();
        //         uint64_t traded_quantity = std::min(order.quantity , best_ask_order.quantity);
        //         order.quantity -= traded_quantity;
        //         best_ask_order.quantity -= traded_quantity;
        //         if(best_ask_order.quantity == 0){
        //             best_ask_level.remove_front(); 
        //         }
        //     }
        //     if(order.quantity > 0 && order.ordertype == OrderType::LIMIT){
        //         order_book.add_order(order);
        //     }
        // }
        // void match_sell_order(Order& order){
        //     while(order.quantity > 0 && order_book.has_bids())
        //     {
        //         PriceLevel& best_bid_level = order_book.best_bid_level();
        //         if(order.ordertype == OrderType::LIMIT && order.price > best_bid_level.get_price()){
        //             break;
        //         }
        //         Order& best_bid_order = best_bid_level.front();
        //         uint64_t traded_quantity = std::min(order.quantity , best_bid_order.quantity);
        //         order.quantity -= traded_quantity;
        //         best_bid_order.quantity -= traded_quantity;
        //         if(best_bid_order.quantity == 0){
        //             best_bid_level.remove_front();
        //         }
        //     }
        // }
};
