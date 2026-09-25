cmake_minimum_required(VERSION 3.22)

# Native (host) unit tests for the HostControllerA code. See docs/Testing.md.
# The code shared with the DisplayController has its own suites in
# ../../Common/tests.
#
#   add_native_test(<name> SOURCES <files...> [SUT <files...>] [STUBS] [FAKE_LOG])
#
# comes from Common/tests/NativeTest.cmake. SOURCES are relative to this
# directory, SUT paths to the project root (User/Src/... or ../Common/Src/...).
# STUBS puts the HAL, CMSIS-RTOS2 and FreeRTOS stand-ins and Core/Inc on the
# include path and links the stub implementations; FAKE_LOG links the recording
# LogService fake (implies STUBS).

set(NATIVE_TEST_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(NATIVE_TEST_INCLUDE_DIRS ${NATIVE_TEST_ROOT}/User/Inc)
set(NATIVE_TEST_BOARD_DIR ${NATIVE_TEST_ROOT}/Core/Inc)
set(NATIVE_TEST_FAKE_LOG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fakes)
include(${NATIVE_TEST_ROOT}/../Common/tests/NativeTest.cmake)

# Test infrastructure
add_native_test(test_support_tests
    SOURCES TestSupportTests.cpp
    FAKE_LOG
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
    SUT User/Src/Astro/AstroDataParser.cpp
)
add_native_test(astro_display_mapper_tests
    SOURCES AstroDisplayMapperTests.cpp
    SUT User/Src/Astro/AstroDisplayMapper.cpp
        User/Src/Astro/AstroDataParser.cpp
        ../Common/Src/Display/DisplayTypes.cpp
)
# STUBS for cmsis_os2.h, which St67FetchTypes.hpp (FetchStage) includes.
add_native_test(astro_progress_bar_tests
    SOURCES AstroProgressBarTests.cpp
    SUT User/Src/Astro/AstroProgressBar.cpp
    STUBS
)

# Settings
add_native_test(settings_codec_tests
    SOURCES SettingsCodecTests.cpp
    SUT User/Src/Settings/SettingsCodec.cpp
)

# Wi-Fi and HTTP logic extracted from the ST67 tasks
add_native_test(http_response_parser_tests
    SOURCES HttpResponseParserTests.cpp
    SUT User/Src/WiFi/HttpResponseParser.cpp
)
add_native_test(st67_http_rules_tests
    SOURCES St67HttpRulesTests.cpp
    SUT User/Src/WiFi/St67HttpRules.cpp
)
# The two below include St67FetchTypes.hpp, which needs cmsis_os2.h (STUBS).
add_native_test(st67_connect_diagnosis_tests
    SOURCES St67ConnectDiagnosisTests.cpp
    SUT User/Src/WiFi/St67ConnectDiagnosis.cpp
    STUBS
)
add_native_test(st67_fetch_status_map_tests
    SOURCES St67FetchStatusMapTests.cpp
    SUT User/Src/WiFi/St67FetchStatusMap.cpp
    STUBS
)
