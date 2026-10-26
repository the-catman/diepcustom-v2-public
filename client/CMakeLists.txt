cmake_minimum_required(VERSION 3.16)
project(diep_client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

file(GLOB_RECURSE CLIENT_SRC ${CMAKE_SOURCE_DIR}/src/*.cpp)

add_executable(client ${CLIENT_SRC})
target_include_directories(client PRIVATE ${CMAKE_SOURCE_DIR}/src)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist)
set_target_properties(client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist)

target_link_options(client PRIVATE
    -lwebsocket.js
    -sALLOW_MEMORY_GROWTH=1
    -sMAX_WEBGL_VERSION=2
    -sENVIRONMENT=web
    -sEXIT_RUNTIME=0)

configure_file(${CMAKE_SOURCE_DIR}/web/index.html ${CMAKE_SOURCE_DIR}/dist/index.html COPYONLY)

add_custom_target(web_assets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/dist/favicon
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/web/favicon/favicon-16x16.png
        ${CMAKE_SOURCE_DIR}/web/favicon/favicon-32x32.png
        ${CMAKE_SOURCE_DIR}/web/favicon/favicon-96x96.png
        ${CMAKE_SOURCE_DIR}/dist/favicon
)
add_dependencies(client web_assets)
