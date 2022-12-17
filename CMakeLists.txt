cmake_minimum_required(VERSION 3.10)

project(tools VERSION 1.0.0 LANGUAGES CXX)

if (POLICY CMP0074)
	cmake_policy(SET CMP0074 NEW)
endif ()


if(WIN32 AND NOT MINGW)
	add_compile_options("/std:c++latest")
	add_compile_options("/Zc:__cplusplus") # bugs in vs for nlohmann json
	add_definitions(-DNOMINMAX -D_CRT_SECURE_NO_WARNINGS) # cancel min max macro in vs17
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
	set(CMAKE_DEBUG_POSTFIX "_d")
endif()
if(UNIX OR MINGW)
	set( CMAKE_CXX_FLAGS "-std=c++20")
	if(CMAKE_BUILD_TYPE MATCHES "Debug")
		add_compile_options("-g")
		add_definitions(-D__debug__)
	endif()
endif()


add_executable(
    tunnel-svr
    ${PROJECT_SOURCE_DIR}/svr.cpp
)
target_link_libraries(
	tunnel-svr
	boost_program_options
)
add_executable(
    hub
    ${PROJECT_SOURCE_DIR}/hub.cpp
    ${PROJECT_SOURCE_DIR}/utils.cpp
)
target_link_libraries(
	hub
	boost_program_options
)
add_executable(
    ep
    ${PROJECT_SOURCE_DIR}/ep.cpp
    ${PROJECT_SOURCE_DIR}/utils.cpp
)
target_link_libraries(
	ep
	boost_program_options
)
add_executable(
    hub2
    ${PROJECT_SOURCE_DIR}/hub2.cpp
    ${PROJECT_SOURCE_DIR}/utils.cpp
)
target_link_libraries(
	hub2
	boost_program_options
)
add_executable(
    ep2
    ${PROJECT_SOURCE_DIR}/ep2.cpp
    ${PROJECT_SOURCE_DIR}/utils.cpp
)
target_link_libraries(
	ep2
	boost_program_options
)
add_executable(
    hub3
    ${PROJECT_SOURCE_DIR}/hub3.cpp
    ${PROJECT_SOURCE_DIR}/utils.cpp
)
target_link_libraries(
	hub3
	boost_program_options
)