cmake_minimum_required(VERSION 3.10)
project(apriltag_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-app-1.0
)

include_directories(
    ${OpenCV_INCLUDE_DIRS}
    ${GST_INCLUDE_DIRS}
)

add_executable(apriltag_demo
    src/main.cpp
)

target_link_libraries(apriltag_demo
    ${OpenCV_LIBS}
    ${GST_LIBRARIES}
    apriltag
)
