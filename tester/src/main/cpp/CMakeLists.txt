# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.10.2)

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.

# 设置源码文件
# https://stackoverflow.com/questions/3201154/automatically-add-all-files-in-a-folder-to-a-target-using-cmake/3201211#3201211
# seems CONFIGURE_DEPENDS not work in androidstudio , todo custom command to touch this file
file(GLOB SOURCES CONFIGURE_DEPENDS
        "*.cpp"
        "jni/*.cpp"
        )

# 设置头文件
set(include_dir
        jni
        )


# Specifies a path to native header files.
include_directories(
        ${include_dir}
)

add_library( # Sets the name of the library.
        tester

        # Sets the library as a shared library.
        SHARED

        # Provides a relative path to your source file(s).
        ${SOURCES})

# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.

find_library( # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log)

find_library(
        android-lib

        android
)

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
        tester

        # Links the target library to the log library
        # included in the NDK.
        ${log-lib}
        ${android-lib})