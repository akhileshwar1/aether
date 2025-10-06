// rest_client.cpp
#include "rest_client.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <openssl/ssl.h>
#include <iostream>

namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

std::string https_get_sync(net::io_context &ioc,
    boost::asio::ssl::context &ctx,
    const std::string &host,
    const std::string &port,
    const std::string &target) {
  try {
    tcp::resolver resolver{ioc};

    // Use beast::tcp_stream wrapped in ssl_stream for correct layering on some toolchains
    beast::tcp_stream tcp_stream{ioc};
    auto const results = resolver.resolve(host, port);

    // Connect TCP socket (tries all endpoints)
    std::cerr << "[rest_client] connecting TCP to " << host << ":" << port << " ...\n";
    boost::asio::connect(tcp_stream.socket(), results);
    std::cerr << "[rest_client] TCP connected\n";

    // Now upgrade to SSL on top of the connected tcp socket
    beast::ssl_stream<beast::tcp_stream> stream{std::move(tcp_stream), ctx};

    // IMPORTANT: set SNI (Server Name Indication) before TLS handshake
    if (stream.native_handle() != nullptr) {
      if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
        std::cerr << "[rest_client] warning: SSL_set_tlsext_host_name failed\n";
      } else {
        std::cerr << "[rest_client] SNI set to " << host << "\n";
      }
    } else {
      std::cerr << "[rest_client] warning: native_handle() returned null\n";
    }

    // Perform TLS handshake
    std::cerr << "[rest_client] performing TLS handshake...\n";
    stream.handshake(boost::asio::ssl::stream_base::client);
    std::cerr << "[rest_client] TLS handshake succeeded\n";

    // Prepare HTTP request
    http::request<http::string_body> req{http::verb::get, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "aether-binance");

    // Send the HTTP request over SSL
    std::cerr << "[rest_client] sending HTTP request " << target << "\n";
    http::write(stream, req);

    // Receive HTTP response
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    std::cerr << "[rest_client] waiting to read response...\n";
    http::read(stream, buffer, res);
    std::cerr << "[rest_client] HTTP response read (status " << res.result_int() << ")\n";

    // Shutdown TLS (ignore EOF as non-fatal)
    beast::error_code ec;
    stream.shutdown(ec);
    if (ec == boost::asio::error::eof) {
      // Normal EOF on some servers — treat as success
      ec.assign(0, ec.category());
      std::cerr << "[rest_client] shutdown EOF ignored\n";
    }
    if (ec) {
      std::cerr << "[rest_client] shutdown error: " << ec.message() << " (non-fatal)\n";
    } else {
      std::cerr << "[rest_client] TLS shutdown succeeded\n";
    }

    return res.body();
  } catch (const std::exception &ex) {
    std::cerr << "[rest_client] exception: " << ex.what() << "\n";
    return std::string(); // signal failure to caller (caller should retry/backoff)
  }
}
