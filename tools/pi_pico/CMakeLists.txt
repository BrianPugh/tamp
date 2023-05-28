# Set minimum required version of CMake
cmake_minimum_required(VERSION 3.12)

if(NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATH is not set. Please set the environment variable.")
endif()

# Include build functions from Pico SDK
include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)
pico_sdk_init()

# Set name of project (as PROJECT_NAME) and C/C   standards
project(tamp C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -ggdb -ffunction-sections -fdata-sections")

file(GLOB FILES ../../tamp/_c_src/tamp/*.c ../../tamp/_c_src/tamp/*.h)
add_library(tamp ${FILES})
#target_link_libraries(tamp pico_stdlib)
target_include_directories(tamp PUBLIC ../../tamp/_c_src/tamp/)
