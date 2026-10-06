cmake_minimum_required(VERSION 3.31)

project(auto-club CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_var.cmake)

add_compile_options(-Wno-psabi -O3)

set(BUILD_SHARED_LIBS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

find_package(phoenix6 REQUIRED)
set_target_properties(phoenix6 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SYSROOT}/usr/include/phoenix6"
    INTERFACE_LINK_DIRECTORIES "${CMAKE_SYSROOT}/usr/lib/phoenix6"
)

find_package(spdlog REQUIRED)
add_compile_definitions(SPDLOG_DISABLE_DEFAULT_LOGGER)

set(PREV_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
set(HTTPLIB_COMPILE ON)
add_subdirectory(external/cpp-httplib)
set_property(TARGET httplib PROPERTY POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS ${PREV_BUILD_SHARED_LIBS})

add_subdirectory(external/json)

add_executable(car_host
    src/main.cpp
)
target_include_directories(car_host PRIVATE ./include)
target_link_libraries(car_host
PRIVATE
    PkgConfig::LIBSYSTEMD
    phoenix6
    spdlog::spdlog
)

set(OUTPUT_DIR_PRE ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR_PRE}/plugin)

add_library(control MODULE
    src/plugin/control.cpp
)
target_include_directories(control PRIVATE ./include)
target_link_libraries(control
PRIVATE
    phoenix6
    spdlog::spdlog
)

add_library(read MODULE
    src/plugin/read.cpp
)
target_include_directories(read PRIVATE ./include)
target_link_libraries(read
PRIVATE
    spdlog::spdlog
)

add_library(telemetry MODULE
    src/plugin/telemetry.cpp
)
target_include_directories(telemetry PRIVATE ./include)
target_link_libraries(telemetry
PRIVATE
    phoenix6
    spdlog::spdlog
    httplib::httplib
    nlohmann_json::nlohmann_json
)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR_PRE})

CHECK_VAR(RPI_HOST "Hostname of the raspberry pi")
CHECK_VAR(RPI_USER "User to login on the raspberry pi")
CHECK_VAR(RPI_DEST "Destination path on the raspberry pi for deployment")

add_custom_target(build_all
    COMMENT "Building all components"
    DEPENDS car_host control read telemetry
)

add_custom_target(deploy
    COMMAND ssh ${RPI_USER}@${RPI_HOST} "mkdir -p ${RPI_DEST}"
    COMMAND rsync -avz ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ ${RPI_USER}@${RPI_HOST}:${RPI_DEST}/
    COMMENT "Deploying via rsync to ${RPI_USER}@${RPI_HOST}..."
    DEPENDS car_host control read telemetry
)

add_custom_target(deploy_plugins
    COMMAND rsync -avz ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/plugin/ ${RPI_USER}@${RPI_HOST}:${RPI_DEST}/plugin/
    COMMENT "Deploying via rsync to ${RPI_USER}@${RPI_HOST}..."
    DEPENDS control read telemetry
)
