#pragma once
// orderbook.h
// Simple L2 order book with apply logic per Binance diff rules

#include <map>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace aether {

  using PriceT = int64_t;
  using SizeT  = int64_t;

  // comparator: bids descending, asks ascending
  using BidsMap = std::map<PriceT, SizeT, std::greater<PriceT>>;
  using AsksMap = std::map<PriceT, SizeT, std::less<PriceT>>;

  class OrderBook {
    public:
      OrderBook();
      ~OrderBook();

      // Build from REST snapshot JSON (throws on bad format)
      void setFromSnapshot(const nlohmann::json &snapshot);

      // Apply a single depthUpdate event. Returns:
      //  - true  => event applied (or ignored if older than current)
      //  - false => gap detected (caller should resync)
      bool applyEvent(const nlohmann::json &depthUpdate);

      // Accessors
      uint64_t lastUpdateId() const noexcept;
      size_t totalLevels() const noexcept;

      // Convenience
      bool bestBid(PriceT &price_out, SizeT &size_out) const;
      bool bestAsk(PriceT &price_out, SizeT &size_out) const;

      // debug
      void printTop(int n = 10) const;

    private:
      BidsMap bids_;
      AsksMap asks_;
      uint64_t last_update_id_;

      static PriceT parsePriceScaled(const std::string &ps);
      static SizeT  parseSizeScaled(const std::string &qs);

      // non-copyable
      OrderBook(const OrderBook&) = delete;
      OrderBook& operator=(const OrderBook&) = delete;
  };

} // namespace aether
