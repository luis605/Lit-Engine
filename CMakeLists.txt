# Set C compiler (prefer gcc-11, fallback to gcc)
find_program(COMPILER_C NAMES gcc-11 gcc)
if(NOT COMPILER_C)
    message(FATAL_ERROR "No suitable C compiler found.")
endif()
set(CMAKE_C_COMPILER ${COMPILER_C})
if(COMPILER_C MATCHES "gcc-11")
    add_compile_options(-fno-lto)
    add_link_options(-fno-lto)
endif()

# Set C++ compiler (prefer g++-11, fallback to g++)
find_program(COMPILER_CXX NAMES g++-11 g++)
if(NOT COMPILER_CXX)
    message(FATAL_ERROR "No suitable C++ compiler found.")
endif()
set(CMAKE_CXX_COMPILER ${COMPILER_CXX})
if(COMPILER_CXX MATCHES "g\\+\\+\\-11")
    add_compile_options(-fno-lto)
    add_link_options(-fno-lto)
endif()

# CMake Settings
cmake_minimum_required(VERSION 3.12)
project(LitEngine)

# Global Configuration
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static-libstdc++ -static-libgcc")
set(CMAKE_CXX_FLAGS "-no-pie -fdiagnostics-color=always")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -pg")
set(CMAKE_CXX_COMPILER_LAUNCHER ccache)
set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=gold")
add_definitions(-w -g)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (CMAKE_CXX_STANDARD EQUAL 11)
        add_compile_options(-fno-lto)
        add_link_options(-fno-lto)
    endif()
endif()

# Source Files
set(SRC_FILES
    ${CMAKE_SOURCE_DIR}/Include/ImGuiColorTextEdit/TextEditor.cpp
    ${CMAKE_SOURCE_DIR}/Include/rlImGui.cpp
    ${CMAKE_SOURCE_DIR}/Include/rlFrustum.cpp
    ${CMAKE_SOURCE_DIR}/Include/NodeEditor/crude_json.cpp
    ${CMAKE_SOURCE_DIR}/Include/NodeEditor/imgui_node_editor.cpp
    ${CMAKE_SOURCE_DIR}/Include/NodeEditor/imgui_canvas.cpp
    ${CMAKE_SOURCE_DIR}/Include/NodeEditor/imgui_node_editor_api.cpp
)

# Use GLOB_RECURSE when all headers are fixed
file(GLOB_RECURSE ENGINE_SRC_FILES
    ${CMAKE_SOURCE_DIR}/Engine/*.cpp
    ${CMAKE_SOURCE_DIR}/GameBuilder/builder.cpp
)

list(APPEND SRC_FILES ${ENGINE_SRC_FILES})

file(GLOB IMGUI_SRC_FILES ${CMAKE_SOURCE_DIR}/Include/imgui/*.cpp)
list(APPEND SRC_FILES ${IMGUI_SRC_FILES})

# Platform-Specific Configuration
if(WIN32)
    set(Bullet_DIR "${CMAKE_SOURCE_DIR}/install/bin/vcpkg/installed/x64-mingw-static/share/bullet")
    set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/install/bin/vcpkg/installed/x64-mingw-static/share/ffmpeg")

    find_package(Bullet CONFIG REQUIRED)
    find_package(FFMPEG REQUIRED)

    include_directories(
        ${BULLET_INCLUDE_DIRS}
    )

    set(LIB_FLAGS
        ${FFMPEG_LIBRARIES}
        ${BULLET_LIBRARIES}
        python312
        winmm
    )
elseif(UNIX)
    set(DEBUGGER gdb)
    find_package(X11 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AVFORMAT REQUIRED libavformat)
    pkg_check_modules(AVCODEC REQUIRED libavcodec)
    pkg_check_modules(AVUTIL REQUIRED libavutil)
    pkg_check_modules(SWSCALE REQUIRED libswscale)
    pkg_check_modules(SWRESAMPLE REQUIRED libswresample)
    pkg_check_modules(BULLET REQUIRED bullet)
    pkg_check_modules(LZMA REQUIRED liblzma)

    include_directories(
        ${CMAKE_SOURCE_DIR}/Include/bullet3/src/
        ${AVFORMAT_LIBRARIES}
        ${AVCODEC_LIBRARIES}
        ${AVUTIL_LIBRARIES}
        ${SWSCALE_LIBRARIES}
        ${SWRESAMPLE_LIBRARIES}
        ${LZMA_LIBRARIES}
    )

    link_directories(
        ${CMAKE_SOURCE_DIR}/Include/bullet3/build/libs
    )

    set(LIB_FLAGS
        avformat
        avcodec
        avutil
        swscale
        libBulletDynamics.a
        libBulletCollision.a
        libLinearMath.a
        libpython3.12.a
        OpenCL
        libz.a
        libm.a
        libc.a
        libexpat.a
        libdl.a
        libxcb.a
    )
endif()

execute_process(COMMAND python3.12 -c "import sys; print(sys.prefix + '/include/python3.12')"       OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE PYTHON_INCLUDE_DIR)
execute_process(COMMAND python3.12 -c "import sysconfig; print(sysconfig.get_config_var('LIBDIR'))" OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE PYTHON_LIBS_DIR)

message(STATUS "PYTHON INCLUDE: ${PYTHON_INCLUDE_DIR}")
message(STATUS "PYTHON LIBRARY: ${PYTHON_LIBS_DIR}")

# Include Directories
include_directories(
    ${CMAKE_SOURCE_DIR}/Include
    ${CMAKE_SOURCE_DIR}/Include/raylib/src
    ${CMAKE_SOURCE_DIR}/Include/raylib/src/external/
    ${CMAKE_SOURCE_DIR}/Include/raylib/src/external/glfw/include
    ${CMAKE_SOURCE_DIR}/Include/nlohmann/include
    ${CMAKE_SOURCE_DIR}/Include/imgui
    ${CMAKE_SOURCE_DIR}/Include/ffmpeg
    ${CMAKE_SOURCE_DIR}/Include/bullet3/src
    ${CMAKE_SOURCE_DIR}/Include/meshoptimizer/src
    ${CMAKE_SOURCE_DIR}/Include/pybind11/include
    ${CMAKE_SOURCE_DIR}/Include/ImGuiColorTextEdit
    ${CMAKE_SOURCE_DIR}/Include/NodeEditor
    ${CMAKE_SOURCE_DIR}/Include/NodeEditor/external/stb_image
    ${CMAKE_SOURCE_DIR}/Include/NodeEditor/misc
    ${CMAKE_SOURCE_DIR}/Include/glm/glm
    ${CMAKE_SOURCE_DIR}/Include/squish
    ${PYTHON_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}
)

# Link directories
link_directories(
    ${CMAKE_SOURCE_DIR}/Include/meshoptimizer/build
    ${CMAKE_SOURCE_DIR}/Include/raylib/src
    ${CMAKE_SOURCE_DIR}/Include/squish/build
    ${PYTHON_LIBS_DIR}
    ${FFMPEG_LIBRARY_DIRS}
    ${BULLET_LIBRARY_DIRS}
)

# Library Flags
list(APPEND LIB_FLAGS
    meshoptimizer
    raylib
    pthread
    squish
    -funroll-loops
    -ftree-vectorize
    -fno-math-errno
    -ffast-math
    -freciprocal-math
    -fvect-cost-model
)

add_executable(LitEngine ${SRC_FILES})

target_link_libraries(LitEngine ${LIB_FLAGS})
target_compile_options(LitEngine PRIVATE
    -pipe
    -Wno-unused-result
)

add_custom_target(run
    COMMAND LitEngine
    DEPENDS LitEngine
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(debug
    COMMAND ${CMAKE_COMMAND} -E echo "Make sure you set -DCMAKE_BUILD_TYPE=Debug in your cmake command"
    COMMAND ${DEBUGGER} ${CMAKE_BINARY_DIR}/LitEngine
    DEPENDS LitEngine
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

if(UNIX)
    add_custom_target(profile
        COMMAND ${CMAKE_COMMAND} -E echo "Make sure you set -DCMAKE_BUILD_TYPE=Debug in your cmake command"
        COMMAND ${CMAKE_COMMAND} -E echo "Running LitEngine..."
        COMMAND LitEngine
        COMMAND ${CMAKE_COMMAND} -E echo "Moving gmon.out to binary directory..."
        COMMAND ${CMAKE_COMMAND} -E rename ${CMAKE_SOURCE_DIR}/gmon.out ${CMAKE_BINARY_DIR}/gmon.out
        COMMAND ${CMAKE_COMMAND} -E echo "Profiling with gprof..."
        COMMAND gprof ${CMAKE_BINARY_DIR}/LitEngine ${CMAKE_BINARY_DIR}/gmon.out
        COMMAND ${CMAKE_COMMAND} -E echo "Analysis results is launching..."
        COMMAND gprof ${CMAKE_BINARY_DIR}/LitEngine ${CMAKE_BINARY_DIR}/gmon.out | gprof2dot -w | dot -Tpng -o ${CMAKE_BINARY_DIR}/profiled.png
        DEPENDS LitEngine
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()

list(REMOVE_ITEM SRC_FILES ${CMAKE_SOURCE_DIR}/Engine/main.cpp)

add_executable(MyApp ${SRC_FILES} GameBuilder/gameSystem.cpp)
target_link_libraries(MyApp ${LIB_FLAGS})

add_custom_target(gameBuilder
    COMMAND MyApp
    DEPENDS MyApp
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)