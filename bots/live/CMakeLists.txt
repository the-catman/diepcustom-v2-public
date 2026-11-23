cmake_minimum_required(VERSION 3.16)
project(bots_live CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

set(SERVER ${CMAKE_CURRENT_SOURCE_DIR}/../../server)
set(ENV_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../env/src)
set(LIVE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# gamecore, usockets and host.
add_subdirectory(${SERVER} server EXCLUDE_FROM_ALL)

# The env's games without Vec, OpenMP or pybind, and the brain's end of the socket.
add_library(livecore STATIC
    ${ENV_SRC}/Duel.cpp
    ${ENV_SRC}/DuelShapes.cpp
    ${ENV_SRC}/Ballistics.cpp
    ${ENV_SRC}/Recorder.cpp
    ${LIVE_SRC}/Match.cpp
    ${LIVE_SRC}/Brain.cpp)
target_include_directories(livecore PUBLIC ${ENV_SRC} ${LIVE_SRC})
target_link_libraries(livecore PUBLIC gamecore pthread)

add_executable(bots-server ${LIVE_SRC}/main.cpp ${LIVE_SRC}/Rooms.cpp)
target_link_libraries(bots-server PRIVATE livecore host)

add_executable(exam ${CMAKE_CURRENT_SOURCE_DIR}/tests/exam.cpp)
target_link_libraries(exam PRIVATE livecore)

# The rooms against a made-up brain, with the game built again under the sanitizers.
set(SANITIZE -fsanitize=address,undefined -fno-omit-frame-pointer)
file(GLOB_RECURSE GAME_SRC CONFIGURE_DEPENDS ${SERVER}/src/*.cpp)
list(REMOVE_ITEM GAME_SRC ${SERVER}/src/main.cpp)
add_library(game-asan STATIC ${GAME_SRC})
target_include_directories(game-asan PUBLIC ${SERVER}/src ${SERVER}/third_party)
target_compile_options(game-asan PUBLIC ${SANITIZE})
target_link_options(game-asan PUBLIC ${SANITIZE})

add_executable(rooms-test ${CMAKE_CURRENT_SOURCE_DIR}/tests/rooms_test.cpp ${LIVE_SRC}/Rooms.cpp)
target_include_directories(rooms-test PRIVATE ${LIVE_SRC})
target_compile_definitions(rooms-test PRIVATE SERVER_DIR="${SERVER}")
target_link_libraries(rooms-test PRIVATE game-asan pthread)
