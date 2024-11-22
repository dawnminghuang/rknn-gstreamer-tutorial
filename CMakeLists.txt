cmake_minimum_required(VERSION 3.5)
project(gst-test)

find_package(PkgConfig REQUIRED)
pkg_search_module(gstreamer REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.4)
pkg_search_module(gstreamer-sdp REQUIRED IMPORTED_TARGET gstreamer-sdp-1.0>=1.4)
pkg_search_module(gstreamer-app REQUIRED IMPORTED_TARGET gstreamer-app-1.0>=1.4)
pkg_search_module(gstreamer-video REQUIRED IMPORTED_TARGET gstreamer-video-1.0>=1.4)
pkg_search_module(gstreamer-rtsp REQUIRED IMPORTED_TARGET gstreamer-rtsp-1.0>=1.4)
pkg_search_module(gstreamer-rtsp-server REQUIRED IMPORTED_TARGET gstreamer-rtsp-server-1.0>=1.4)
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
    PkgConfig::gstreamer-rtsp-server
    PkgConfig::libfontconfig
    PkgConfig::librga
    PkgConfig::libpng
    rknnrt
)

set_target_properties(gst-test PROPERTIES LINK_SEARCH_START_STATIC 1)
set_target_properties(gst-test PROPERTIES LINK_SEARCH_END_STATIC 1)
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
