cmake_minimum_required(VERSION 3.10)
project(CgwDiagService)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -g)

# SOME/IP 实现选项
option(USE_MOCK_SOMEIP "Use mock SOME/IP implementation instead of vsomeip" ON)

if(USE_MOCK_SOMEIP)
    message(STATUS "Using mock SOME/IP implementation")
    add_definitions(-DUSE_MOCK_SOMEIP)
else()
    message(STATUS "Using real vsomeip implementation")
    
    # 查找 vsomeip
    find_package(vsomeip3 REQUIRED)
    if(vsomeip3_FOUND)
        message(STATUS "Found vsomeip: ${vsomeip3_DIR}")
        include_directories(${vsomeip3_INCLUDE_DIRS})
    else()
        message(FATAL_ERROR "vsomeip3 not found. Please install vsomeip.")
    endif()
    
    # 查找 Boost（vsomeip 依赖）
    find_package(Boost REQUIRED COMPONENTS system thread log)
    include_directories(${Boost_INCLUDE_DIRS})
endif()

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

# CGW Framework (provides cgw-framework-log: Logger, ContextScope, structured fields)
# CGW-DIAG-DSN-CR-008
find_package(CGWFramework REQUIRED)

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
    src/version_collector.cpp
    src/fota_facade.cpp
    src/someip_service.cpp
    src/someip_client_proxy.cpp
    src/cgw_sec_client_proxy.cpp
    src/fota_someip_service.cpp
    src/fota_someip_proxy.cpp
    src/vin_store.cpp
    src/diagnostic_server.cpp
    src/diag_log_adapter.cpp
    src/diag_log_context.cpp
)

# 根据 SOME/IP 实现添加源文件
if(USE_MOCK_SOMEIP)
    # 模拟实现不需要额外源文件
else()
    list(APPEND LIB_SOURCES
        src/someip_client_proxy_vsomeip.cpp
        src/someip_service_vsomeip.cpp
    )
endif()

# Create cgw_diag library
add_library(cgw_diag_lib STATIC ${LIB_SOURCES})
target_include_directories(cgw_diag_lib PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(cgw_diag_lib PUBLIC yaml-cpp::yaml-cpp nlohmann_json::nlohmann_json CGWFramework::cgw-framework)

# 根据 SOME/IP 实现添加链接库
if(USE_MOCK_SOMEIP)
    # 模拟实现不需要额外链接
else()
    target_link_libraries(cgw_diag_lib PUBLIC 
        vsomeip3
        ${Boost_LIBRARIES}
    )
endif()

# Main application executable
add_executable(cgw_diag src/main.cpp)
target_include_directories(cgw_diag PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(cgw_diag PRIVATE cgw_diag_lib)

enable_testing()
add_executable(CgwDiagTests
    tests/test_error_codes.cpp
    tests/test_data_models.cpp
    tests/test_config_loader.cpp
    tests/test_did_registry.cpp
    tests/test_uds_client.cpp
    tests/test_diagnostic_router.cpp
    tests/test_transport_adapter.cpp
    tests/test_version_collector.cpp
    tests/test_fota_facade.cpp
    tests/test_someip_service.cpp
    tests/test_someip_proxy.cpp
    tests/test_someip_factory.cpp
    tests/test_integration.cpp
    tests/test_vin_store.cpp
    tests/test_diagnostic_server.cpp
    tests/test_vin_integration.cpp
    tests/test_log_adapter.cpp
    tests/test_log_events.cpp
    tests/test_log_integration.cpp
)
target_include_directories(CgwDiagTests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(CgwDiagTests PRIVATE cgw_diag_lib GTest::GTest GTest::gmock GTest::Main)

# 根据 SOME/IP 实现添加链接库
if(USE_MOCK_SOMEIP)
    # 模拟实现不需要额外链接
else()
    target_link_libraries(CgwDiagTests PRIVATE 
        vsomeip3
        ${Boost_LIBRARIES}
    )
endif()
add_test(NAME CgwDiagTests COMMAND CgwDiagTests)

add_executable(test_cgw_sec_client_proxy
    tests/test_cgw_sec_client_proxy.cpp
)
target_include_directories(test_cgw_sec_client_proxy PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(test_cgw_sec_client_proxy PRIVATE cgw_diag_lib GTest::GTest GTest::gmock GTest::Main)
add_test(NAME test_cgw_sec_client_proxy COMMAND test_cgw_sec_client_proxy)
