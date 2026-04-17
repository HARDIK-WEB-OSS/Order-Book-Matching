cmake_minimum_required(VERSION 3.16)
project(order_book_engine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  $<$<CONFIG:Release>:-O3>
  $<$<CONFIG:Release>:-march=native>
  $<$<CONFIG:Release>:-DNDEBUG>
  $<$<CONFIG:Debug>:-O0>
  $<$<CONFIG:Debug>:-g>
)

find_package(Threads REQUIRED)

add_library(engine STATIC
  engine/order_book.cpp
  engine/matching_engine.cpp
)
target_include_directories(engine PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine PUBLIC Threads::Threads)

add_executable(server api/server.cpp)
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
find_package(OpenSSL REQUIRED)
target_link_libraries(server PRIVATE engine OpenSSL::SSL OpenSSL::Crypto)

add_executable(stress_test benchmark/stress_test.cpp)
target_link_libraries(stress_test PRIVATE engine)

set_target_properties(stress_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/benchmark
)

add_executable(test_matching tests/test_matching.cpp)
target_include_directories(test_matching PRIVATE
  ${CMAKE_SOURCE_DIR}/third_party/catch2
)
target_link_libraries(test_matching PRIVATE engine)

enable_testing()
add_test(NAME matching_tests COMMAND test_matching)

install(TARGETS server stress_test test_matching
  RUNTIME DESTINATION bin
)

message(STATUS "")
message(STATUS "  order-book-engine (C++17)")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Targets    : server  stress_test  test_matching")
message(STATUS "")
