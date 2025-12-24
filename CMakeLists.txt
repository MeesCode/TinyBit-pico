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
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(tinybit C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add TinyBit library
add_subdirectory(TinyBit-lib)

# Add SD library
add_subdirectory(no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)

# Add executable. Default name is the project name, version 0.1

add_executable(tinybit
    main.c
    st7789_lcd.c
    syscalls.c
    hw_config.c
)

pico_generate_pio_header(tinybit ${CMAKE_CURRENT_LIST_DIR}/st7789_lcd.pio)

# pull in common dependencies
target_link_libraries(
    tinybit
    pico_stdlib
    hardware_pio
    hardware_interp
    hardware_dma
    tinybit_lib
    no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
)
# Increase heap size for Lua
# With TinyBitMemory (80KB), we have limited RAM left
target_compile_definitions(tinybit PRIVATE
    PICO_HEAP_SIZE=32768
)


# Enable USB output for debugging (optional)
pico_enable_stdio_usb(tinybit 1)
pico_enable_stdio_uart(tinybit 0)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(tinybit)

# call pico_set_program_url to set path to example on github, so users can find the source for an example via picotool
