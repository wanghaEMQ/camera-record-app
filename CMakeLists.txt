cmake_minimum_required(VERSION 3.1)
project(camera-record-app
  VERSION 0.0.1
  LANGUAGES CXX
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

find_package(nng)

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

#SET(CMAKE_C_FLAGS "-g -fsanitize=address")
#SET(CMAKE_CXX_FLAGS "-g -fsanitize=address")
SET(CMAKE_C_FLAGS "-g -fsanitize=address")
SET(CMAKE_CXX_FLAGS "-g -pthread")

add_executable(camerarecord src/main.cpp)
target_link_libraries(camerarecord
  PRIVATE
    opencv_core
    opencv_highgui
	nng
)

