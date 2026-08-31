cmake_minimum_required(VERSION 3.22)

project(ExpeditionOnline VERSION 0.2.0 LANGUAGES CXX)

option(EXPEDITION_BUILD_SERVER "Build ExpeditionOnlineServer" ON)
option(EXPEDITION_BUILD_CLIENT "Build the UE4SS native client mod" OFF)
option(EXPEDITION_BUILD_PROBE "Build the protocol probe client" ON)
option(EXPEDITION_BUILD_TESTS "Build protocol tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ExpeditionOnlineProtocol STATIC
    shared/src/protocol.cpp
    shared/src/socket.cpp
)
target_include_directories(ExpeditionOnlineProtocol PUBLIC shared/include)
target_compile_definitions(ExpeditionOnlineProtocol PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
if(WIN32)
    target_link_libraries(ExpeditionOnlineProtocol PUBLIC ws2_32)
endif()

if(MSVC)
    target_compile_options(ExpeditionOnlineProtocol PRIVATE /W4 /permissive-)
else()
    target_compile_options(ExpeditionOnlineProtocol PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(EXPEDITION_BUILD_SERVER)
    add_subdirectory(server)
endif()

if(EXPEDITION_BUILD_PROBE)
    add_subdirectory(tools/probe)
endif()

if(EXPEDITION_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(EXPEDITION_BUILD_CLIENT)
    if(NOT DEFINED UE4SS_ROOT)
        message(FATAL_ERROR "Set UE4SS_ROOT to a source checkout matching the UE4SS build installed in the game")
    endif()
    if(NOT EXISTS "${UE4SS_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR "UE4SS_ROOT does not contain CMakeLists.txt: ${UE4SS_ROOT}")
    endif()
    add_subdirectory("${UE4SS_ROOT}" "${CMAKE_BINARY_DIR}/ue4ss")
    add_subdirectory(client)
endif()
