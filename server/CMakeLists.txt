cmake_minimum_required(VERSION 3.16)
project(diep_server CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(UWS ${TP}/uWebSockets)
set(USOCKETS ${UWS}/uSockets)

# uSockets, C library. No SSL, epoll event loop (Linux default).
file(GLOB USOCKETS_C ${USOCKETS}/src/*.c ${USOCKETS}/src/eventing/*.c)
file(GLOB USOCKETS_CPP ${USOCKETS}/src/crypto/sni_tree.cpp)
add_library(usockets STATIC ${USOCKETS_C} ${USOCKETS_CPP})
target_include_directories(usockets PUBLIC ${USOCKETS}/src)
target_compile_definitions(usockets PUBLIC LIBUS_NO_SSL=1)

# Game core (everything except the uWS entrypoint).
file(GLOB_RECURSE SERVER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(REMOVE_ITEM SERVER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
add_library(gamecore STATIC ${SERVER_SRC})
target_include_directories(gamecore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${UWS}/src
    ${USOCKETS}/src
    ${TP})

# The websocket host: modes, the API, the client's files and the tick timer.
add_library(host STATIC ${CMAKE_CURRENT_SOURCE_DIR}/host/Host.cpp)
target_include_directories(host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/host)
target_link_libraries(host PUBLIC gamecore usockets z pthread)

add_executable(server ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
target_link_libraries(server PRIVATE host)
