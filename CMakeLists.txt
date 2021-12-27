cmake_minimum_required(VERSION 3.20)
project(epmon)

if(NOT TARGET spdlog)
    # Stand-alone build
    find_package(spdlog REQUIRED)
endif()

set(CMAKE_CXX_STANDARD 11)
add_subdirectory(src)
add_executable(epmon epmon.cpp monitor_config.cpp monitor.cpp process_info.cpp process_info.h monitor.h monitor_config.h)
target_link_libraries(epmon PRIVATE spdlog::spdlog_header_only curl procps pthread)
