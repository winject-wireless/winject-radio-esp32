cmake_minimum_required(VERSION 3.16)
# IDF's project.cmake calls git_describe; set a version before include so a
# fresh tree without commits still configures (H5 / first clone).
set(PROJECT_VER "1.0.0")
add_compile_definitions(WINJECT_VERSION_STRING="v${PROJECT_VER}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(winject-esp32)
