cmake_minimum_required(VERSION 3.1)
project(camera-record-app
  VERSION 0.0.1
  LANGUAGES C
)

# Additional search scripts path for libusb-1.0, libjpeg, OpenCV
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

# Xcode and Visual Studio do not using CMAKE_BUILD_TYPE cache variable
# so we use Release build type only with single configuration generators.
if (NOT CMAKE_CONFIGURATION_TYPES)
  if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "No build type selected, default to Release")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
  endif()
endif()

option(BUILD_TEST "Build test program" OFF)
option(ENABLE_UVC_DEBUGGING "Enable UVC debugging" OFF)

set(libuvc_DESCRIPTION "A cross-platform library for USB video devices")
set(libuvc_URL "https://github.com/libuvc/libuvc")

set(SOURCES 
  src/ctrl.c
  src/ctrl-gen.c
  src/device.c
  src/diag.c
  src/frame.c
  src/init.c
  src/stream.c
  src/misc.c
)

find_package(LibUSB)
find_package(LibUVC)

# JpegPkg name to differ from shipped with CMake
find_package(JpegPkg QUIET)
if(JPEG_FOUND)
  message(STATUS "Building libuvc with JPEG support.")
  set(LIBUVC_HAS_JPEG TRUE)
  list(APPEND SOURCES src/frame-mjpeg.c)
else()
  message(WARNING "JPEG not found. libuvc will not support JPEG decoding.")
endif()

if(UNIX AND NOT APPLE)
  set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
  set(THREADS_PREFER_PTHREAD_FLAG TRUE)
  find_package(Threads REQUIRED)
  set(threads Threads::Threads)
endif()

# OpenCV defines targets with transitive dependencies not with namespaces but using opencv_ prefix. 
# This targets provide necessary include directories and linked flags.
find_package(OpenCVPkg REQUIRED
  COMPONENTS
    opencv_core
    opencv_highgui
)

add_executable(camerarecord src/main.c)
target_link_libraries(camerarecord
  PRIVATE
    uvc
    opencv_core
    opencv_highgui
)

