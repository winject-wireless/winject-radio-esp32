cmake_minimum_required(VERSION 3.16)
project(winject-esp32-host-tests CXX)

# Host unit tests for the IDF-independent firmware modules (m-plane commands,
# settings codec, config validation, FCS). Offline builds can point
# FETCHCONTENT_SOURCE_DIR_GOOGLETEST at an existing googletest checkout.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googletest)

set(FW_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../winject-esp32)

add_library(winject-host STATIC
    ${FW_DIR}/config_types.cpp
    ${FW_DIR}/diag/test_run.cpp
    ${FW_DIR}/settings_blob.cpp
    ${FW_DIR}/radio/fcs.cpp
    ${FW_DIR}/mplane/mplane_args.cpp
    ${FW_DIR}/mplane/mplane_commands.cpp
    ${FW_DIR}/mplane/mplane_reply.cpp
    ${FW_DIR}/mplane/mplane_req_id.cpp
)
target_include_directories(winject-host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${FW_DIR}
    ${FW_DIR}/diag
    ${FW_DIR}/endpoint
    ${FW_DIR}/radio
    ${FW_DIR}/mplane
)
target_compile_definitions(winject-host PUBLIC WINJECT_HOST_TEST)
target_compile_options(winject-host PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_executable(winject-esp32-host-tests
    config_types_test.cpp
    control_peer_test.cpp
    fcs_test.cpp
    mplane_args_test.cpp
    mplane_commands_test.cpp
    settings_blob_test.cpp
    test_run_test.cpp
    udp_l2_match_test.cpp
)
target_include_directories(winject-esp32-host-tests PRIVATE ${FW_DIR}
                                                      ${FW_DIR}/endpoint)
target_link_libraries(winject-esp32-host-tests PRIVATE winject-host
                      GTest::gtest_main)
target_compile_options(winject-esp32-host-tests PRIVATE -Wall -Wextra)

include(GoogleTest)
gtest_discover_tests(winject-esp32-host-tests)
