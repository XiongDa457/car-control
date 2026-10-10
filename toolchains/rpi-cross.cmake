set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/check_var.cmake)

CHECK_VAR(TOOLCHAIN_DIR "Path to cross-compilation toolchain")
CHECK_VAR(TARGET_TRIPLE "Target triple (ex. aarch64-none-linux-gnu)")
set(CMAKE_C_COMPILER ${TOOLCHAIN_DIR}/bin/${TARGET_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_DIR}/bin/${TARGET_TRIPLE}-g++)

CHECK_VAR(RPI_SYSROOT "Path to Raspberry Pi 5 sysroot")

list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES RPI_SYSROOT TOOLCHAIN_DIR TARGET_TRIPLE)

set(CMAKE_SYSROOT ${RPI_SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${RPI_SYSROOT})

set(CMAKE_CXX_FLAGS "-nostdinc++ \
    -isystem ${RPI_SYSROOT}/usr/include/c++/14 \
    -isystem ${RPI_SYSROOT}/usr/include/aarch64-linux-gnu/c++/14" CACHE STRING "" FORCE)

set(MULTIARCH_PATH "${RPI_SYSROOT}/usr/lib/aarch64-linux-gnu")
set(GCC_14_PATH "${RPI_SYSROOT}/usr/lib/gcc/aarch64-linux-gnu/14")
set(GLOBAL_LINK_FLAGS "--sysroot=${RPI_SYSROOT} \
    -B${MULTIARCH_PATH} -L${MULTIARCH_PATH} \
    -B${GCC_14_PATH} -L${GCC_14_PATH} \
    -Wl,-rpath-link=${MULTIARCH_PATH}")

if(DEFINED USE_ADDRESS_SANITIZER)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    set(GLOBAL_LINK_FLAGS "${GLOBAL_LINK_FLAGS} -fsanitize=address")
endif()

set(CMAKE_C_LINK_FLAGS ${GLOBAL_LINK_FLAGS} CACHE STRING "" FORCE)
set(CMAKE_CXX_LINK_FLAGS "${GLOBAL_LINK_FLAGS} -nostdlib++ -lstdc++" CACHE STRING "" FORCE)

include_directories(
    ${RPI_SYSROOT}/usr/include
    ${RPI_SYSROOT}/usr/include/aarch64-linux-gnu
)

set(ENV{PKG_CONFIG_DIR} "")
set(ENV{PKG_CONFIG_LIBDIR} "${MULTIARCH_PATH}/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)