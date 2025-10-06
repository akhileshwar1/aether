#pragma once
// event_queue.h
// Thread-safe queue for JsonEvent

#include <nlohmann/json.hpp>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstdint>

using json = nlohmann::json;

struct JsonEvent {
  json j;
  uint64_t local_recv_ts_us;
};

class EventQueue {
  public:
    EventQueue();
    ~EventQueue();

    // push a new event (moves in)
    void push(JsonEvent &&e);

    // blocking pop
    JsonEvent pop_blocking();

    // non-blocking size
    size_t size();

    // peek first's U (returns false if none or parse error)
    bool peek_first_U(uint64_t &outU);

    // drain all events into a vector (moves them)
    std::vector<JsonEvent> drain_all();

  private:
    std::deque<JsonEvent> dq_;
    std::mutex m_;
    std::condition_variable cv_;
};
