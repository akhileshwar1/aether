#pragma once
// ws_client.h
// websocket reader that pushes depthUpdate events into EventQueue

#include <string>
#include <atomic>
#include <thread>
#include "event_queue.h"

// starts a thread that runs the WS reader; returns std::thread (moveable)
std::thread start_ws_reader(const std::string &symbol,
    const std::string &updateSpeed,
    EventQueue &queue,
    std::atomic<bool> &stopFlag);
