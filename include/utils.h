#pragma once
// utils.h - small inline helpers

#include <iostream>
#include <chrono>
#include <thread>
#include "event_queue.h"

// Waits until EventQueue has some buffered depthUpdate events and returns the first U
inline uint64_t wait_for_initial_buffer(EventQueue &queue,
    int min_events = 5,
    int timeout_ms = 500)
{
  uint64_t firstU = 0;
  int waited_ms = 0;

  std::cerr << "[wait_for_initial_buffer] waiting for initial depthUpdate events...\n";

  while (true) {
    size_t sz = queue.size();

    if (sz >= static_cast<size_t>(min_events)) {
      if (queue.peek_first_U(firstU)) break;
    }

    // if at least one event is present, wait a short grace period to gather more
    if (sz > 0 && waited_ms >= 100) {
      if (queue.peek_first_U(firstU)) break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    waited_ms += 50;

    if (waited_ms >= timeout_ms) {
      // timeout — use whatever we have
      if (queue.peek_first_U(firstU)) break;
      // otherwise keep waiting until at least one event arrives
    }
  }

  std::cerr << "[wait_for_initial_buffer] got first buffered event U = "
    << firstU << " (buffered_events=" << queue.size() << ")\n";

  return firstU;
}
