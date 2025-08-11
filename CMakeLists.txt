cmake_minimum_required(VERSION 3.5)

# Cross-compilation settings
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Set the target sysroot
set(CMAKE_SYSROOT ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
# Search for programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Search for libraries and headers in the target environment
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

project(gst-test)

find_package(PkgConfig REQUIRED)
pkg_search_module(gstreamer REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.2)
pkg_search_module(gstreamer-sdp REQUIRED IMPORTED_TARGET gstreamer-sdp-1.0>=1.2)
pkg_search_module(gstreamer-app REQUIRED IMPORTED_TARGET gstreamer-app-1.0>=1.2)
pkg_search_module(gstreamer-video REQUIRED IMPORTED_TARGET gstreamer-video-1.0>=1.2)
pkg_search_module(gstreamer-rtsp REQUIRED IMPORTED_TARGET gstreamer-rtsp-1.0>=1.2)
pkg_search_module(libfontconfig REQUIRED IMPORTED_TARGET fontconfig)
pkg_search_module(librga REQUIRED IMPORTED_TARGET librga)
pkg_search_module(libpng REQUIRED IMPORTED_TARGET libpng)

aux_source_directory(./yolov5 SOURCES)

add_executable(gst-test test-appnpu.cpp ${SOURCES})

target_include_directories(gst-test PUBLIC ${PROJECT_SOURCE_DIR}/rknn)

target_link_libraries(gst-test
    PkgConfig::gstreamer
    PkgConfig::gstreamer-sdp
    PkgConfig::gstreamer-app
    PkgConfig::gstreamer-video
    PkgConfig::gstreamer-rtsp
    PkgConfig::libfontconfig
    PkgConfig::librga
    PkgConfig::libpng
    rknnrt
)

set_target_properties(gst-test PROPERTIES LINK_SEARCH_START_STATIC 1)
set_target_properties(gst-test PROPERTIES LINK_SEARCH_END_STATIC 1)
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
