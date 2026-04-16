cmake_minimum_required(VERSION 3.16)
project(hindiurdu_backend CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.3
)
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.4
)

FetchContent_MakeAvailable(httplib json Catch2)

find_package(SQLite3 REQUIRED)

add_executable(hindiurdu_server
  src/main.cpp
  src/catalog.cpp
  src/db.cpp
  src/fuzzy.cpp
)
target_include_directories(hindiurdu_server PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${httplib_SOURCE_DIR}"
)
target_link_libraries(hindiurdu_server PRIVATE nlohmann_json::nlohmann_json SQLite::SQLite3)

add_executable(fuzzy_tests
  tests/fuzzy_tests.cpp
  src/fuzzy.cpp
)
target_include_directories(fuzzy_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(fuzzy_tests PRIVATE Catch2::Catch2WithMain)

add_executable(merge_word_catalog tools/merge_word_catalog.cpp)
target_link_libraries(merge_word_catalog PRIVATE nlohmann_json::nlohmann_json)

include(CTest)
add_test(NAME fuzzy_tests COMMAND fuzzy_tests)
