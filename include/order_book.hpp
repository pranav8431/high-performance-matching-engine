#pragma once
#include <cstdint>
#include "price_level.hpp"
#include <map>
class OrderBook{
    private:
        std:: map<int64_t , PriceLevel , std::greater<int64_t>> bids;
        std:: map<int64_t , PriceLevel > asks;
    public:
        void add_order(const Order& order){
            if(order.side == Side::BUY){
                auto [it , inserter] = bids.try_emplace(order.price , PriceLevel(order.price));
                it->second.add_order(order);
            }
            else{
                auto [it , inserted] = asks.try_emplace(order.price , PriceLevel(order.price));
                it->second.add_order(order);
            }
        }
        bool empty() const{
            return bids.empty() && asks.empty();
        }
        bool has_bids() const{
            return !bids.empty();

        }bool has_asks() const{
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
        bool cancel_order(uint64_t order_id){
            // Implementation for canceling an order
            //search buy side
            for(auto it = bids.begin() ; it!= bids.end() ; it++){
                PriceLevel& level = it->second;
                //search orders at this price level
                for(std:: size_t i = 0 ; i < level.size() , i++){
                    Order& order = level.get_order(i);
                    if(order.id == order_id){
                        level.remove_order(i);
                        if(level.is_empty()){
                            bids.erase(it);
                        }
                        return true;
                    }
                }
            }
            //search sell side
            for(auto it = asks.begin() ; it != asks.end() ; it++){
                
            }
        }
};