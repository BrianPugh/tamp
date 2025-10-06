# ESP-IDF test project for Tamp library
cmake_minimum_required(VERSION 3.16)

# The tamp component is accessed via symlink at components/tamp
# (symlink is created by Makefile: espidf/test/components/tamp -> ../tamp)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tamp_test)
