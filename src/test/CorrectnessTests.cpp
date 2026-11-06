#include "../OrderFlow.hpp"

#include <iostream>
#include <cassert>

void run_tests() {
    std::cout << "Running Correctness Tests...\n";

    // TEST 1: Basic Full Match
    // Scenario: Seller puts 100 @ $10. Buyer takes all.
    {
        OrderEngine ob;
        uint64_t orderId1 = ob.addOrder(1000, 100, Side::Sell); 
        uint64_t orderId2 = ob.addOrder(1000, 100, Side::Buy);  

        const auto& trades = ob.getTrades();
        assert(trades.size() == 1);
        assert(trades[0].price == 1000);
        assert(trades[0].quantity == 100);
        assert(trades[0].buyerOrderId == orderId2);
        assert(trades[0].sellerOrderId == orderId1);
        assert(ob.isAskEmpty() && ob.isBidEmpty());
        std::cout << "[PASS] Basic Full Match\n";
    }

    // TEST 2: Aggressor Partial Fill
    // Scenario: Seller has 50. Buyer wants 100. Buyer should get 50, rest sits on book.
    {
        OrderEngine ob;
        ob.addOrder(1000, 50, Side::Sell);
        ob.addOrder(1000, 100, Side::Buy);

        const auto& trades = ob.getTrades();
        assert(trades.size() == 1);
        assert(trades[0].quantity == 50);
        
        // Check Bids: Should have 50 remaining @ 1000
        auto bestBid = ob.getBestBid();
        assert(bestBid.first == 1000);
        assert(bestBid.second == 50);
        assert(ob.isAskEmpty());
        std::cout << "[PASS] Aggressor Partial Fill\n";
    }

    // TEST 3: Resting Order Partial Fill
    // Scenario: Seller has 100. Buyer wants 20. Seller remains with 80.
    {
        OrderEngine ob;
        ob.addOrder(1000, 100, Side::Sell);
        ob.addOrder(1000, 20, Side::Buy);

        assert(ob.getTrades().size() == 1);
        
        // Check Asks: Should have 80 remaining @ 1000
        auto bestAsk = ob.getBestAsk();
        assert(bestAsk.first == 1000);
        assert(bestAsk.second == 80);
        std::cout << "[PASS] Resting Order Partial Fill\n";
    }

    // TEST 4: Price Improvement (Maker-Taker Pricing)
    // Scenario: Seller willing to sell at 900. Buyer willing to pay 1000.
    // Result: Trade MUST happen at 900 (the resting price).
    {
        OrderEngine ob;
        ob.addOrder(900, 100, Side::Sell); // Cheapest seller
        ob.addOrder(1000, 100, Side::Buy); // Aggressive buyer

        const auto& trades = ob.getTrades();
        assert(trades.size() == 1);
        assert(trades[0].price == 900); // Crucial check!
        std::cout << "[PASS] Price Improvement (Trade at Resting Price)\n";
    }

    // TEST 5: Time Priority (FIFO)
    // Scenario: Seller A @ 1000, Seller B @ 1000. Buyer buys. A must be filled first.
    {
        OrderEngine ob;
        uint64_t orderId1 = ob.addOrder(1000, 50, Side::Sell); // Arrived first
        uint64_t orderId2 = ob.addOrder(1000, 50, Side::Sell); // Arrived second
        
        uint64_t orderId3 = ob.addOrder(1000, 70, Side::Buy); // Sweeps first, hits second

        const auto& trades = ob.getTrades();
        assert(trades.size() == 2);
        
        // First trade: filled against Order 1
        assert(trades[0].sellerOrderId == orderId1);
        assert(trades[0].quantity == 50);

        // Second trade: filled against Order 2
        assert(trades[1].sellerOrderId == orderId2);
        assert(trades[1].quantity == 20); // 70 - 50 = 20 left
        std::cout << "[PASS] Time Priority (FIFO)\n";
    }

    // TEST 6: Walking the Book (Multi-Level Match)
    // Scenario: Buyer sweeps through multiple price levels.
    {
        OrderEngine ob;
        ob.addOrder(100, 10, Side::Sell); // Level 1
        ob.addOrder(101, 10, Side::Sell); // Level 2
        ob.addOrder(102, 10, Side::Sell); // Level 3

        // Buyer is willing to pay up to 105 for 25 units
        ob.addOrder(105, 25, Side::Buy); 

        const auto& trades = ob.getTrades();
        assert(trades.size() == 3);

        // Trade 1: Best price
        assert(trades[0].price == 100 && trades[0].quantity == 10);
        // Trade 2: Next best
        assert(trades[1].price == 101 && trades[1].quantity == 10);
        // Trade 3: Worst price (partial fill)
        assert(trades[2].price == 102 && trades[2].quantity == 5);

        // Verify Book State
        assert(ob.isAskEmpty() == false);
        assert(ob.getBestAsk().second == 5); // 5 left at 102
        std::cout << "[PASS] Walking the Book\n";
    }

    // TEST 7: Order Cancellation
    // Scenario: Cancel an order, then try to match against it.
    {
        OrderEngine ob;
        uint64_t orderId1 = ob.addOrder(1000, 100, Side::Sell);
        uint64_t orderId2 = ob.addOrder(1000, 100, Side::Sell);
        uint64_t orderId3 = ob.addOrder(1000, 100, Side::Sell);


        ob.cancelOrder(orderId2);
        // Order should exhaust orderId1 and half of orderId3
        ob.addOrder(1000, 150, Side::Buy);

        assert(ob.getTrades().size() == 2);
        assert(ob.getTrades()[0].sellerOrderId == orderId1);
        assert(ob.getTrades()[1].sellerOrderId == orderId3);
        assert(ob.getTrades()[0].quantity == 100);
        assert(ob.getTrades()[1].quantity == 50);
        assert(!ob.isAskEmpty());
        assert(ob.isBidEmpty());
        
        std::cout << "[PASS] Order Cancellation\n";
    }

    // TEST 8: Order price change
    // Scenario: Changing price loses priority
    {
        OrderEngine ob;

        // 1. SETUP: Create the "Subject" (Order 1)
        // Created at T=0. Currently sitting at Price 10.
        uint64_t orderId1 = ob.addOrder(10, 100, Side::Buy);

        // 2. SETUP: Create the "Control Group" at the target price (Price 11)
        // Created at T=1 and T=2.
        uint64_t orderId2 = ob.addOrder(11, 100, Side::Buy);
        uint64_t orderId3 = ob.addOrder(11, 100, Side::Buy);

        std::cout << "[Test] Setup complete. Modifying Order 1 price to 11..." << std::endl;

        // 3. ACTION: Modify Order 1 to Price 11
        // Modifying price moves order to the back of the queue at the current price level
        ob.updateOrder(orderId1, 11, 100); 

        // 4. ASSERTION: Check Priority
        // The queue at 11 should now be: [Order 2, Order 3, Order 1]
        // Order 1 must be LAST, despite having ID 1 and being created first.
        
        ob.addOrder(11, 200, Side::Sell);
        assert(ob.getTrades().size() == 2);
        assert(ob.getTrades()[0].buyerOrderId == orderId2);
        assert(ob.getTrades()[1].buyerOrderId == orderId3);

        std::cout << "[Test] Passed: Order 1 moved to the back of the queue." << std::endl;
    }

    // TEST 8: Order Modification (Priority Check)
    // Scenario: If I increase the size of an order, does it lose its time priority?
    // (Standard logic: Reducing size keeps priority, Increasing size moves to back of queue)
    {
        OrderEngine ob;
        uint64_t orderId1 = ob.addOrder(1000, 10, Side::Sell); // First
        uint64_t orderId2 = ob.addOrder(1000, 10, Side::Sell); // Second
        
        // Modify Order 1: Increase size to 20 (Should lose priority)
        ob.updateOrder(orderId1, 1000, 20); 

        // Buyer takes 5
        uint64_t orderId3 = ob.addOrder(1000, 5, Side::Buy);

        const auto& trades = ob.getTrades();
        // Should match against Order 2 now, because Order 1 lost priority
        assert(trades[0].sellerOrderId == orderId2); 

        // Decreasing quantity retains priority
        ob.updateOrder(orderId2, 1000, 2);

        // Buyer takes 1
        uint64_t orderId4 = ob.addOrder(1000, 1, Side::Buy);

        // Should match against Order 2
        assert(trades[1].sellerOrderId == orderId2);

        std::cout << "[PASS] Modify Order (Priority Loss)\n";
    }

    std::cout << "-----------------------\nAll Tests Passed Successfully.\n";
}

int main() {
    run_tests();
    return 0;
}