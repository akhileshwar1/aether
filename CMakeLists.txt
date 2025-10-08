cmake_minimum_required(VERSION 3.10)
project(aether_binance_depth LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra")

# Option: build shared lib (default ON)
option(BUILD_RING_SHARED "Build libring_mmap as shared library" ON)
option(BUILD_RING_STATIC "Build libring_mmap as static library" ON)

find_package(Boost REQUIRED COMPONENTS system thread)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)

# Include dirs for project headers
set(PROJECT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${Boost_INCLUDE_DIRS} ${PROJECT_INCLUDE_DIR})

# -- Library target(s) for ring_mmap ---------------------------------------
set(RING_SRCS src/ring_mmap.cpp)

if(BUILD_RING_SHARED)
  add_library(ring_mmap_shared SHARED ${RING_SRCS})
  target_include_directories(ring_mmap_shared PUBLIC ${PROJECT_INCLUDE_DIR})
  # ensure PIC for shared library
  set_target_properties(ring_mmap_shared PROPERTIES POSITION_INDEPENDENT_CODE ON OUTPUT_NAME "ring_mmap")
  # link libs if ring_mmap needs any (none here), you can add if needed
endif()

if(BUILD_RING_STATIC)
  add_library(ring_mmap_static STATIC ${RING_SRCS})
  target_include_directories(ring_mmap_static PUBLIC ${PROJECT_INCLUDE_DIR})
  # allow using static with PIC when linking into shared objects
  set_target_properties(ring_mmap_static PROPERTIES POSITION_INDEPENDENT_CODE ON OUTPUT_NAME "ring_mmap")
endif()

# Choose which library the executable will link to:
# prefer shared if available, else static
if(TARGET ring_mmap_shared)
  set(RING_LIB_TARGET ring_mmap_shared)
elseif(TARGET ring_mmap_static)
  set(RING_LIB_TARGET ring_mmap_static)
else()
  message(FATAL_ERROR "No ring_mmap library target available")
endif()

# -- Executable --------------------------------------------------------------
set(SRCS
  src/event_queue.cpp
  src/orderbook.cpp
  src/rest_client.cpp
  src/ws_client.cpp
  src/main.cpp
)

add_executable(aether_binance_depth ${SRCS})

# Link in ring library and other deps
target_link_libraries(aether_binance_depth
  PRIVATE
  ${RING_LIB_TARGET}
  ${Boost_LIBRARIES}
  OpenSSL::SSL
  OpenSSL::Crypto
  nlohmann_json::nlohmann_json
  pthread
)

target_include_directories(aether_binance_depth PRIVATE ${PROJECT_INCLUDE_DIR})

# -- Install rules (optional) ------------------------------------------------
install(TARGETS aether_binance_depth
  RUNTIME DESTINATION bin)

if(TARGET ring_mmap_shared)
  install(TARGETS ring_mmap_shared LIBRARY DESTINATION lib)
endif()
if(TARGET ring_mmap_static)
  install(TARGETS ring_mmap_static ARCHIVE DESTINATION lib)
endif()
install(DIRECTORY ${PROJECT_INCLUDE_DIR}/ DESTINATION include FILES_MATCHING PATTERN "*.h")

# -- Summary info ------------------------------------------------------------
message(STATUS "BUILD_RING_SHARED = ${BUILD_RING_SHARED}")
message(STATUS "BUILD_RING_STATIC = ${BUILD_RING_STATIC}")
message(STATUS "Using RING_LIB_TARGET = ${RING_LIB_TARGET}")

