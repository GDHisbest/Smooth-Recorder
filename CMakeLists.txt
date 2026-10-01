cmake_minimum_required(VERSION 3.24)

project(SmoothRecorder)

find_package(geode REQUIRED)

add_geode_mod(
    ID flim.smooth_recorder
    RESOURCES about.md
    SOURCES src/main.cpp
)
