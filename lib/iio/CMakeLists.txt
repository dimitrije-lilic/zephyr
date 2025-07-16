# SPDX-License-Identifier: Apache-2.0

zephyr_interface_library_named(iio)

target_include_directories(iio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

zephyr_library()

zephyr_library_sources(iio.c)

zephyr_library_link_libraries(iio)
