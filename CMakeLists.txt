cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(bb_awg C CXX ASM)

pico_sdk_init()

add_executable(bb_awg
    src/main.c
    src/cmd.c
    src/wave.c
    src/dac.c
)

pico_set_program_name(bb_awg "bb_awg")
pico_set_program_description(bb_awg "Breadboard AWG: AD7302 parallel DAC via PIO+DMA")
pico_set_program_version(bb_awg "0.1")

pico_generate_pio_header(bb_awg ${CMAKE_CURRENT_LIST_DIR}/src/ad7302.pio)

target_compile_options(bb_awg PRIVATE -Wall -Wextra)
target_include_directories(bb_awg PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

pico_enable_stdio_uart(bb_awg 0)   # GP0/GP1 are DAC data lines, not UART
pico_enable_stdio_usb(bb_awg 1)

target_link_libraries(bb_awg
    pico_stdlib
    hardware_pio
    hardware_dma
)

pico_add_extra_outputs(bb_awg)
