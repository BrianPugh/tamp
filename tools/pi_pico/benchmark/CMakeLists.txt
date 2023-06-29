# Set minimum required version of CMake
cmake_minimum_required(VERSION 3.12)

if(NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATH is not set. Please set the environment variable.")
endif()

# Include build functions from Pico SDK
include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)
pico_sdk_init()

# Set name of project (as PROJECT_NAME) and C/C   standards
project(tamp_benchmark C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -ggdb -ffunction-sections -fdata-sections")

file(GLOB TAMP_FILES ../../../tamp/_c_src/tamp/*.c ../../../tamp/_c_src/tamp/*.h)
add_library(tamp ${TAMP_FILES})
target_include_directories(tamp PUBLIC ../../../tamp/_c_src/)

add_executable(tamp_benchmark main.c)
target_link_libraries(tamp_benchmark tamp)

# Add pico_stdlib library which aggregates commonly used features
target_link_libraries(tamp_benchmark pico_stdlib)

pico_enable_stdio_usb(tamp_benchmark 1)
pico_enable_stdio_uart(tamp_benchmark 0)
pico_add_extra_outputs(tamp_benchmark)



function(embed_resource resource_file_name source_file_name variable_name)

    if(EXISTS "${source_file_name}")
        if("${source_file_name}" IS_NEWER_THAN "${resource_file_name}")
            return()
        endif()
    endif()

    file(READ "${resource_file_name}" hex_content HEX)

    string(REPEAT "[0-9a-f]" 32 pattern)
    string(REGEX REPLACE "(${pattern})" "\\1\n" content "${hex_content}")

    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " content "${content}")

    string(REGEX REPLACE ", $" "" content "${content}")

    set(array_definition "static const unsigned char ${variable_name}[] =\n{\n${content}\n};")

    set(source "// Auto generated file.\n${array_definition}\n")

    file(WRITE "${source_file_name}" "${source}")

endfunction()

embed_resource("../../../build/enwik8-100kb" "enwik8.h" "ENWIK8")
embed_resource("../../../build/enwik8-100kb.tamp" "enwik8_compressed.h" "ENWIK8_COMPRESSED")
