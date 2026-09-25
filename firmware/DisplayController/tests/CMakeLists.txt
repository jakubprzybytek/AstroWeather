cmake_minimum_required(VERSION 3.22)

# Native (host) unit tests for the DisplayController code. The shared display
# code has its own suites in ../../Common/tests. add_native_test() comes from
# Common/tests/NativeTest.cmake; see HostControllerA/docs/Testing.md.

set(NATIVE_TEST_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(NATIVE_TEST_INCLUDE_DIRS ${NATIVE_TEST_ROOT}/User/Inc)
set(NATIVE_TEST_BOARD_DIR ${NATIVE_TEST_ROOT}/Core/Inc)
include(${NATIVE_TEST_ROOT}/../Common/tests/NativeTest.cmake)

add_native_test(screens_tests
    SOURCES ScreensTests.cpp
    SUT User/Src/Screens.cpp
)
add_native_test(no_data_timer_tests
    SOURCES NoDataTimerTests.cpp
)
