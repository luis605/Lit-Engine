# Attempt to set the C compiler to gcc-11
find_program(GCC_11_PATH NAMES gcc-11)
if(GCC_11_PATH)
    set(CMAKE_C_COMPILER ${GCC_11_PATH})
else()
    # Fallback to gcc if gcc-11 is not found
    find_program(GCC_PATH NAMES gcc)
    if(GCC_PATH)
        set(CMAKE_C_COMPILER ${GCC_PATH})
    else()
        message(FATAL_ERROR "No suitable C compiler found.")
    endif()
endif()

# Attempt to set the C++ compiler to g++-11
find_program(GXX_11_PATH NAMES g++-11)
if(GXX_11_PATH)
    set(CMAKE_CXX_COMPILER ${GXX_11_PATH})
else()
    # Fallback to g++ if g++-11 is not found
    find_program(GXX_PATH NAMES g++)
    if(GXX_PATH)
        set(CMAKE_CXX_COMPILER ${GXX_PATH})
    else()
        message(FATAL_ERROR "No suitable C++ compiler found.")
    endif()
endif()

# CMake Settings
cmake_minimum_required(VERSION 3.12)
project(LitEngine)

# Global Configuration
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static-libstdc++ -static-libgcc")
set(CMAKE_CXX_FLAGS "-no-pie")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -pg")
add_definitions(-w -g)

# Source Files
set(SRC_FILES
    ${CMAKE_SOURCE_DIR}/include/ImGuiColorTextEdit/TextEditor.cpp
    ${CMAKE_SOURCE_DIR}/include/rlImGui.cpp
    ${CMAKE_SOURCE_DIR}/include/NodeEditor/crude_json.cpp
    ${CMAKE_SOURCE_DIR}/include/NodeEditor/imgui_node_editor.cpp
    ${CMAKE_SOURCE_DIR}/include/NodeEditor/imgui_canvas.cpp
    ${CMAKE_SOURCE_DIR}/include/NodeEditor/imgui_node_editor_api.cpp
)

# Use GLOB_RECURSE when all headers are fixed
file(GLOB ENGINE_SRC_FILES
    ${CMAKE_SOURCE_DIR}/Engine/*.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Core/Events.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Core/Textures.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Core/PythonLinkIssueWorkaround.cpp
)
list(APPEND SRC_FILES ${ENGINE_SRC_FILES})

# Debugger Configuration
if(UNIX)
    set(DEBUGGER gdb)
endif()

# Platform-Specific Configuration
if(WIN32)
    set(Bullet_DIR "${CMAKE_SOURCE_DIR}/install/bin/vcpkg/installed/x64-mingw-static/share/bullet")
    set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/install/bin/vcpkg/installed/x64-mingw-static/share/ffmpeg")

    find_package(Bullet CONFIG REQUIRED)
    find_package(FFMPEG REQUIRED)

    message(STATUS "BULLET INCLUDE: ${Bullet_DIR}")
    message(STATUS "BULLET LIBRARIES: ${BULLET_LIBRARIES}")
    message(STATUS "BULLET INCLUDE DIR: ${BULLET_INCLUDE_DIRS}")
    message(STATUS "BULLET LIBRARIES DIR: ${BULLET_LIBRARY_DIRS}")

    include_directories(
        ${BULLET_INCLUDE_DIRS}
    )
endif()

if(UNIX)
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
        ${CMAKE_SOURCE_DIR}/include/bullet3/src/
        ${AVFORMAT_LIBRARIES}
        ${AVCODEC_LIBRARIES}
        ${AVUTIL_LIBRARIES}
        ${SWSCALE_LIBRARIES}
        ${SWRESAMPLE_LIBRARIES}
        ${CMAKE_SOURCE_DIR}/include/raylib/src/
        ${LZMA_LIBRARIES}
        ${CMAKE_SOURCE_DIR}/include/meshoptimizer/build/
    )
endif()

# Python Configuration
execute_process(
    COMMAND python3.12 -c "import sys; print(sys.prefix + '/include/python3.12')"
    OUTPUT_VARIABLE PYTHON_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND python3.12 -c "import sysconfig; print(sysconfig.get_config_var('LIBDIR'))"
    OUTPUT_VARIABLE PYTHON_LIBS_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

message(STATUS "PYTHON INCLUDE: ${PYTHON_INCLUDE_DIR}")
message(STATUS "PYTHON LIBRARY: ${PYTHON_LIBS_DIR}")

# Dear ImGui Files
file(GLOB IMGUI_SRC_FILES
    ${CMAKE_SOURCE_DIR}/include/imgui/*.cpp
)

# Include Directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/raylib/src
    ${CMAKE_SOURCE_DIR}/include/raylib/src/external/
    ${CMAKE_SOURCE_DIR}/include/raylib/src/external/glfw/include
    ${CMAKE_SOURCE_DIR}/include/nlohmann/include
    ${CMAKE_SOURCE_DIR}/include/imgui
    ${CMAKE_SOURCE_DIR}/include/ffmpeg
    ${CMAKE_SOURCE_DIR}/include/bullet3/src
    ${CMAKE_SOURCE_DIR}/include/meshoptimizer/src
    ${CMAKE_SOURCE_DIR}/include/pybind11/include
    ${CMAKE_SOURCE_DIR}/include/ImGuiColorTextEdit
    ${CMAKE_SOURCE_DIR}/include/NodeEditor
    ${CMAKE_SOURCE_DIR}/include/NodeEditor/external/stb_image
    ${CMAKE_SOURCE_DIR}/include/NodeEditor/misc
    ${PYTHON_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}
)

# Link directories
link_directories(
    ${CMAKE_SOURCE_DIR}/include/meshoptimizer/build
    ${CMAKE_SOURCE_DIR}/include/raylib/src
    ${PYTHON_LIBS_DIR}
    ${FFMPEG_LIBRARY_DIRS}
    ${BULLET_LIBRARY_DIRS}
    ${CMAKE_SOURCE_DIR}/include/libs/linux
)

# Library Flags
if(UNIX)
    link_directories(
        ${CMAKE_SOURCE_DIR}/include/bullet3/build/libs
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
else()
    set(LIB_FLAGS
        ${FFMPEG_LIBRARIES}
        ${BULLET_LIBRARIES}
        python312
    )
endif()

list(APPEND LIB_FLAGS
    meshoptimizer
    raylib
    pthread
    -funroll-loops
    -ftree-vectorize
    -fno-math-errno
    -freciprocal-math
    -fvect-cost-model
)

if(WIN32)
    list(APPEND LIB_FLAGS
        winmm
    )
endif()

add_executable(LitEngine ${SRC_FILES} ${IMGUI_SRC_FILES})

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
        COMMAND ${CMAKE_COMMAND} -E echo "Running LitEngine..."
        COMMAND LitEngine
        COMMAND ${CMAKE_COMMAND} -E echo "Moving gmon.out to binary directory..."
        COMMAND ${CMAKE_COMMAND} -E rename ${CMAKE_SOURCE_DIR}/gmon.out ${CMAKE_BINARY_DIR}/gmon.out
        COMMAND ${CMAKE_COMMAND} -E echo "Profiling with gprof..."
        COMMAND gprof ${CMAKE_BINARY_DIR}/LitEngine ${CMAKE_BINARY_DIR}/gmon.out > ${CMAKE_BINARY_DIR}/analysis.txt
        COMMAND ${CMAKE_COMMAND} -E echo "Analysis results:"
        COMMAND cat ${CMAKE_BINARY_DIR}/analysis.txt
        DEPENDS LitEngine
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()



############# GAME BUILDER #############

list(REMOVE_ITEM SRC_FILES ${CMAKE_SOURCE_DIR}/Engine/main.cpp)

add_executable(MyApp ${SRC_FILES} ${IMGUI_SRC_FILES} GameBuilder/gameSystem.cpp)
target_link_libraries(MyApp ${LIB_FLAGS})

add_custom_target(gameBuilder
    COMMAND MyApp
    DEPENDS MyApp
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
