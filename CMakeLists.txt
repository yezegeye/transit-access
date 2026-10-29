cmake_minimum_required(VERSION 3.18)
project(transit_access LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG REQUIRED)
add_library(router STATIC cpp/router.cpp)
target_include_directories(router PUBLIC cpp)
set_target_properties(router PROPERTIES POSITION_INDEPENDENT_CODE ON)

pybind11_add_module(transit_router cpp/bindings.cpp)
target_link_libraries(transit_router PRIVATE router)

include(FetchContent)
FetchContent_Declare(catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2)
FetchContent_MakeAvailable(catch2)
add_executable(router_tests cpp/tests/test_router.cpp)
target_link_libraries(router_tests PRIVATE router Catch2::Catch2WithMain)
target_compile_definitions(router_tests PRIVATE
  TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures")
enable_testing()
add_test(NAME router_tests COMMAND router_tests)
