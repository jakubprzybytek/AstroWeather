cmake_minimum_required(VERSION 3.22)

add_executable(numeric_display_tests
    NumericDisplayTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../User/Src/Display/DisplayTypes.cpp
)

target_include_directories(numeric_display_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../User/Inc
)

target_compile_features(numeric_display_tests PRIVATE cxx_std_17)

add_test(NAME numeric_display_tests COMMAND numeric_display_tests)