# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Werror=return-type -Wl,--hash-style=sysv")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
#set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
#set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} -fsanitize=address")

# Header file directories
include_directories(
        ${CMAKE_SOURCE_DIR}/src/main/inline32
        ${CMAKE_SOURCE_DIR}/src/main/inline64
        ${CMAKE_SOURCE_DIR}/src/main/xDL
        ${CMAKE_SOURCE_DIR}/src/main/xHook
        ${CMAKE_SOURCE_DIR}/src/main/cpp
        ${CMAKE_SOURCE_DIR}/src/main/unwind32
        ${CMAKE_SOURCE_DIR}/src/main/unwind64
)

if (${CMAKE_ANDROID_ARCH_ABI} STREQUAL "arm64-v8a")
    SET(
            # Sets the name of the library.
            PLATFORM_DIFF_CODE

            # And64InlineHook
            src/main/inline64/And64InlineHook.cpp

            # Unwind-64
            src/main/unwind64/backtrace_64.cpp
    )
else()
    SET(
            # Sets the name of the library.
            PLATFORM_DIFF_CODE

            # Android-Inline-Hook
            src/main/inline32/relocate.c
            src/main/inline32/inlineHook.c

            # Unwind-32
            src/main/unwind32/backtrace.c
            src/main/unwind32/map_info.c
            src/main/unwind32/backtrace-arm.c
            src/main/unwind32/ptrace.c
            src/main/unwind32/backtrace-helper.c
    )
endif()

# Source code directories
add_library(
        # Sets the name of the library.
        raphael

        # Sets the library as a shared library.
        SHARED

        # platform diff code
        ${PLATFORM_DIFF_CODE}

        # xDL
        src/main/xDL/xdl.c
        src/main/xDL/xdl_iterate.c
        src/main/xDL/xdl_lzma.c
        src/main/xDL/xdl_util.c

        # xHook
        src/main/xHook/xh_core.c
        src/main/xHook/xh_elf.c
        src/main/xHook/xh_log.c
        src/main/xHook/xh_util.c

        src/main/cpp/AllocPool.hpp
        src/main/cpp/Cache.h
        src/main/cpp/MemoryCache.cpp
        src/main/cpp/MapData.cpp
        src/main/cpp/Raphael.h
        src/main/cpp/Raphael.cpp
        src/main/cpp/xloader.cpp
)

# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.

find_library(
        # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that you want CMake to locate.
        log
)

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries(
        # Specifies the target library.
        raphael

        # Links the target library to the log library included in the NDK.
        -latomic
        ${log-lib}
        -lz
)