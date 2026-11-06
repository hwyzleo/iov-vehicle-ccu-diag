# =============================================================================
# CCU-DIAG - CMake build (CCU-DIAG-DSN-CR-009 §3/§4)
#
# Contract:
#   * Standard configure -> build -> install with GNUInstallDirs.
#   * No compiler / sysroot / target triple / host absolute path is set here.
#   * Framework is consumed only through the installed CCUFramework package
#     (find_package CONFIG + imported targets); no source/build-tree usage.
#   * Production target stays ccu_diag. The single rootfs install component is
#     ccu-diag-runtime and installs exactly:
#         usr/bin/ccu_diag
#         usr/lib/systemd/system/ccu-diag.service
#         etc/ccu/conf.d/diag.yaml                (from diag.default.yaml)
#         usr/share/ccu-diag/schema/diag.schema.yaml
#     Internal lib / headers / tests / dev fixtures / VIN are NOT installed.
#   * BUILD_TESTING=OFF must not find GTest or create test targets.
#   * USE_MOCK_SOMEIP is an explicit option; production TARGET builds pass OFF.
#     No HOST environment auto-detection.
# =============================================================================
cmake_minimum_required(VERSION 3.24)
project(CcuDiagService VERSION 1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(DiagWarnings)

# ----------------------------------------------------------------------------
# Options (explicit only)
# ----------------------------------------------------------------------------
option(USE_MOCK_SOMEIP
       "Use mock SOME/IP backend (HOST/dev/CI only; production passes OFF)"
       OFF)

# ----------------------------------------------------------------------------
# Dependency classification (CCU-DIAG-DSN-CR-009 §4.2)
#   framework       : service dependency, installed SDK only
#   yaml-cpp        : direct TARGET dependency (BUILD lock ID: yaml-cpp)
#   nlohmann_json   : direct TARGET dependency (BUILD lock ID: nlohmann-json)
#   vsomeip3/Boost  : production backend direct dependency (real backend only)
#   GTest           : HOST/test-only (BUILD_TESTING)
# ----------------------------------------------------------------------------
find_package(Threads REQUIRED)
find_package(CCUFramework CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

if(NOT USE_MOCK_SOMEIP)
    message(STATUS "Using real vsomeip SOME/IP backend")
    find_package(vsomeip3 REQUIRED)
    find_package(Boost REQUIRED COMPONENTS system thread log)
else()
    message(STATUS "Using mock SOME/IP backend (NON_PRODUCTION)")
endif()

# ----------------------------------------------------------------------------
# Sources
# ----------------------------------------------------------------------------
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
    src/ccu_sec_client_proxy.cpp
    src/fota_someip_service.cpp
    src/fota_someip_proxy.cpp
    src/vin_store.cpp
    src/diagnostic_server.cpp
    src/diag_log_adapter.cpp
    src/diag_log_context.cpp
    src/someip_process_application.cpp
)

if(NOT USE_MOCK_SOMEIP)
    list(APPEND LIB_SOURCES
        src/someip_client_proxy_vsomeip.cpp
        src/someip_service_vsomeip.cpp
    )
endif()

# Internal object collection. Never installed, never exported.
add_library(ccu_diag_lib STATIC ${LIB_SOURCES})
target_include_directories(ccu_diag_lib
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_compile_features(ccu_diag_lib PUBLIC cxx_std_17)
target_link_libraries(ccu_diag_lib
    PUBLIC
        CCUFramework::ccu-framework-log
        yaml-cpp::yaml-cpp
        nlohmann_json::nlohmann_json
        Threads::Threads)
if(USE_MOCK_SOMEIP)
    target_compile_definitions(ccu_diag_lib PUBLIC USE_MOCK_SOMEIP)
else()
    # Include dirs come from the controlled vsomeip3/Boost packages/sysroot.
    target_include_directories(ccu_diag_lib PUBLIC
        ${vsomeip3_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
    target_link_libraries(ccu_diag_lib PUBLIC vsomeip3 ${Boost_LIBRARIES})
endif()
diag_apply_warnings(ccu_diag_lib)

# Production daemon target (fixed name ccu_diag).
add_executable(ccu_diag src/main.cpp)
target_link_libraries(ccu_diag PRIVATE ccu_diag_lib)
target_compile_features(ccu_diag PRIVATE cxx_std_17)
diag_apply_warnings(ccu_diag)

# ----------------------------------------------------------------------------
# Tests: HOST-only, non-installed. Never configured when BUILD_TESTING=OFF.
# ----------------------------------------------------------------------------
if(BUILD_TESTING)
    enable_testing()
    find_package(GTest REQUIRED)

    # GTest imported targets mark their include dir as SYSTEM (-isystem). If an
    # earlier -isystem dir on the search path (e.g. the CCU Framework SDK
    # prefix) also ships gtest/gmock headers of a different version, the
    # compiler would use those headers while linking a mismatched GTest library
    # (ABI/ODR break). Re-add the found GTest include dir as a plain -I
    # (searched before any -isystem dir) so headers always match the linked
    # library, whichever GTest install find_package resolved.
    get_target_property(GTEST_INCLUDE_DIRS GTest::gtest INTERFACE_INCLUDE_DIRECTORIES)

    add_executable(CcuDiagTests
        tests/test_error_codes.cpp
        tests/test_data_models.cpp
        tests/test_config_loader.cpp
        tests/test_unified_config.cpp
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
    target_include_directories(CcuDiagTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    if(GTEST_INCLUDE_DIRS)
        target_include_directories(CcuDiagTests PRIVATE ${GTEST_INCLUDE_DIRS})
    endif()
    target_compile_definitions(CcuDiagTests
        PRIVATE CCU_DIAG_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(CcuDiagTests
        PRIVATE ccu_diag_lib GTest::GTest GTest::gmock GTest::Main)
    diag_apply_warnings(CcuDiagTests)
    add_test(NAME CcuDiagTests COMMAND CcuDiagTests)

    add_executable(test_ccu_sec_client_proxy
        tests/test_ccu_sec_client_proxy.cpp
    )
    target_include_directories(test_ccu_sec_client_proxy
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    if(GTEST_INCLUDE_DIRS)
        target_include_directories(test_ccu_sec_client_proxy PRIVATE ${GTEST_INCLUDE_DIRS})
    endif()
    target_link_libraries(test_ccu_sec_client_proxy
        PRIVATE ccu_diag_lib GTest::GTest GTest::gmock GTest::Main)
    diag_apply_warnings(test_ccu_sec_client_proxy)
    add_test(NAME test_ccu_sec_client_proxy COMMAND test_ccu_sec_client_proxy)
endif()

# ----------------------------------------------------------------------------
# Install: ccu-diag-runtime rootfs component only (CCU-DIAG-DSN-CR-009 §3.2).
# Supports CMAKE_INSTALL_PREFIX=/usr + BUILD-managed DESTDIR. No install-time
# network/device access, no service start/stop, no install-time provisioning.
# ----------------------------------------------------------------------------
install(TARGETS ccu_diag
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT ccu-diag-runtime)

install(FILES packaging/systemd/ccu-diag.service
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system
    COMPONENT ccu-diag-runtime)

# FULL_SYSCONFDIR gives /etc when PREFIX=/usr (FHS special case), matching the
# BUILD rootfs staging layout (etc/ at install-root root) and main.cpp's
# production default /etc/ccu/conf.d/diag.yaml.
install(FILES config/diag.default.yaml
    DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/ccu/conf.d
    RENAME diag.yaml
    COMPONENT ccu-diag-runtime)

install(FILES config/schema/diag.schema.yaml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/ccu-diag/schema
    COMPONENT ccu-diag-runtime)
