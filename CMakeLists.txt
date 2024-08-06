cmake_minimum_required(VERSION 3.12)
project(LitEngine)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define source files
set(SRC_FILES
    include/ImGuiColorTextEdit/TextEditor.cpp
    include/rlImGui.cpp
    include/ImNodes/ImNodes.cpp
    include/ImNodes/ImNodesEz.cpp
)

if(UNIX)
    set(DEBUGGER gdb)
endif()

file(GLOB IMGUI_SRC_FILES
    ${CMAKE_SOURCE_DIR}/include/imgui/*.cpp
)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/ImGuiColorTextEdit
    ${CMAKE_SOURCE_DIR}/include/ImNodes
    ${CMAKE_SOURCE_DIR}/include/nlohmann/include
    ${CMAKE_SOURCE_DIR}/include/imgui
    ${CMAKE_SOURCE_DIR}/include/bullet3/src
    ${CMAKE_SOURCE_DIR}/include/raylib/src
    ${CMAKE_SOURCE_DIR}/include/meshoptimizer/src
    ${CMAKE_SOURCE_DIR}/include/pybind11/include
)

# Link directories
link_directories(
    ${CMAKE_SOURCE_DIR}/include/meshoptimizer/build
)

# Libraries to link against
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWSCALE REQUIRED libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED libswresample)
pkg_check_modules(RAYLIB REQUIRED raylib)
pkg_check_modules(BULLET REQUIRED bullet)
pkg_check_modules(LZMA REQUIRED liblzma)

set(LIB_FLAGS
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
    ${CMAKE_SOURCE_DIR}/include/raylib/src/libraylib.a
    ${BULLET_LIBRARIES}
    ${LZMA_LIBRARIES}
    pthread
    ${CMAKE_SOURCE_DIR}/include/meshoptimizer/build/libmeshoptimizer.a
)

if(UNIX)
    # Assuming Python 3.11 is installed
    find_package(Python3 COMPONENTS Interpreter Development)
    include_directories(${Python3_INCLUDE_DIRS})
    list(APPEND LIB_FLAGS ${Python3_LIBRARIES})
endif()

# Add source files and create executable
add_executable(LitEngine ${SRC_FILES} ${IMGUI_SRC_FILES} Engine/main.cpp)
add_executable(LitEngineProfile ${SRC_FILES} ${IMGUI_SRC_FILES} Engine/main.cpp)
add_executable(LitEngineDebug ${SRC_FILES} ${IMGUI_SRC_FILES} Engine/main.cpp)

target_link_libraries(LitEngine ${LIB_FLAGS} -O3 -funroll-loops -ftree-vectorize -fno-math-errno -freciprocal-math -fvect-cost-model -fgraphite-identity)
target_link_libraries(LitEngineProfile ${LIB_FLAGS} -pg)
target_link_libraries(LitEngineDebug ${LIB_FLAGS} -g -O0)

# Optionally add compilation flags
target_compile_options(LitEngine PRIVATE
    -pipe
    -Wno-unused-result
)

# Add custom commands for run, profile, debug
add_custom_target(run
    COMMAND LitEngine
    DEPENDS LitEngine
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(debug
    COMMAND ${DEBUGGER} ${CMAKE_BINARY_DIR}/LitEngineDebug
    DEPENDS LitEngineDebug
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(profile
    COMMAND LitEngineProfile
    DEPENDS LitEngineProfile
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND gprof ${CMAKE_BINARY_DIR}/LitEngineProfile gmon.out > analysis.txt
    COMMAND cat analysis.txt
)