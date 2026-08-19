#pragma once

#include <iterator>
#include <cstdint>
#include <list>
#include "order.hpp"

class PriceLevel{
    private:
        int64_t price;
        std :: list<Order> orders;
        uint64_t total_quantity;

    public:

        explicit PriceLevel(int64_t price){
            this->price = price;
            this->total_quantity = 0 ;
        }

        int64_t get_price() const{
            return this->price;
        }

        bool is_empty() const{
            return this->orders.empty();
        }

        Order& front(){
            return this->orders.front();
        }

        std:: size_t size() const{
            return this->orders.size();
        }

        std :: list<Order> :: iterator add_order(const Order& order){
            orders.push_back(order);
            total_quantity += order.quantity;
            return std :: prev(orders.end());
        }

        void reduce_front(uint64_t quantity){
            orders.front().quantity -= quantity;
            total_quantity -= quantity;
        }
        
        void remove_front(){
            total_quantity -= this->orders.front().quantity;
            orders.pop_front();
        }

        auto begin(){
            return orders.begin();
        }

        auto end(){
            return orders.end();
        }
        
        void erase(std :: list<Order>:: iterator it){
            total_quantity -= it->quantity;
            orders.erase(it);
        }
};