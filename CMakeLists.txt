# cmake boilerplate
cmake_minimum_required(VERSION 3.20)

project(SearchEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# https://github.com/catchorg/Catch2/blob/devel/docs/cmake-integration.md
Include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1 # or a later release
)

FetchContent_MakeAvailable(Catch2)

add_executable(tests tests/test.cpp)
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)

enable_testing()

include(CTest)
include(Catch)
catch_discover_tests(tests)

# main executable
add_executable(project src/main.cpp)

