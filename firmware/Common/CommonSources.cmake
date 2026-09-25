# Code shared by the HostControllerA and DisplayController firmware.
#
# Each firmware project includes this file and adds ${COMMON_SOURCES} to its
# executable and ${COMMON_INCLUDE_DIR} to its include path. The sources are
# compiled inside each project, not into a library, because every file must see
# that project's device define (STM32G0B1xx or STM32G070xx), its
# stm32g0xx_hal_conf.h, its FreeRTOSConfig.h and its main.h pin labels.
#
# The shared code may include "main.h", "cmsis_os2.h" and "FreeRTOS.h" and use
# the pin labels both CubeMX projects define (LED_1, LED_2, SWITCH_1, SWITCH_2,
# ADDR_0..2, DISPLAY_1_EN..DISPLAY_5_EN, SCT_LATCH, SCT_ENABLE). It must not
# depend on anything only one project has, such as the host's LogService.

set(COMMON_DIR ${CMAKE_CURRENT_LIST_DIR})
set(COMMON_INCLUDE_DIR ${COMMON_DIR}/Inc)

file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS
    ${COMMON_DIR}/Src/*.c
    ${COMMON_DIR}/Src/*.cpp
)
