cmake_minimum_required(VERSION 3.20)
project(MixAgent VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies via FetchContent
include(FetchContent)

FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v5.0.0
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
)

FetchContent_MakeAvailable(ftxui nlohmann_json httplib spdlog)

# Find system packages
find_package(Threads REQUIRED)

# Source files
file(GLOB_RECURSE SOURCES src/*.cpp)

add_executable(mixagent ${SOURCES})

target_include_directories(mixagent PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${httplib_SOURCE_DIR}
)

target_link_libraries(mixagent PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    Threads::Threads
)

# Tests
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    file(GLOB_RECURSE TEST_SOURCES tests/*.cpp)
    # Collect all source files except main.cpp for test builds
    file(GLOB_RECURSE LIB_SOURCES src/*.cpp)
    list(FILTER LIB_SOURCES EXCLUDE REGEX "src/main\\.cpp$")

    add_executable(mixagent_tests ${TEST_SOURCES} ${LIB_SOURCES})
    target_include_directories(mixagent_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${httplib_SOURCE_DIR}
    )
    target_link_libraries(mixagent_tests PRIVATE
        GTest::gtest_main
        ftxui::screen
        ftxui::dom
        ftxui::component
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        Threads::Threads
    )
    include(GoogleTest)
    gtest_discover_tests(mixagent_tests)
endif()
