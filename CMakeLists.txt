cmake_minimum_required(VERSION 3.10)
project(CgwDiagService)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -g)

# LIB_DIR: reserved for platform-specific third-party libraries
if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
    if (APPLE)
        set(LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/lib/arm64-apple-darwin)
    elseif (UNIX AND NOT APPLE)
        set(LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/lib/aarch64-linux-gnu)
    endif ()
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    if (APPLE)
        set(LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/lib/x86_64-apple-darwin)
    elseif (UNIX AND NOT APPLE)
        set(LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/lib/x86_64-linux-gnu)
    endif ()
endif ()

find_package(yaml-cpp REQUIRED)
include_directories(${YAML_CPP_INCLUDE_DIRS})

find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

find_package(nlohmann_json REQUIRED)

set(LIB_SOURCES
    src/error_codes.cpp
    src/data_models.cpp
    src/config_loader.cpp
    src/did_registry.cpp
    src/uds_client.cpp
    src/diagnostic_router.cpp
    src/do_can_transport.cpp
    src/doip_transport.cpp
)

# Create cgw_diag library
add_library(cgw_diag_lib STATIC ${LIB_SOURCES})
target_include_directories(cgw_diag_lib PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(cgw_diag_lib PUBLIC yaml-cpp::yaml-cpp nlohmann_json::nlohmann_json)

enable_testing()
add_executable(CgwDiagTests
    tests/test_error_codes.cpp
    tests/test_data_models.cpp
    tests/test_config_loader.cpp
    tests/test_did_registry.cpp
    tests/test_uds_client.cpp
    tests/test_diagnostic_router.cpp
    tests/test_transport_adapter.cpp
)
target_include_directories(CgwDiagTests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(CgwDiagTests PRIVATE cgw_diag_lib GTest::GTest GTest::gmock GTest::Main)
add_test(NAME CgwDiagTests COMMAND CgwDiagTests)
