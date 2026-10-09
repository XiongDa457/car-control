cmake_minimum_required(VERSION 3.31)
cmake_policy(SET CMP0210 NEW)

project(auto-club C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_var.cmake)

add_compile_options(-Wno-psabi)

set(BUILD_SHARED_LIBS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/plugin")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

find_package(phoenix6 REQUIRED)
set_target_properties(phoenix6 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SYSROOT}/usr/include/phoenix6"
    INTERFACE_LINK_DIRECTORIES "${CMAKE_SYSROOT}/usr/lib/phoenix6"
)

find_package(spdlog REQUIRED)
add_compile_definitions(SPDLOG_DISABLE_DEFAULT_LOGGER)

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

set(USOCKETS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/uWebSockets/uSockets")
file(GLOB USOCKETS_SOURCES
    "${USOCKETS_SOURCE_DIR}/src/*.c"
    "${USOCKETS_SOURCE_DIR}/src/eventing/*.c"
    "${USOCKETS_SOURCE_DIR}/src/crypto/*.c"
)

message("uSocket sources: ${USOCKET_SOURCES}")
add_library(uSockets STATIC ${USOCKETS_SOURCES})
target_include_directories(uSockets PUBLIC "${USOCKETS_SOURCE_DIR}/src")
target_compile_definitions(uSockets PUBLIC
    LIBUS_USE_EPOLL
    LIBUS_NO_SSL=1
)
target_link_libraries(uSockets PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
)
set_target_properties(uSockets PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

add_library(uWebSockets INTERFACE)
target_include_directories(uWebSockets INTERFACE
    external/uWebSockets/src
    external/uWebSockets/uSockets/src
)
target_link_libraries(uWebSockets INTERFACE)

add_subdirectory(external/glaze)


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
    uWebSockets
    uSockets
    glaze::glaze
)

CHECK_VAR(RPI_HOST "Hostname of the raspberry pi")
CHECK_VAR(RPI_USER "User to login on the raspberry pi")
CHECK_VAR(RPI_DEST "Destination path on the raspberry pi for deployment")

add_custom_target(deploy
    COMMAND ssh ${RPI_USER}@${RPI_HOST} "mkdir -p ${RPI_DEST}"
    COMMAND rsync -avz ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ ${RPI_USER}@${RPI_HOST}:${RPI_DEST}/
    COMMENT "Deploying via rsync to ${RPI_USER}@${RPI_HOST}..."
    DEPENDS car_host control read telemetry
)
