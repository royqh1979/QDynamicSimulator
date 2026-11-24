cmake_minimum_required(VERSION 3.16)

project(QDynamicSimulator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets Svg)

add_subdirectory(src/core)
add_subdirectory(src/io)
add_subdirectory(src/app)

include(CTest)
if(BUILD_TESTING)
    add_custom_target(all-test-targets)
    add_custom_target(all-profile-targets)
    add_subdirectory(tests)
endif()
