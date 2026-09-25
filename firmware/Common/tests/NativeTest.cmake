# add_native_test(<name> SOURCES <files...> [SUT <files...>] [STUBS] [FAKE_LOG])
#
# Registers one native test executable and its CTest entry. A tests/CMakeLists.txt
# sets these before including this file:
#
#   NATIVE_TEST_ROOT          the directory SUT paths are relative to
#   NATIVE_TEST_INCLUDE_DIRS  extra include directories (a project's User/Inc)
#   NATIVE_TEST_BOARD_DIR     the directory holding the main.h that STUBS builds
#                             compile against: a project's Core/Inc, or
#                             Common/tests/board for the shared code
#   NATIVE_TEST_FAKE_LOG_DIR  where FakeLog.hpp and FakeLogService.cpp are;
#                             only needed by FAKE_LOG tests
#
# SOURCES are relative to the calling directory. Common/Inc and tests/support
# are always on the include path. STUBS adds the HAL, CMSIS-RTOS2 and FreeRTOS
# stand-ins from tests/stubs plus NATIVE_TEST_BOARD_DIR, and links the stub
# implementations. FAKE_LOG links the recording LogService fake and TaskBase
# (implies STUBS).

option(NATIVE_TESTS_COVERAGE "Instrument native tests for gcov coverage" OFF)

set(NATIVE_TEST_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
set(NATIVE_TEST_STUBS_DIR ${CMAKE_CURRENT_LIST_DIR}/stubs)
set(NATIVE_TEST_SUPPORT_DIR ${CMAKE_CURRENT_LIST_DIR}/support)

if(NATIVE_TESTS_COVERAGE)
    add_compile_options(--coverage -O0)
    add_link_options(--coverage)
endif()

function(add_native_test name)
    cmake_parse_arguments(ARG "STUBS;FAKE_LOG" "" "SOURCES;SUT" ${ARGN})

    set(sut_paths)
    foreach(source IN LISTS ARG_SUT)
        list(APPEND sut_paths ${NATIVE_TEST_ROOT}/${source})
    endforeach()

    add_executable(${name} ${ARG_SOURCES} ${sut_paths})
    target_compile_features(${name} PRIVATE cxx_std_17)
    target_include_directories(${name} PRIVATE
        ${NATIVE_TEST_COMMON_DIR}/Inc
        ${NATIVE_TEST_INCLUDE_DIRS}
        ${NATIVE_TEST_SUPPORT_DIR}
    )

    if(ARG_STUBS OR ARG_FAKE_LOG)
        target_include_directories(${name} PRIVATE
            ${NATIVE_TEST_STUBS_DIR}
            ${NATIVE_TEST_BOARD_DIR}
        )
        target_sources(${name} PRIVATE
            ${NATIVE_TEST_STUBS_DIR}/StubHal.cpp
            ${NATIVE_TEST_STUBS_DIR}/StubRtos.cpp
        )
    endif()

    if(ARG_FAKE_LOG)
        if(NOT NATIVE_TEST_FAKE_LOG_DIR)
            message(FATAL_ERROR "${name}: FAKE_LOG needs NATIVE_TEST_FAKE_LOG_DIR")
        endif()
        target_include_directories(${name} PRIVATE ${NATIVE_TEST_FAKE_LOG_DIR})
        target_sources(${name} PRIVATE
            ${NATIVE_TEST_FAKE_LOG_DIR}/FakeLogService.cpp
            ${NATIVE_TEST_COMMON_DIR}/Src/Utils/TaskBase.cpp
        )
    endif()

    add_test(NAME ${name} COMMAND ${name})
endfunction()
