# ci/verify-installed-framework.cmake - CCU-DIAG-DSN-CR-009 §4.1/§9
#
# Verify the installed CCU Framework SDK is relocatable and consumable from an
# isolated prefix: find_package(CCUFramework CONFIG REQUIRED) resolves and the
# exported ccu-framework-log target compiles/links in a trivial HOST consumer.
# HOST success does NOT replace TARGET architecture/ABI/link verification.
#
# Usage:
#   cmake -DSDK_PREFIX=<framework-install-prefix> \
#         -P ci/verify-installed-framework.cmake
#
# Exit: FATAL_ERROR on any failure.

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED SDK_PREFIX)
    message(FATAL_ERROR "SDK_PREFIX must point to the installed framework prefix")
endif()

set(_consumer_dir "${CMAKE_CURRENT_BINARY_DIR}/_fw_consumer")
file(REMOVE_RECURSE "${_consumer_dir}")
file(MAKE_DIRECTORY "${_consumer_dir}")

file(WRITE "${_consumer_dir}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.24)
project(fw_consumer LANGUAGES CXX)
find_package(CCUFramework CONFIG REQUIRED)
add_executable(fw_consumer main.cpp)
target_compile_features(fw_consumer PRIVATE cxx_std_17)
target_link_libraries(fw_consumer PRIVATE CCUFramework::ccu-framework-log)
]=])

file(WRITE "${_consumer_dir}/main.cpp" [=[
#include "log.h"
int main() { return 0; }
]=])

message(STATUS "[verify-installed-framework] SDK_PREFIX=${SDK_PREFIX}")

execute_process(
    COMMAND ${CMAKE_COMMAND}
            -DCMAKE_PREFIX_PATH=${SDK_PREFIX}
            -DCCUFramework_DIR=${SDK_PREFIX}/lib/cmake/CCUFramework
            -S "${_consumer_dir}" -B "${_consumer_dir}/build"
    RESULT_VARIABLE _cfg)
if(NOT _cfg EQUAL 0)
    file(REMOVE_RECURSE "${_consumer_dir}")
    message(FATAL_ERROR "[verify-installed-framework] configure failed (find_package/resolve)")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} --build "${_consumer_dir}/build" --config Debug
    RESULT_VARIABLE _build)
if(NOT _build EQUAL 0)
    file(REMOVE_RECURSE "${_consumer_dir}")
    message(FATAL_ERROR "[verify-installed-framework] build failed (compile/link)")
endif()

file(REMOVE_RECURSE "${_consumer_dir}")
message(STATUS "[verify-installed-framework] OK: find_package + ccu-framework-log from isolated prefix")
