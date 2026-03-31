cmake_minimum_required(VERSION 3.16)
project(MinimalBackendPlugin VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

# Find SDK
find_package(tmagent-plugin-sdk 1.0 REQUIRED)

# Create plugin library
add_library(MinimalBackendPlugin MODULE
    MinimalBackendPlugin.cpp
    MinimalBackendPlugin.h
)

target_link_libraries(MinimalBackendPlugin PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    tmagent-plugin-sdk
)

# Copy metadata file to output directory
add_custom_command(TARGET MinimalBackendPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/minimal_backend.json
        $<TARGET_FILE_DIR:MinimalBackendPlugin>/minimal_backend.json
)
