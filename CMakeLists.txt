cmake_minimum_required(VERSION 3.10)
project(aether_binance_depth)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra")

find_package(Boost REQUIRED COMPONENTS system thread)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)

include_directories(${Boost_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(aether_binance_depth
  src/event_queue.cpp
  src/orderbook.cpp
  src/rest_client.cpp
  src/ws_client.cpp
  src/ring_mmap.cpp
  src/main.cpp
)

target_link_libraries(aether_binance_depth
  PRIVATE
  ${Boost_LIBRARIES}
  OpenSSL::SSL
  OpenSSL::Crypto
  nlohmann_json::nlohmann_json
)
