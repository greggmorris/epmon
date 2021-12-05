cmake_minimum_required(VERSION 3.20)
project(epmon)

set(CMAKE_CXX_STANDARD 11)
add_executable(epmon epmon.cpp monitor_config.cpp monitor.cpp process_info.cpp process_info.h monitor.h monitor_config.h)
target_link_libraries(epmon curl procps pthread)
