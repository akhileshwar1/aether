// ws_client.cpp
#include "ws_client.h"
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <nlohmann/json.hpp>
#include <iostream>

namespace beast = boost::beast;
namespace websocket = boost::beast::websocket;
namespace ssl = boost::asio::ssl;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

static uint64_t mono_now_us() {
  using namespace std::chrono;
  return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::thread start_ws_reader(const std::string &symbol,
    const std::string &updateSpeed,
    EventQueue &queue,
    std::atomic<bool> &stopFlag) {
  return std::thread([symbol, updateSpeed, &queue, &stopFlag] {
      try {
      net::io_context ioc;
      ssl::context ctx{ssl::context::tlsv12_client};
      ctx.set_verify_mode(ssl::verify_none); // production: enable verify

      tcp::resolver resolver{ioc};
      websocket::stream<beast::ssl_stream<tcp::socket>> ws{ioc, ctx};

      std::string host = "stream.binance.com";
      std::string port = "9443";
      std::string path = "/ws/" + symbol + "@depth";
      if (!updateSpeed.empty() && updateSpeed == "100ms")
      path = "/ws/" + symbol + "@depth@100ms";

      auto const results = resolver.resolve(host, port);
      boost::asio::connect(ws.next_layer().next_layer(), results);
      ws.next_layer().handshake(ssl::stream_base::client);
      ws.handshake(host, path);

      beast::flat_buffer buffer;
      size_t counter = 0;

      while (!stopFlag.load()) {
        buffer.clear();
        beast::error_code ec;
        ws.read(buffer, ec);
        if (ec) {
          std::cerr << "[ws_reader] read error: " << ec.message() << "\n";
          break;
        }
        std::string msg = beast::buffers_to_string(buffer.data());
        uint64_t now_us = mono_now_us();
        try {
          json j = json::parse(msg);
          if (j.contains("e") && j["e"] == "depthUpdate") {
            queue.push(JsonEvent{std::move(j), now_us});
            if (++counter % 10000 == 0) {
              std::cerr << "[ws_reader] received " << counter << " depth events\n";
            }
          }
        } catch (const std::exception &ex) {
          std::cerr << "[ws_reader] JSON parse error: " << ex.what() << "\n";
        }
      }
      beast::error_code ec2;
      ws.close(websocket::close_code::normal, ec2);
      } catch (const std::exception &ex) {
        std::cerr << "[ws_reader] exception: " << ex.what() << "\n";
      }
  });
}
