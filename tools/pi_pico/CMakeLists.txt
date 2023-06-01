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
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -ggdb -ffunction-sections -fdata-sections")

file(GLOB FILES ../../tamp/_c_src/tamp/*.c ../../tamp/_c_src/tamp/*.h)
add_library(tamp ${FILES})
target_include_directories(tamp PUBLIC ../../tamp/_c_src/tamp/)

# include zlib for comparison
add_library(zlib
    zlib/adler32.c
    zlib/compress.c
    zlib/crc32.c
    zlib/deflate.c
    zlib/inffast.c
    zlib/inflate.c
    zlib/inftrees.c
    zlib/trees.c
    zlib/uncompr.c
    zlib/zutil.c
)
target_compile_definitions(zlib PRIVATE Z_SOLO=1 NO_GZIP=1)
target_include_directories(zlib PUBLIC ${CMAKE_CURRENT_LIST_DIR}/zlib)

add_library(uzlib
    uzlib/src/tinflate.c
    uzlib/src/genlz77.c
    uzlib/src/defl_static.c
    )
target_include_directories(zlib PUBLIC ${CMAKE_CURRENT_LIST_DIR}/uzlib/src)
