cmake_minimum_required(VERSION 3.10)

project(net_tunnel VERSION 1.0.0 LANGUAGES CXX)

set( CMAKE_CXX_FLAGS "-std=c++20")
if(CMAKE_BUILD_TYPE MATCHES "Debug")
	add_compile_options("-g")
	add_definitions(-D__debug__)
endif()

## hub
add_executable(
    hub
    ${PROJECT_SOURCE_DIR}/hub.cpp
    ${PROJECT_SOURCE_DIR}/utils.cpp
)
target_link_libraries(
	hub
	boost_program_options
)

## ep
add_executable(
    ep
    ${PROJECT_SOURCE_DIR}/ep.cpp
    ${PROJECT_SOURCE_DIR}/utils.cpp
)
target_link_libraries(
	ep
	boost_program_options
)