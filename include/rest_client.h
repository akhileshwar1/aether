#pragma once
// rest_client.h
// simple synchronous HTTPS GET using Boost.Beast + OpenSSL

#include <string>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

std::string https_get_sync(boost::asio::io_context &ioc,
    boost::asio::ssl::context &ctx,
    const std::string &host,
    const std::string &port,
    const std::string &target);
