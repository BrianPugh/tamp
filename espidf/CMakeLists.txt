# This ESP Component is only intended to be built from CI;
# See .github/workflows/esp_upload_component.yml
#
# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)


set( SRCS "tamp/common.c" "tamp/decompressor.c")
# list both variants as sources to force a rebuild of both .objs after switching variants via Kconfig.
# Otherwise, an incompatible .obj from a previous build may linger on and cause issues at link- or runtime!
list(APPEND SRCS "compressor_esp32.cpp")
list(APPEND SRCS "tamp/compressor.c")


idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "tamp/"
)
