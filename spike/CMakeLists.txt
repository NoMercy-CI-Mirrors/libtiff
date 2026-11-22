cmake_minimum_required(VERSION 3.16)
project(tiff_core_spike LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra)

add_library(spike_core STATIC
    core/header.cpp
    core/ifd.cpp
    core/geometry.cpp
    core/codecs.cpp
    core/file.cpp
    capi/tiff_core_capi.cpp)
target_include_directories(spike_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/capi/include)

add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE spike_core)

# Pure C99 consumer of the safe C API: proves the installed header set is
# C-clean and usable without any C++ in the client.
add_executable(test_capi tests/test_capi.c)
target_link_libraries(test_capi PRIVATE spike_core)
set_target_properties(test_capi PROPERTIES LINKER_LANGUAGE CXX)

enable_testing()
add_test(NAME core_unit COMMAND test_core)
add_test(NAME capi_c99 COMMAND test_capi
    ${CMAKE_CURRENT_SOURCE_DIR}/../test/images/minisblack-1c-8b.tiff)
