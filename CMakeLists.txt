# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

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
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(tusb_uac2_dummy_mic C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(tusb_uac2_dummy_mic)

target_compile_definitions(tusb_uac2_dummy_mic PRIVATE CFG_TUSB_DEBUG=1)

target_sources(tusb_uac2_dummy_mic PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src/tusb_uac2_dummy_mic.c
    ${CMAKE_CURRENT_LIST_DIR}/lib/tusb/usb_descriptors.c
)

pico_set_program_name(tusb_uac2_dummy_mic "tusb_uac2_dummy_mic")
pico_set_program_version(tusb_uac2_dummy_mic "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(tusb_uac2_dummy_mic 1)
pico_enable_stdio_usb(tusb_uac2_dummy_mic 0)

# Add the standard library to the build
target_link_libraries(tusb_uac2_dummy_mic
        pico_stdlib
        pico_multicore
        tinyusb_device
        tinyusb_board
)

# Add the standard include files to the build
target_include_directories(tusb_uac2_dummy_mic PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/lib/tusb
)

pico_add_extra_outputs(tusb_uac2_dummy_mic)
