cmake_minimum_required(VERSION 3.12)
project(LitEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static-libstdc++ -static-libgcc")
set (CMAKE_CXX_FLAGS "-no-pie")

add_definitions(-w)

set(SRC_FILES
    include/ImGuiColorTextEdit/TextEditor.cpp
    include/rlImGui.cpp
    include/ImNodes/ImNodes.cpp
    include/ImNodes/ImNodesEz.cpp
)

# Use GLOB_RECURSE when all headers are fixed
file(GLOB ENGINE_SRC_FILES
    ${CMAKE_SOURCE_DIR}/Engine/*.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Core/Events.cpp
)


list(APPEND SRC_FILES ${ENGINE_SRC_FILES})

message(STATUS "Engine Source Files: ${ENGINE_SRC_FILES}")


if(UNIX)
    set(DEBUGGER gdb)
endif()

if(WIN32)
    set(Bullet_DIR "${CMAKE_SOURCE_DIR}/install/bin/vcpkg/installed/x64-mingw-static/share/bullet")
    set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/install/bin/vcpkg/installed/x64-mingw-static/share/ffmpeg")

    find_package(Bullet CONFIG REQUIRED)
    find_package(FFMPEG REQUIRED)
endif()

if(UNIX)
    find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
    find_package(X11 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AVFORMAT REQUIRED libavformat)
    pkg_check_modules(AVCODEC REQUIRED libavcodec)
    pkg_check_modules(AVUTIL REQUIRED libavutil)
    pkg_check_modules(SWSCALE REQUIRED libswscale)
    pkg_check_modules(SWRESAMPLE REQUIRED libswresample)
    pkg_check_modules(RAYLIB REQUIRED raylib)
    pkg_check_modules(BULLET REQUIRED bullet)
    pkg_check_modules(LZMA REQUIRED liblzma)
endif()

execute_process(
    COMMAND python -c "import sys; print(sys.prefix + '/include')"
    OUTPUT_VARIABLE PYTHON_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND python -c "import sys; print(sys.prefix + '/libs')"
    OUTPUT_VARIABLE PYTHON_LIBS_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(WIN32)
    message(STATUS "Bullet_DIR: ${Bullet_DIR}")
    message(STATUS "BULLET_LIBRARIES: ${BULLET_LIBRARIES}")
    message(STATUS "BULLET_INCLUDE_DIRS: ${BULLET_INCLUDE_DIRS}")
    message(STATUS "BULLET_LIBRARY_DIRS: ${BULLET_LIBRARY_DIRS}")
endif()

message(STATUS "PYTHON DIR: ${PYTHON_INCLUDE_DIR}")

file(GLOB IMGUI_SRC_FILES
    ${CMAKE_SOURCE_DIR}/include/imgui/*.cpp
)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/raylib/src
    ${CMAKE_SOURCE_DIR}/include/raylib/src/external/glfw/include
    ${PYTHON_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/include/nlohmann/include
    ${CMAKE_SOURCE_DIR}/include/imgui
    ${CMAKE_SOURCE_DIR}/include/ffmpeg
    ${CMAKE_SOURCE_DIR}/include/bullet3/src
    ${CMAKE_SOURCE_DIR}/include/meshoptimizer/src
    ${CMAKE_SOURCE_DIR}/include/pybind11/include
    ${CMAKE_SOURCE_DIR}/include/ImGuiColorTextEdit
    ${CMAKE_SOURCE_DIR}/include/ImNodes
    ${Python3_INCLUDE_DIRS}
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

if(UNIX)
    set(LIB_FLAGS
        avformat
        avcodec
        avutil
        swscale
        libBulletDynamics.a
        libBulletCollision.a
        libLinearMath.a
        python3.11.a
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
        python311
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

if(UNIX)
    include_directories(
        ${Python3_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/include/bullet3/src
        ${AVFORMAT_LIBRARIES}
        ${AVCODEC_LIBRARIES}
        ${AVUTIL_LIBRARIES}
        ${SWSCALE_LIBRARIES}
        ${SWRESAMPLE_LIBRARIES}
        ${CMAKE_SOURCE_DIR}/include/raylib/src/libraylib.a
        ${LZMA_LIBRARIES}
        ${CMAKE_SOURCE_DIR}/include/meshoptimizer/build/libmeshoptimizer.a
        )
elseif(WIN32)
    list(APPEND LIB_FLAGS
        winmm
    )
    include_directories(${BULLET_INCLUDE_DIRS})
endif()

add_executable(LitEngine ${SRC_FILES} ${IMGUI_SRC_FILES})

target_compile_definitions(LitEngine PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)

# Link libraries with flags based on the build type
target_link_libraries(LitEngine ${LIB_FLAGS})
target_compile_options(LitEngine PRIVATE
    -pipe
    -Wno-unused-result
)

# Set the different build types
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -pg")

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

add_executable(MyApp GameBuilder/gameSystem.cpp)
target_link_libraries(MyApp ${LIB_FLAGS})

add_custom_target(gameBuilder
    COMMAND MyApp
    DEPENDS MyApp
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
