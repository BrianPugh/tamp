# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Consume the LOCAL tamp component (espidf/tamp/), not the registry version.
# Its C sources are staged by `make -C espidf/tamp stage` (or `make esp32-device-data`).
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../espidf/tamp")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tamp_device_test)
