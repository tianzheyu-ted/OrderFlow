
#include "OrderFlow.hpp"
#include "IdGenerator.hpp"
#include "RingBuffer.hpp"

#include <cstdint>      // Required for uint64_t
#include <map>          // Required for std::map
#include <list>         // Required for std::list
#include <functional>   // Required for std::greater and std::less
#include <vector>
#include <chrono>
#include <iostream>

// Constructer

OrderEngine::OrderEngine() : idGen{} {
    idToIndex.resize(1'000'000);
    orderPool.reserve(50'000);
    trades.reserve(100'000);
}

// Private Methods

uint64_t OrderEngine::addOrder(Order order) {
    uint64_t id = order.id;
    if (id >= idToIndex.size()) {
        size_t newSize = std::max(idToIndex.size() * 2, static_cast<size_t>(id + 1));
        idToIndex.resize(newSize);
    }

    if (order.side == Side::Buy) {
        matchBuyOrder(order);
    } else {
        matchSellOrder(order);
    }

    return order.id;
}

void OrderEngine::matchBuyOrder(Order& aggressorBid) {
    uint64_t executionTime = getCurrentTimestamp();
    
    while (aggressorBid.quantity > 0 && !asks.empty()) {
        auto askLevelItem = asks.begin();
        auto& askLevel = askLevelItem->second;

        auto& node = orderPool[askLevel.head];
        auto& restingAsk = node.order;

        if (aggressorBid.price < restingAsk.price) break;

        uint64_t restingPrice = restingAsk.price;
        uint64_t tradeQty = std::min(aggressorBid.quantity, restingAsk.quantity);

        restingAsk.quantity -= tradeQty;
        aggressorBid.quantity -= tradeQty;

        Trade newTrade = {
            aggressorBid.id,
            restingAsk.id,
            tradeQty,
            restingPrice,
            executionTime
        };

        trades.push_back(newTrade);

        if (restingAsk.quantity == 0) {
            uint64_t filledOrderId = restingAsk.id;
            unlinkNode(askLevel.head, askLevel);
            idToIndex[filledOrderId] = NULL_INDEX;
            
            if (askLevel.head == NULL_INDEX) {
                asks.erase(askLevelItem);
            }
        }
    }

    if (aggressorBid.quantity > 0) {
        auto& priceLevel = bids[aggressorBid.price];

        int32_t idx = addToPool(aggressorBid);
        idToIndex[aggressorBid.id] = idx;

        // Assign fields in case priceLevel is fresh
        priceLevel.price = aggressorBid.price;
        priceLevel.side = Side::Buy;
    }
}

void OrderEngine::matchSellOrder(Order& aggressorAsk) {
    uint64_t executionTime = getCurrentTimestamp();

    while (aggressorAsk.quantity > 0 && !bids.empty()) {
        auto bidLevelIt = bids.rbegin();
        auto& bidLevel = bidLevelIt->second;

        auto& node = orderPool[bidLevel.head];
        auto& restingBid = node.order;

        if (aggressorAsk.price > restingBid.price) break;

        uint64_t restingPrice = restingBid.price;
        uint64_t tradeQty = std::min(aggressorAsk.quantity, restingBid.quantity);

        restingBid.quantity -= tradeQty;
        aggressorAsk.quantity -= tradeQty;

        Trade newTrade{restingBid.id, aggressorAsk.id, tradeQty, restingPrice, executionTime};
        trades.push_back(newTrade);

        if (restingBid.quantity == 0) {
            uint64_t filledOrderId = restingBid.id;
            unlinkNode(bidLevel.head, bidLevel);
            idToIndex[filledOrderId] = NULL_INDEX;
            
            if (bidLevel.head == NULL_INDEX) {
                bids.erase(std::prev(bidLevelIt.base()));
            }
        }
    }

    if (aggressorAsk.quantity > 0) {
        auto& priceLevel = asks[aggressorAsk.price];

        int32_t index = addToPool(aggressorAsk);
        idToIndex[aggressorAsk.id] = index;

        priceLevel.price = aggressorAsk.price;
        priceLevel.side = Side::Sell;
    }
}

int32_t OrderEngine::allocateSlot() {
    if (freeHead != NULL_INDEX) {
        int32_t idx = freeHead;
        freeHead = orderPool[idx].next;
        return idx;
    } else {
        orderPool.emplace_back();
        return orderPool.size() - 1;
    }
}

void OrderEngine::unlinkNode(int32_t idx, PriceLevel& priceLevel) {
    auto& node = orderPool[idx];

    if (node.prev != NULL_INDEX) {
        orderPool[node.prev].next = node.next;
    } else {
        priceLevel.head = node.next;
    }

    if (node.next != NULL_INDEX) {
        orderPool[node.next].prev = node.prev;
    } else {
        priceLevel.tail = node.prev;
    }

    node.next = NULL_INDEX;
    node.prev = NULL_INDEX;

    node.active = false;
    node.next = freeHead;
    freeHead = idx;
}

int32_t OrderEngine::addToPool(Order& order) {
    int32_t index = allocateSlot();

    orderPool[index].next = NULL_INDEX;
    orderPool[index].prev = NULL_INDEX;
    orderPool[index].order = order;
    orderPool[index].active = true;

    PriceLevel& priceLevel = (order.side == Side::Buy) ? bids[order.price] : asks[order.price];
    if (priceLevel.head == NULL_INDEX && priceLevel.tail == NULL_INDEX) {
        priceLevel.head = index;
        priceLevel.tail = index;
    } else {
        orderPool[priceLevel.tail].next = index;
        orderPool[index].prev = priceLevel.tail;
        priceLevel.tail = index;
    }

    return index;
}

uint64_t OrderEngine::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
    return nanoseconds.count();
}

// Public Methods

uint64_t OrderEngine::addOrder(uint64_t price, uint64_t quantity, Side side) {
    if (price <= 0 || quantity <= 0) {
        std::cout << "OrderEngine::addOrder: price or quantity invalid.\n";
        return 0;
    }

    Order order = {
        idGen.next(),
        price,
        quantity,
        side
    };

    return addOrder(order);
}


bool OrderEngine::cancelOrder(uint64_t orderId) {
    if (orderId >= idToIndex.size()) {
        std::cout << "OrderEngine::cancelOrder: orderId invalid.\n";
        return false;
    }

    int32_t index = idToIndex[orderId];
    if (index == NULL_INDEX) {
        std::cout << "OrderEngine::cancelOrder: order doesn't exist or already filled/cancelled.\n";
        return false;
    }

    auto& node = orderPool[index];
    if (!node.active) {
        std::cout << "OrderEngine::cancelOrder: order is not active.\n";
        return false;
    }

    // Verify that the order in the pool is the one we want to cancel.
    // This prevents cancelling a new order that recycled an old order's slot.
    if (node.order.id != orderId) {
        std::cout << "OrderEngine::cancelOrder: order in the pool is not the one aimed to cancel.\n";
        return false; // Stale idToIndex node. The original order was likely filled.
    }

    auto& map = (node.order.side == Side::Buy) ? bids : asks;
    auto levelItem = map.find(node.order.price);
    if (levelItem == map.end()) {
        // Should be an unreachable state if logic is correct, but good to have.
        std::cout << "OrderEngine::cancelOrder: invalid (should be unreachable).\n";
        return false;
    }

    unlinkNode(index, levelItem->second);
    idToIndex[orderId] = NULL_INDEX;

    if (levelItem->second.head == NULL_INDEX) {
        map.erase(levelItem);
    }

    return true;
}


bool OrderEngine::updateOrder(uint64_t orderId, uint64_t newPrice, uint64_t newQuantity) {
    if (orderId == 0 || orderId >= idToIndex.size() || newPrice <= 0 || newQuantity <= 0) {
        std::cout << "OrderEngine::updateOrder: invalid parameter.\n";
        return false;
    }

    int32_t index = idToIndex[orderId];
    if (index == NULL_INDEX) {
        std::cout << "OrderEngine::updateOrder: order already cancelled.\n";
        return false;
    }

    // Verify that the order in the pool is the one we want to modify.
    // This prevents modifying a new order that recycled an old order's slot.
    if (orderPool[index].order.id != orderId) {
        std::cout << "OrderEngine::updateOrder: order in the pool is not the one aimed to update.\n";
        return false; // Stale idToIndex node. The original order was likely filled.
    }

    auto& order = orderPool[index].order;
    if (order.price != newPrice || order.quantity < newQuantity) {
        cancelOrder(orderId);
        addOrder({orderId, newPrice, newQuantity, order.side});
    } else {
        // implying order.quantity >= newQuantity
        order.quantity = newQuantity;
    }

    return true;
}


std::pair<uint64_t, uint64_t> OrderEngine::getBestAsk() const {
    if (asks.empty()) return {0, 0};
    uint64_t price = asks.begin()->first;
    auto& priceLevel = asks.begin()->second;
    uint64_t quantity = orderPool[priceLevel.head].order.quantity;
    return {price, quantity};
}


std::pair<uint64_t, uint64_t> OrderEngine::getBestBid() const {
    if (bids.empty()) return {0, 0};
    uint64_t price = bids.rbegin()->first;
    auto& priceLevel = bids.rbegin()->second;
    uint64_t quantity = orderPool[priceLevel.head].order.quantity;
    return {price, quantity};
}


void OrderEngine::clear() 
{
    idToIndex.clear();
    idToIndex.resize(1'000'000);
    orderPool.clear();
    freeHead = NULL_INDEX;
    bids.clear();
    asks.clear();
    trades.clear();
    idGen.reset();
}