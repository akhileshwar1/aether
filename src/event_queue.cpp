// event_queue.cpp
#include "event_queue.h"

EventQueue::EventQueue() = default;
EventQueue::~EventQueue() = default;

void EventQueue::push(JsonEvent &&e) {
  {
    std::lock_guard<std::mutex> lk(m_);
    dq_.push_back(std::move(e));
  }
  cv_.notify_one();
}

JsonEvent EventQueue::pop_blocking() {
  std::unique_lock<std::mutex> lk(m_);
  cv_.wait(lk, [&]{ return !dq_.empty(); });
  JsonEvent e = std::move(dq_.front());
  dq_.pop_front();
  return e;
}

size_t EventQueue::size() {
  std::lock_guard<std::mutex> lk(m_);
  return dq_.size();
}

bool EventQueue::peek_first_U(uint64_t &outU) {
  std::lock_guard<std::mutex> lk(m_);
  if (dq_.empty()) return false;
  try {
    outU = dq_.front().j.at("U").get<uint64_t>();
    return true;
  } catch(...) {
    return false;
  }
}

std::vector<JsonEvent> EventQueue::drain_all() {
  std::lock_guard<std::mutex> lk(m_);
  std::vector<JsonEvent> v;
  v.reserve(dq_.size());
  while (!dq_.empty()) {
    v.push_back(std::move(dq_.front()));
    dq_.pop_front();
  }
  return v;
}
