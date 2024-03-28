# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# include("./tamp/_c_src/tamp/CMakeLists.txt")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED False)

idf_component_register(
    SRCS
        "tamp/_c_src/tamp/common.c"
        "tamp/_c_src/tamp/compressor.c"
        "tamp/_c_src/tamp/compressor_esp32.cpp"
        "tamp/_c_src/tamp/decompressor.c"
    INCLUDE_DIRS "tamp/_c_src"
    PRIV_INCLUDE_DIRS "tamp/_c_src/tamp"
)

if(CONFIG_TAMP_ESP32)
    # add_compile_definitions(TAMP_ESP32=1)
    add_definitions(-DTAMP_ESP32=1)
else()
    # add_compile_definitions(TAMP_ESP32=0)
    add_definitions(-DTAMP_ESP32=0)
endif()
