#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>
#include <list>
#include <vector>

#include "IdGenerator.hpp"

constexpr int32_t NULL_INDEX = -1;

enum class Side : uint8_t {
    Buy,
    Sell
};

struct Order {
    uint64_t id;
    uint64_t price;
    uint64_t quantity;
    Side side;
};

struct Trade {
    uint64_t buyerOrderId;
    uint64_t sellerOrderId;
    uint64_t quantity;
    uint64_t price;
    uint64_t timestamp;
};

struct OrderLocation {
    uint64_t price;
    uint64_t index;
    Side side;
};

struct OrderNode {
    Order order;
    int32_t prev = NULL_INDEX;
    int32_t next = NULL_INDEX;

    bool active = false;
};

struct PriceLevel {
    int32_t head = NULL_INDEX;
    int32_t tail = NULL_INDEX;

    uint64_t price;
    Side side;
};



class OrderEngine {
private:
    std::vector<OrderNode> orderPool;
    std::vector<int> idToIndex;
    int32_t freeHead = NULL_INDEX;

    // Maps Price -> List of orders at that price
    std::map<uint64_t, PriceLevel> bids;
    std::map<uint64_t, PriceLevel> asks;
    std::vector<Trade> trades;

    // RingBuffer<Trade> logger;
    IdGenerator idGen;


public:
    OrderEngine();

    // Places order to the list. Assumes orders are processed in-order. Returns orderId
    uint64_t addOrder(uint64_t price, uint64_t quantity, Side side);

    // Cancels order given by the orderId
    bool cancelOrder(uint64_t orderId);
    bool updateOrder(uint64_t orderId, uint64_t newPrice, uint64_t newQuantity);

    // Getters for testing
    const std::vector<Trade>& getTrades() const { return trades; }
    void clearTrades() { trades.clear(); }
    bool isAskEmpty() const { return asks.empty(); }
    bool isBidEmpty() const { return bids.empty(); }

    // Returns {price, quantity}
    std::pair<uint64_t, uint64_t> getBestAsk() const;
    std::pair<uint64_t, uint64_t> getBestBid() const;
    const std::map<uint64_t, PriceLevel>& getBids() const { return bids; }
    const std::map<uint64_t, PriceLevel>& getAsks() const { return asks; }
    PriceLevel& getBestAskLevel() { return asks.begin()->second; }
    PriceLevel& getBestBidLevel() { return bids.rbegin()->second; }

    void clear();


private:
    // Internal helper signatures
    uint64_t addOrder(Order order);
    void matchBuyOrder(Order& aggressorBid);
    void matchSellOrder(Order& agressorAsk);
    int32_t allocateSlot();
    void unlinkNode(int32_t idx, PriceLevel& priceLevel);
    int32_t addToPool(Order& order);
    uint64_t getCurrentTimestamp();
};