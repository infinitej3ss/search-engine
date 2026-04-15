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
  GIT_TAG        v3.8.1 # or a later release
)

FetchContent_MakeAvailable(Catch2)
find_package(OpenSSL COMPONENTS SSL)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
file(GLOB PROJECT_SOURCES CONFIGURE_DEPENDS src/*.cpp src/crawler/*.cpp src/crawler/robots.txt/*.cpp)
list(REMOVE_ITEM PROJECT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

list(REMOVE_ITEM PROJECT_SOURCES 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/crawler/crawler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/crawler/page_file_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/crawler/robots.txt/Robots.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/majestic_million_parser.cpp
)

add_executable(tests ${TEST_SOURCES} ${PROJECT_SOURCES})
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)
target_link_libraries(tests PRIVATE OpenSSL::SSL)
target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/crawler ${CMAKE_SOURCE_DIR}/src/ranker)
enable_testing()

include(CTest)
include(Catch)
catch_discover_tests(tests)

add_executable(engine src/main.cpp ${PROJECT_SOURCES})
target_link_libraries(engine PRIVATE OpenSSL::SSL)
target_include_directories(engine PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/crawler ${CMAKE_SOURCE_DIR}/src/ranker)

add_executable(crawler src/crawler/crawler.cpp ${PROJECT_SOURCES})
target_link_libraries(crawler PRIVATE OpenSSL::SSL)
target_include_directories(crawler PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/crawler ${CMAKE_SOURCE_DIR}/src/ranker)

add_executable(majestic src/majestic_million_parser.cpp ${PROJECT_SOURCES})
target_link_libraries(majestic PRIVATE OpenSSL::SSL)
target_include_directories(majestic PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/crawler ${CMAKE_SOURCE_DIR}/src/ranker)
