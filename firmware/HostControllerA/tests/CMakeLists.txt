cmake_minimum_required(VERSION 3.22)

# Native (host) unit tests. See docs/Testing.md.
#
#   add_native_test(<name> SOURCES <files...> [SUT <User/Src files...>] [STUBS] [FAKE_LOG])
#
# SOURCES are paths relative to this directory, SUT paths relative to the
# project root. STUBS puts tests/stubs (HAL, CMSIS-RTOS2 and FreeRTOS
# stand-ins) and Core/Inc on the include path and links the stub
# implementations; FAKE_LOG links the recording LogService fake (implies STUBS).

option(NATIVE_TESTS_COVERAGE "Instrument native tests for gcov coverage" OFF)

set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(NATIVE_TESTS_COVERAGE)
    add_compile_options(--coverage -O0)
    add_link_options(--coverage)
endif()

function(add_native_test name)
    cmake_parse_arguments(ARG "STUBS;FAKE_LOG" "" "SOURCES;SUT" ${ARGN})

    set(sut_paths)
    foreach(source IN LISTS ARG_SUT)
        list(APPEND sut_paths ${PROJECT_ROOT}/${source})
    endforeach()

    add_executable(${name} ${ARG_SOURCES} ${sut_paths})
    target_compile_features(${name} PRIVATE cxx_std_17)
    target_include_directories(${name} PRIVATE
        ${PROJECT_ROOT}/User/Inc
        ${CMAKE_CURRENT_SOURCE_DIR}/support
    )

    if(ARG_STUBS OR ARG_FAKE_LOG)
        target_include_directories(${name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/stubs
            ${CMAKE_CURRENT_SOURCE_DIR}/fakes
            ${PROJECT_ROOT}/Core/Inc
        )
        target_sources(${name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/stubs/StubHal.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/stubs/StubRtos.cpp
        )
    endif()

    if(ARG_FAKE_LOG)
        target_sources(${name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/fakes/FakeLogService.cpp
            ${PROJECT_ROOT}/User/Src/Utils/TaskBase.cpp
        )
    endif()

    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Test infrastructure
add_native_test(test_support_tests
    SOURCES TestSupportTests.cpp
    FAKE_LOG
)

# Display
add_native_test(numeric_display_tests
    SOURCES NumericDisplayTests.cpp
    SUT User/Src/Display/DisplayTypes.cpp
)
add_native_test(display_codec_tests
    SOURCES DisplayCodecTests.cpp
    SUT User/Src/Display/DisplayCodec.cpp
)

# Sensors
add_native_test(current_sense_conversion_tests
    SOURCES CurrentSenseConversionTests.cpp
)

# Clock
add_native_test(calendar_date_tests
    SOURCES CalendarDateTests.cpp
)
add_native_test(rtc_trim_tests
    SOURCES RtcTrimTests.cpp
)
add_native_test(clock_sync_tests
    SOURCES ClockSyncTests.cpp
)

# Astro refresh
add_native_test(refresh_schedule_tests
    SOURCES RefreshScheduleTests.cpp
)
add_native_test(astro_data_parser_tests
    SOURCES AstroDataParserTests.cpp
    SUT User/Src/HostController/AstroDataParser.cpp
)

# Settings
add_native_test(settings_codec_tests
    SOURCES SettingsCodecTests.cpp
    SUT User/Src/Settings/SettingsCodec.cpp
)

# ---------------------------------------------------------------------------
# Phase 1: display protocol, address straps and parser edge cases
# ---------------------------------------------------------------------------
add_native_test(display_i2c_protocol_tests
    SOURCES DisplayI2cProtocolTests.cpp
    SUT User/Src/Display/DisplayI2cProtocol.cpp
)
add_native_test(display_address_tests
    SOURCES DisplayAddressTests.cpp
    SUT User/Src/Display/DisplayAddress.cpp
    STUBS
)

# ---------------------------------------------------------------------------
# Phase 2: Wi-Fi and HTTP logic extracted from the ST67 tasks
# ---------------------------------------------------------------------------
add_native_test(http_response_parser_tests
    SOURCES HttpResponseParserTests.cpp
    SUT User/Src/WiFi/HttpResponseParser.cpp
)
add_native_test(st67_http_rules_tests
    SOURCES St67HttpRulesTests.cpp
    SUT User/Src/WiFi/St67HttpRules.cpp
)
# The two below include St67FetchTypes.hpp, which needs cmsis_os2.h (STUBS);
# St67HttpFetchTask.hpp includes it without the HostController/ prefix.
add_native_test(st67_connect_diagnosis_tests
    SOURCES St67ConnectDiagnosisTests.cpp
    SUT User/Src/WiFi/St67ConnectDiagnosis.cpp
    STUBS
)
target_include_directories(st67_connect_diagnosis_tests PRIVATE
    ${PROJECT_ROOT}/User/Inc/HostController
)
add_native_test(st67_fetch_status_map_tests
    SOURCES St67FetchStatusMapTests.cpp
    SUT User/Src/WiFi/St67FetchStatusMap.cpp
    STUBS
)

# ---------------------------------------------------------------------------
# Phase 2: astro publishing, progress bar and sensing logic
# ---------------------------------------------------------------------------
add_native_test(astro_display_mapper_tests
    SOURCES AstroDisplayMapperTests.cpp
    SUT User/Src/HostController/AstroDisplayMapper.cpp
        User/Src/HostController/AstroDataParser.cpp
        User/Src/Display/DisplayTypes.cpp
)
# STUBS for cmsis_os2.h, which St67FetchTypes.hpp (FetchStage) includes.
add_native_test(astro_progress_bar_tests
    SOURCES AstroProgressBarTests.cpp
    SUT User/Src/HostController/AstroProgressBar.cpp
    STUBS
)
add_native_test(crc32_tests
    SOURCES Crc32Tests.cpp
)
