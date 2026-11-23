cmake_minimum_required(VERSION 3.16)
project(diepenv CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

set(SERVER ${CMAKE_CURRENT_SOURCE_DIR}/../../server)

# The game itself, minus the websocket entrypoint.
file(GLOB_RECURSE GAME_SRC CONFIGURE_DEPENDS ${SERVER}/src/*.cpp)
list(REMOVE_ITEM GAME_SRC ${SERVER}/src/main.cpp)
add_library(game STATIC ${GAME_SRC})
target_include_directories(game PUBLIC ${SERVER}/src ${SERVER}/third_party)

find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG REQUIRED)
find_package(OpenMP REQUIRED)

file(GLOB ENV_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
pybind11_add_module(diepenv ${ENV_SRC})
target_link_libraries(diepenv PRIVATE game OpenMP::OpenMP_CXX)
