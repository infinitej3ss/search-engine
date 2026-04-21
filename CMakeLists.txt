# cmake boilerplate
cmake_minimum_required(VERSION 3.20)

project(SearchEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "-O3")

# https://github.com/catchorg/Catch2/blob/devel/docs/cmake-integration.md
Include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

find_package(OpenSSL COMPONENTS SSL)

# ---- libraries -----------------------------------------------------------

# index core: index, isr, constraint solver, page data
add_library(index_core STATIC
  src/index/index.cpp
  src/index/index_builder.cpp
  src/index/isr.cpp
  src/index/Common.cpp
  src/index/page_data.cpp
)
target_include_directories(index_core PUBLIC
  ${CMAKE_SOURCE_DIR}/src/index
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}
)

# ---- executables ---------------------------------------------------------

# standalone query engine (cli demo)
add_executable(engine src/main.cpp)
target_link_libraries(engine PRIVATE index_core)

# index builder: reads crawler page files, writes per-rank blobs
add_executable(build_index src/build_index.cpp)
target_link_libraries(build_index PRIVATE index_core)

# search server: linuxtinyserver + search plugin
add_executable(search_server
  server/LinuxTinyServer.cpp
  server/SearchPlugin.cpp
)
target_include_directories(search_server PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}
)
target_link_libraries(search_server PRIVATE index_core)

# shard server: one runs per shard machine. configured via SHARD_DATA_DIR env var
add_executable(shard_server
  server/LinuxTinyServer.cpp
  server/ShardPlugin.cpp
)
target_include_directories(shard_server PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}
)
target_link_libraries(shard_server PRIVATE index_core)

# crawler
# add_executable(crawler src/crawler/crawler.cpp)
# target_link_libraries(crawler PRIVATE index_core OpenSSL::SSL)
# target_include_directories(crawler PRIVATE
#   ${CMAKE_SOURCE_DIR}/src
#   ${CMAKE_SOURCE_DIR}/src/crawler
#   ${CMAKE_SOURCE_DIR}/src/ranker
# )

# majestic million parser
add_executable(majestic src/majestic_million_parser.cpp)
target_link_libraries(majestic PRIVATE index_core OpenSSL::SSL)
target_include_directories(majestic PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/crawler
  ${CMAKE_SOURCE_DIR}/src/ranker
)

# ---- tests ---------------------------------------------------------------

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(tests ${TEST_SOURCES})
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain index_core)
target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

enable_testing()
include(CTest)
include(Catch)
catch_discover_tests(tests)
