cmake_minimum_required(VERSION 3.16)
project(tmagent-plugin-sdk VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt5 or Qt6
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

# Header-only library (INTERFACE target)
add_library(tmagent-plugin-sdk INTERFACE)

# Include directories
target_include_directories(tmagent-plugin-sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link Qt Core
target_link_libraries(tmagent-plugin-sdk INTERFACE Qt${QT_VERSION_MAJOR}::Core)

# Define SDK version macros
target_compile_definitions(tmagent-plugin-sdk INTERFACE
    TMAGENT_SDK_VERSION_MAJOR=1
    TMAGENT_SDK_VERSION_MINOR=0
    TMAGENT_SDK_VERSION_PATCH=0
)

# Install headers
install(DIRECTORY include/tmagent
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# Generate CMake config files
include(CMakePackageConfigHelpers)

# Create config file
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tmagent-plugin-sdk-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/tmagent-plugin-sdk-config.cmake"
    INSTALL_DESTINATION lib/cmake/tmagent-plugin-sdk
)

# Create version file
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/tmagent-plugin-sdk-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Install CMake config files
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/tmagent-plugin-sdk-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tmagent-plugin-sdk-config-version.cmake"
    DESTINATION lib/cmake/tmagent-plugin-sdk
)
