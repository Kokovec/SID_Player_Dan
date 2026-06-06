# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(LIB_DIR ${CMAKE_CURRENT_LIST_DIR}/lib)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(SID_Player_Dan C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# SD card + FatFS library (carlk3/no-OS-FatFS-SD-SPI-RPi-Pico)
# SOURCE_SUBDIR points to the subdirectory that contains the CMakeLists.txt
# (the repo root has no CMakeLists.txt; the library lives under FatFs_SPI/).
include(FetchContent)
FetchContent_Declare(
    FatFs_SPI
    GIT_REPOSITORY https://github.com/carlk3/no-OS-FatFS-SD-SPI-RPi-Pico
    GIT_TAG        master
    SOURCE_SUBDIR  FatFs_SPI
)

# RP2350 has no hardware RTC. Provide an empty stub target so the carlk3
# library's CMakeLists.txt can link against hardware_rtc without error.
# The actual no-op function stubs live in hardware/rtc.h in this repo root.
if(NOT TARGET hardware_rtc)
    add_library(hardware_rtc INTERFACE)
    target_include_directories(hardware_rtc INTERFACE ${CMAKE_CURRENT_LIST_DIR})
endif()

FetchContent_MakeAvailable(FatFs_SPI)

# 6502 CPU emulator static library
add_library(m6502_cpu STATIC
    ${LIB_DIR}/m6502/sources/6502.c
)
target_include_directories(m6502_cpu PUBLIC
    ${LIB_DIR}/m6502/API
    ${CMAKE_CURRENT_LIST_DIR}
)
target_compile_definitions(m6502_cpu PUBLIC
    CPU_6502_STATIC
    CPU_6502_DEPENDENCIES_H="sid_types.h"
)

add_executable(SID_Player_Dan
    SID_Player_Dan.c
    hw_config.c
)

pico_generate_pio_header(SID_Player_Dan ${CMAKE_CURRENT_LIST_DIR}/uart_tx.pio)
pico_generate_pio_header(SID_Player_Dan ${CMAKE_CURRENT_LIST_DIR}/uart_rx.pio)

pico_set_program_name(SID_Player_Dan "SID_Player_Dan")
pico_set_program_version(SID_Player_Dan "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(SID_Player_Dan 0)
pico_enable_stdio_usb(SID_Player_Dan 1)

# Add the standard library to the build
target_link_libraries(SID_Player_Dan
        pico_stdlib
        hardware_pwm
        hardware_pio
        m6502_cpu
        FatFs_SPI)

# Add the standard include files to the build
target_include_directories(SID_Player_Dan PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(SID_Player_Dan)
