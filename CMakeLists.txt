cmake_minimum_required(VERSION 3.20)
project(netfight CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- Third-party: raylib (window, input, drawing). Not my code: fetched as-is. ---
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

# --- Deterministic simulation: no raylib, no floats, no OS calls. ---
add_library(sim STATIC src/sim.cpp)
target_include_directories(sim PUBLIC src)

# --- Networking: UDP socket + lockstep session. Also raylib-free. ---
add_library(netcore STATIC src/net.cpp src/session.cpp)
target_link_libraries(netcore PUBLIC sim)
if(WIN32)
  target_link_libraries(netcore PUBLIC ws2_32)
endif()

# --- Game executable ---
add_executable(netfight src/main.cpp)
target_link_libraries(netfight PRIVATE netcore raylib)
if(APPLE)
  target_link_libraries(netfight PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

# --- Headless determinism test: two sims, same inputs, hashes must match ---
add_executable(sim_test tests/sim_test.cpp)
target_link_libraries(sim_test PRIVATE sim)

# --- Network test: two sessions over real UDP on localhost must stay in sync ---
add_executable(session_test tests/session_test.cpp)
target_link_libraries(session_test PRIVATE netcore)