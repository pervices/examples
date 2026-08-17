cmake_minimum_required(VERSION 3.16)
project(minimal_uhdless_demo CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(send_main
    send_main.cpp
    src/buffer_tracker.cpp
    src/clock_sync.cpp
    src/metadata.cpp
    src/pv_iface.cpp
    src/super_send_packet_handler_mmsg.cpp
    src/system_time.cpp
    src/tcp_simple.cpp
    src/thread.cpp
    src/time_spec.cpp
    src/udp_simple.cpp
    src/vrt_if_packet.cpp
)

target_compile_features(send_main PRIVATE cxx_std_20)

target_include_directories(send_main PRIVATE include)

target_compile_options(send_main PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wcast-align
    -Woverloaded-virtual
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
)

target_link_libraries(send_main PRIVATE
    Threads::Threads
)
