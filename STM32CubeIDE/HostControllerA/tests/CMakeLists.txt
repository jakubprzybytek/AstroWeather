cmake_minimum_required(VERSION 3.22)

add_executable(numeric_display_tests
    NumericDisplayTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../User/Src/Display/DisplayTypes.cpp
)

add_executable(display_codec_tests
    DisplayCodecTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../User/Src/Display/DisplayCodec.cpp
)

add_executable(current_sense_conversion_tests
    CurrentSenseConversionTests.cpp
)

add_executable(astro_data_parser_tests
    AstroDataParserTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../User/Src/HostController/AstroDataParser.cpp
)

target_include_directories(current_sense_conversion_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../User/Inc
)

target_include_directories(numeric_display_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../User/Inc
)

target_include_directories(display_codec_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../User/Inc
)

target_include_directories(astro_data_parser_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../User/Inc
)

target_compile_features(numeric_display_tests PRIVATE cxx_std_17)
target_compile_features(display_codec_tests PRIVATE cxx_std_17)
target_compile_features(astro_data_parser_tests PRIVATE cxx_std_17)

add_test(NAME numeric_display_tests COMMAND numeric_display_tests)
add_test(NAME display_codec_tests COMMAND display_codec_tests)
add_test(NAME current_sense_conversion_tests COMMAND current_sense_conversion_tests)
add_test(NAME astro_data_parser_tests COMMAND astro_data_parser_tests)