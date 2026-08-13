#pragma once


#include <cstdint>
#include <deque>
#include "order.hpp"

class PriceLevel{
    private:
        int64_t price;
        std :: deque<Order> orders;
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
        void add_order(const Order& order){
            orders.push_back(order);
            total_quantity += order.quantity;
        }
        void reduce_front(uint64_t quantity){
            orders.front().quantity -= quantity;
            total_quantity -= quantity;
        }
        void remove_front(){
            total_quantity -= this->orders.front().quantity;
            orders.pop_front();
        }
        Order& get_order(std :: size_t index){
            return orders[index];
        }
        void remove_order(std:: size_t index){
            total_quantity -= orders[index].quantity;
            orders.erase(orders.begin() + index);
        }
};