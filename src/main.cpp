// main.cpp
#include "event_queue.h"
#include "utils.h"
#include "orderbook.h"
#include "rest_client.h"
#include "ws_client.h"
#include "ring_mmap.h"

#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <nlohmann/json.hpp>


using json = nlohmann::json;
using namespace aether;
using aether::ring::RingHandle;
using aether::ring::create_ring;
using aether::ring::open_ring;
using aether::ring::close_ring;
using aether::ring::publish_message;
using aether::ring::publish_snapshot_json;
using aether::ring::ring_buf_size;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " SYMBOL [100ms]\n";
    return 1;
  }
  std::string symbol = argv[1];
  std::string updateSpeed = (argc >= 3 ? argv[2] : "");
  std::string ring_path = (argc >= 4 ? argv[3] : "/dev/shm/aether.byte.ring");
  size_t ring_buf_size = 8 * 1024 * 1024; // 8MB (tune as required)

  EventQueue queue;
  std::atomic<bool> stopFlag{false};

  // Create or open ring using the C++ API
  RingHandle *ring = nullptr;
  ring = create_ring(ring_path.c_str(), ring_buf_size);
  if (!ring) {
    std::cerr << "[main] create_ring failed; trying open_ring...\n";
    ring = open_ring(ring_path.c_str());
    if (!ring) {
      std::cerr << "[main] ring_create/open failed. continuing WITHOUT publishing to ring.\n";
    } else {
      std::cerr << "[main] opened existing ring: " << ring_path << " (buf_size=" << ring_buf_size << ")\n";
    }
  } else {
    std::cerr << "[main] created ring at " << ring_path << " (buf_size=" << ring_buf_size << ")\n";
  }

  // start ws reader thread
  std::thread ws_thread = start_ws_reader(symbol, updateSpeed, queue, stopFlag);

  
  // Wait for initial buffered events per Binance spec
  uint64_t firstU = wait_for_initial_buffer(queue, /*min_events=*/5, /*timeout_ms=*/500);
  std::cerr << "[main] noted first event U = " << firstU << "\n";

  // setup io_context and ssl ctx for REST
  boost::asio::io_context ioc;
  boost::asio::ssl::context ctx{boost::asio::ssl::context::tlsv12_client};
  ctx.set_verify_mode(boost::asio::ssl::verify_none); // production: enable verify

  // binance needs the symbol target in uppercase for rest endpoints.
  auto to_upper = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
  };

  // fetch snapshot until lastUpdateId >= firstU
  json snapshot;
  std::string host = "api.binance.com";
  std::string port = "443";
  std::string target = "/api/v3/depth?symbol=" + to_upper(symbol) + "&limit=5000";
  while (true) {
    try {
      std::cerr << "[main] fetching snapshot...\n";
      std::string body = https_get_sync(ioc, ctx, host, port, target);
      snapshot = json::parse(body);
      uint64_t lastUpdateId = snapshot.at("lastUpdateId").get<uint64_t>();
      std::cerr << "[main] snapshot.lastUpdateId = " << lastUpdateId << "\n";
      if (lastUpdateId >= firstU) break;
      std::cerr << "[main] snapshot too old, retrying in 1s\n";
      std::this_thread::sleep_for(std::chrono::seconds(1));
    } catch (...) {
      std::cerr << "[main] snapshot fetch error, retrying\n";
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  // Publish snapshot to ring (if ring available)
  if (ring) {
    std::string snap_str = snapshot.dump();
    bool ok = publish_snapshot_json(ring, snap_str.c_str());
    if (!ok) {
      std::cerr << "[main] Warning: publish_snapshot_json failed. Will continue but consumer may not get snapshot.\n";
    } else {
      std::cerr << "[main] Published snapshot to ring (" << snap_str.size() << " bytes)\n";
    }
  }

  // drain buffered events and keep those after lastUpdateId
  std::vector<JsonEvent> buffered = queue.drain_all();
  std::cerr << "[main] buffered events count = " << buffered.size() << "\n";
  uint64_t lastUpdateId = snapshot.at("lastUpdateId").get<uint64_t>();
  size_t idx = 0;
  while (idx < buffered.size()) {
    uint64_t u = buffered[idx].j.at("u").get<uint64_t>();
    if (u <= lastUpdateId) ++idx;
    else break;
  }
  std::vector<JsonEvent> to_apply;
  for (size_t i = idx; i < buffered.size(); ++i) to_apply.push_back(std::move(buffered[i]));
  std::cerr << "[main] to_apply size after discard = " << to_apply.size() << "\n";

  if (!to_apply.empty()) {
    uint64_t firstBufU = to_apply.front().j.at("U").get<uint64_t>();
    uint64_t firstBufu = to_apply.front().j.at("u").get<uint64_t>();
    if (!(firstBufU <= lastUpdateId + 1 && lastUpdateId + 1 <= firstBufu)) {
      std::cerr << "[main] buffered event range does not cover snapshot+1. Exiting.\n";
      stopFlag.store(true);
      if (ws_thread.joinable()) ws_thread.join();
      if (ring) close_ring(ring);
      return 2;
    }
  } else {
    std::cerr << "[main] no buffered events after discarding old ones. Proceeding with snapshot only.\n";
  }

  // build local book from snapshot
  OrderBook book;
  book.setFromSnapshot(snapshot);
  std::cerr << "[main] built local book lastUpdateId=" << book.lastUpdateId() << " levels=" << book.totalLevels() << "\n";
  book.printTop(5);

  // helper: publish JSON payload with small retry
  auto publish_json_to_ring = [&](RingHandle *r, uint8_t msg_type, const std::string &s) -> bool {
    if (!r) return false;
    const void *data = s.data();
    size_t len = s.size();
    const int MAX_TRIES = 3;
    for (int t=0;t<MAX_TRIES;++t) {
      bool ok = publish_message(r, msg_type, data, len);
      if (ok) return true;
      // simple backoff
      std::this_thread::sleep_for(std::chrono::milliseconds(10 * (t+1)));
    }
    return false;
  };

  // apply buffered events sequentially
  size_t applied = 0;
  for (auto &ev : to_apply) {
    bool ok = book.applyEvent(ev.j);
    if (!ok) {
      std::cerr << "[main] gap detected while applying buffered events. Need to resync. Exiting.\n";
      stopFlag.store(true);
      if (ws_thread.joinable()) ws_thread.join();
      if (ring) close_ring(ring);
      return 3;
    }

    // publish buffered event to ring as DEPTH_UPDATE (type=1)
    if (ring) {
      std::string evs = ev.j.dump();
      if (!publish_json_to_ring(ring, 1, evs)) {
        std::cerr << "[main] Warning: failed to publish buffered event to ring after retries\n";
      }
    }

    ++applied;
  }
  std::cerr << "[main] applied " << applied << " buffered events. book_update_id now = " << book.lastUpdateId() << "\n";
  book.printTop(5);

  // live processing
  std::cerr << "[main] entering live processing loop. Ctrl+C to exit.\n";
  size_t liveCounter = 0;
  while (true) {
    JsonEvent ev = queue.pop_blocking();
    uint64_t U = ev.j.at("U").get<uint64_t>();
    uint64_t u = ev.j.at("u").get<uint64_t>();
    std::cerr << "[ws] incoming U=" << U << " u=" << u << " book=" << book.lastUpdateId() << "\n";
    if (u < book.lastUpdateId()) continue;
    if (U > book.lastUpdateId() + 1) {
      std::cerr << "[main] SEQ GAP DETECTED. Need resync. Exiting.\n";
      stopFlag.store(true);
      break;
    }
    bool ok = book.applyEvent(ev.j);
    if (!ok) {
      std::cerr << "[main] applyEvent returned false (gap). Exiting.\n";
      stopFlag.store(true);
      break;
    }

    // publish live depthUpdate to ring (type=1)
    if (ring) {
      std::string evs = ev.j.dump();
      if (!publish_json_to_ring(ring, 1, evs)) {
        std::cerr << "[main] Warning: failed to publish live event to ring after retries\n";
      }
    }

    book.printTop(5);
    ++liveCounter;
    if (liveCounter % 10000 == 0) {
      std::cerr << "[main] applied " << liveCounter << " live events. book_update_id=" << book.lastUpdateId() << " levels=" << book.totalLevels() << "\n";
    }
    if (liveCounter % 1000 == 0) book.printTop(5);
  }

  stopFlag.store(true);
  if (ws_thread.joinable()) ws_thread.join();
  if (ring) {
    close_ring(ring);
    std::cerr << "[main] closed ring\n";
  }
  std::cerr << "[main] exiting.\n";
  return 0;
}
