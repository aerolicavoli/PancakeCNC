# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    /Users/adlic/esp/esp-idf/components/
)

project(pancake_esp)
