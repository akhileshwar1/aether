// orderbook.cpp
#include "orderbook.h"
#include <cmath>
#include <iostream>

namespace aether {

  static constexpr int64_t PRICE_SCALE = 100000000LL; // or set per-symbol

  OrderBook::OrderBook() : bids_(), asks_(), last_update_id_(0) {}
  OrderBook::~OrderBook() = default;

  auto OrderBook::parsePriceScaled(const std::string &ps) -> PriceT {
    double d = std::stod(ps);
    return static_cast<PriceT>(llround(d * PRICE_SCALE));
  }
  auto OrderBook::parseSizeScaled(const std::string &qs) -> SizeT {
    double d = std::stod(qs);
    return static_cast<SizeT>(llround(d * PRICE_SCALE));
  }

  void OrderBook::setFromSnapshot(const nlohmann::json &snapshot) {
    bids_.clear();
    asks_.clear();
    const auto lastId = snapshot.at("lastUpdateId").get<uint64_t>();
    last_update_id_ = lastId;

    for (const auto &b : snapshot.at("bids")) {
      PriceT p = parsePriceScaled(b.at(0).get<std::string>());
      SizeT  q = parseSizeScaled(b.at(1).get<std::string>());
      if (q > 0) bids_[p] = q;
    }
    for (const auto &a : snapshot.at("asks")) {
      PriceT p = parsePriceScaled(a.at(0).get<std::string>());
      SizeT  q = parseSizeScaled(a.at(1).get<std::string>());
      if (q > 0) asks_[p] = q;
    }
  }

  bool OrderBook::applyEvent(const nlohmann::json &event) {
    uint64_t U = event.at("U").get<uint64_t>();
    uint64_t u = event.at("u").get<uint64_t>();
    if (u < last_update_id_) return true;            // old, ignore
    if (U > last_update_id_ + 1) return false;       // gap -> resync needed

    if (event.contains("b")) {
      for (const auto &lvl : event.at("b")) {
        PriceT p = parsePriceScaled(lvl.at(0).get<std::string>());
        SizeT  q = parseSizeScaled(lvl.at(1).get<std::string>());
        if (q == 0) bids_.erase(p);
        else bids_[p] = q;
      }
    }
    if (event.contains("a")) {
      for (const auto &lvl : event.at("a")) {
        PriceT p = parsePriceScaled(lvl.at(0).get<std::string>());
        SizeT  q = parseSizeScaled(lvl.at(1).get<std::string>());
        if (q == 0) asks_.erase(p);
        else asks_[p] = q;
      }
    }
    last_update_id_ = u;
    return true;
  }

  uint64_t OrderBook::lastUpdateId() const noexcept { return last_update_id_; }
  size_t OrderBook::totalLevels() const noexcept { return bids_.size() + asks_.size(); }

  bool OrderBook::bestBid(PriceT &price_out, SizeT &size_out) const {
    if (bids_.empty()) return false;
    auto it = bids_.begin();
    price_out = it->first; size_out = it->second; return true;
  }
  bool OrderBook::bestAsk(PriceT &price_out, SizeT &size_out) const {
    if (asks_.empty()) return false;
    auto it = asks_.begin();
    price_out = it->first; size_out = it->second; return true;
  }

  void OrderBook::printTop(int n) const {
    std::cout << "OrderBook last_update_id=" << last_update_id_ << "\n";
    std::cout << " Asks (lowest):\n";
    int i=0;
    for (auto it = asks_.begin(); it != asks_.end() && i<n; ++it, ++i) {
      std::cout << "  " << double(it->first)/PRICE_SCALE << " : " << double(it->second)/PRICE_SCALE << "\n";
    }
    std::cout << " Bids (highest):\n";
    i=0;
    for (auto it = bids_.begin(); it != bids_.end() && i<n; ++it, ++i) {
      std::cout << "  " << double(it->first)/PRICE_SCALE << " : " << double(it->second)/PRICE_SCALE << "\n";
    }
  }

} // namespace aether
