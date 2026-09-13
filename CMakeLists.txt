cmake_minimum_required(VERSION 3.31)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
project(auto-club CXX)

include(FetchContent)

add_compile_options(-Wno-psabi -O3)

find_package(phoenix6 REQUIRED)
set_target_properties(phoenix6 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${RPI_SYSROOT}/usr/include/phoenix6"
    INTERFACE_LINK_DIRECTORIES "${RPI_SYSROOT}/usr/lib/phoenix6"
)

find_package(spdlog REQUIRED)
add_subdirectory(external/cpp-httplib)
add_subdirectory(external/json)

add_executable(motor-control
    src/main.cpp
    src/helpers.cpp
    src/server.cpp
    src/read.cpp
    src/control.cpp
)
target_include_directories(motor-control PRIVATE ./include)
target_link_libraries(motor-control 
PRIVATE
    phoenix6
    spdlog::spdlog
    httplib::httplib
    nlohmann_json::nlohmann_json
)

if (NOT DEFINED RPI_HOST)
    message(FATAL_ERROR "RPI_HOST is not set. Please set the RPI_HOST environment variable or pass -DRPI_HOST=...")
endif()
if (NOT DEFINED RPI_USER)
    message(FATAL_ERROR "RPI_USER is not set. Please set the RPI_USER environment variable or pass -DRPI_USER=...")
endif()
if (NOT DEFINED RPI_DEST)
    message(FATAL_ERROR "RPI_DEST is not set. Please set the RPI_DEST environment variable or pass -DRPI_DEST=...")
endif()

set(RPI_HOST ${RPI_HOST} CACHE STRING "Raspberry Pi IP address or hostname")
set(RPI_USER ${RPI_USER} CACHE STRING "Username on the Raspberry Pi")
set(RPI_DEST ${RPI_DEST} CACHE STRING "Destination directory on the Raspberry Pi")

add_custom_target(deploy
    COMMAND ssh ${RPI_USER}@${RPI_HOST} "mkdir -p ${RPI_DEST}"
    COMMAND rsync -avz $<TARGET_FILE:motor-control> ${RPI_USER}@${RPI_HOST}:${RPI_DEST}/
    COMMENT "Deploying motor-control binary via rsync to ${RPI_USER}@${RPI_HOST}..."
    DEPENDS motor-control
)
