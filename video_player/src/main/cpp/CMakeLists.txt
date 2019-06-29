# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)

# 设置生成的so动态库最后输出的目录，CMAKE_SOURCE_DIR就是CMakeLists.txt所在的路径，在gradle中配置
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../../../jniLibs/${CMAKE_ANDROID_ARCH_ABI})

# 设置头文件目录，C文件中include时不需要带路径了
include_directories(${CMAKE_SOURCE_DIR}/include)

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.

# 定义一个SOURCE的全局变量，指向当前目录下的全部cpp文件
file(GLOB SOURCE *.cpp)

add_library( # Sets the name of the library.
        video_player

        # Sets the library as a shared library.
        SHARED

        # Provides a relative path to your source file(s).
        ${SOURCE})

# 设置一个变量ABI_DIR，指向需要使用的第三方库的路径
set(ABI_DIR ${CMAKE_SOURCE_DIR}/../jniLibs/${CMAKE_ANDROID_ARCH_ABI})

# 设置第三方库的路径，可以引入多个库
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -L${ABI_DIR}")

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

# ANativeWindow 需要
find_library( # Sets the name of the path variable.
        android-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        android)

find_library( # Sets the name of the path variable.
        z-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        z)

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
        video_player
        # 使用静态库时需要注意引入顺序问题，比如n.a引用m.a，那么m必须放在n之前，动态库顺序无所谓
        # 这里使用静态库，就要注意avformat要在avcodec前面，动态库顺序无所谓
        # avfilter avformat avcodec avutil swresample swscale
        # 也可以使用下面 -Wl 的方式
        -Wl,--start-group
        avcodec avfilter avformat avutil swresample swscale
        -Wl,--end-group
        # Links the target library to the log library
        # included in the NDK.
        ${log-lib}
        ${android-lib}
        ${z-lib})