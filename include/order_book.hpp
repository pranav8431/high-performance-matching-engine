#pragma once
#include <cstdint>
#include "price_level.hpp"
#include <map>
#include <unordered_map>
#include <list>
#include <functional>


class OrderBook{
    private:
        std:: map<int64_t , PriceLevel , std::greater<int64_t>> bids;
        std:: map<int64_t , PriceLevel > asks;
        struct OrderLocation{
            Side side;
            int64_t price;
            std :: list<Order>:: iterator iterator;
        };
        std :: unordered_map<uint64_t , OrderLocation> order_index;

    public:

        void add_order(const Order& order){
            if(order.side == Side::BUY){
                auto [it , inserter] = bids.try_emplace(order.price , PriceLevel(order.price));
                auto order_it = it->second.add_order(order);
                order_index[order.id] = {
                    Side :: BUY,
                    order.price,
                    order_it
                };
            }
            else{
                auto [it , inserted] = asks.try_emplace(order.price , PriceLevel(order.price));
                auto order_it = it->second.add_order(order);
                order_index[order.id] = {
                    Side :: SELL,
                    order.price,
                    order_it
                };
            }
        }

        bool empty() const{
            return bids.empty() && asks.empty();
        }

        bool has_bids() const{
            return !bids.empty();

        }
        
        bool has_asks() const{
            return !asks.empty();
        }
        
        int64_t best_bid() const{
            return bids.begin()->first;
        }
        
        int64_t best_ask() const{
            return asks.begin()->first;
        }
        
        PriceLevel& best_bid_level(){
            return bids.begin()->second;
        }
        
        PriceLevel& best_ask_level(){
            return asks.begin()->second;
        }
        
        void remove_best_bid_level(){
            if(!bids.empty()){
                bids.erase(bids.begin());
            }
        }
        
        void remove_best_ask_level(){
            if(!asks.empty()){
                asks.erase(asks.begin());
            }
        }


        void remove_from_index(uint64_t order_id){
            order_index.erase(order_id);
        }
        
        bool modify_order(uint64_t order_id , int64_t new_price , uint64_t new_quantity ){

        };


        bool cancel_order(uint64_t order_id) {
            auto index_it = order_index.find(order_id);

            if (index_it == order_index.end()) {
                return false;
            }

            OrderLocation location = index_it->second;

            if (location.side == Side::BUY) {
                auto level_it = bids.find(location.price);

                if (level_it != bids.end()) {
                    level_it->second.erase(location.iterator);

                    if (level_it->second.is_empty()) {
                        bids.erase(level_it);
                    }
                }
            }
            else {
                auto level_it = asks.find(location.price);

                if (level_it != asks.end()) {
                    level_it->second.erase(location.iterator);

                    if (level_it->second.is_empty()) {
                        asks.erase(level_it);
                    }
                }
            }

            order_index.erase(index_it);

            return true;
        }
};