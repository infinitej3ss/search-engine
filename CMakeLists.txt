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

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

# ranker dynamic — non-header-only components (BM25 scorer, etc.)
add_library(ranker_dynamic STATIC src/ranker/dynamic/bm25.cpp)
target_include_directories(ranker_dynamic PUBLIC ${CMAKE_SOURCE_DIR}/src)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(tests ${TEST_SOURCES})
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain ranker_dynamic)
target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

enable_testing()

include(CTest)
include(Catch)
catch_discover_tests(tests)

# index core — shared between engine and build_index
add_library(index_core STATIC
  src/index.cpp
  src/isr.cpp
  src/constraint_solver.cpp
  src/Common.cpp
  src/page_data.cpp
)
target_include_directories(index_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

# query-serving binary
add_executable(engine src/main.cpp)
target_link_libraries(engine PRIVATE index_core ranker_dynamic)

# builder binary — reads crawler page files, writes per-rank index blobs
add_executable(build_index src/build_index.cpp)
target_link_libraries(build_index PRIVATE index_core)

