# Copyright (c) 2018 Tundra. All right reserved.
# Use of this code is governed by the terms defined in LICENSE.

cmake_minimum_required (VERSION 2.8.12)
project(capnprof)
enable_testing()

# Include ctest before any of the dependencies can do it, because they'll do it
# wrong.
include(CTest)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

add_subdirectory(deps/zipprof zipprof EXCLUDE_FROM_ALL)
add_subdirectory(deps/capnproto capnproto EXCLUDE_FROM_ALL)

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
  set(CMAKE_CXX_FLAGS "-Wall -Werror -Wno-unused-function -Wno-unused-variable -o3 -g -std=c++11 -fPIC")
else()
  set(CMAKE_CXX_FLAGS "-Wall -Werror -Wno-unused-function -Wno-unused-variable -o0 -g -std=c++11 -fPIC")
endif()


file(GLOB src_files "src/prof.cc" "src/stats.cc" "src/trace.cc")
add_library(capnprof ${src_files})
target_link_libraries(capnprof
    CapnProto::capnp CapnProto::kj capnpc zipprof)

add_executable(cprof "src/main.cc")
target_link_libraries(cprof capnprof)

file(GLOB test_files "tests/*.hh" "tests/*.cc")
add_executable(capnprof_test_main ${test_files} ${src_files})
target_link_libraries(capnprof_test_main gtest_main "z" CapnProto::capnp CapnProto::kj capnpc zipprof)
include_directories(capnprof_test_main
  "src"
  "deps/googletest/googletest/include"
  "deps/zipprof/include")
add_test(NAME capnprof_test COMMAND capnprof_test_main)
