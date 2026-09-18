cmake_minimum_required(VERSION 3.16)

project(SensorHub)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Widgets QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Widgets REQUIRED)
    set(QT_LIB_PREFIX Qt5)
else()
    set(QT_LIB_PREFIX Qt6)
endif()

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 可执行目标
add_executable(SensorHub
    src/main.cpp
    src/core/DataHub.cpp
    src/data/SimulationProducer.cpp
)
target_link_libraries(SensorHub PRIVATE ${QT_LIB_PREFIX}::Widgets)
target_include_directories(SensorHub PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)