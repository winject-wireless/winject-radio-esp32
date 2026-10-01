cmake_minimum_required(VERSION 3.16)
# IDF's project.cmake calls git_describe; set a version before include so a
# fresh tree without commits still configures (H5 / first clone).
set(PROJECT_VER "0.0.0-dev")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(winject-esp32)
