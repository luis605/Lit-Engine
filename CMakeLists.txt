cmake_minimum_required(VERSION 3.12)
project(LitEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(BUILD_SHARED_LIBS OFF)

add_definitions(-w)

set(SRC_FILES
    Engine/main.cpp
    include/ImGuiColorTextEdit/TextEditor.cpp
    include/rlImGui.cpp
    include/ImNodes/ImNodes.cpp
    include/ImNodes/ImNodesEz.cpp
)

if(UNIX)
    set(DEBUGGER gdb)
endif()

if(WIN32)
    set(Bullet_DIR "${CMAKE_SOURCE_DIR}/install/bin/vcpkg/installed/x64-mingw-static/share/bullet")
    set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/install/bin/vcpkg/installed/x64-mingw-static/share/ffmpeg")
endif()

find_package(Bullet CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)

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

message(STATUS "Bullet_DIR: ${Bullet_DIR}")
message(STATUS "BULLET_LIBRARIES: ${BULLET_LIBRARIES}")
message(STATUS "BULLET_INCLUDE_DIRS: ${BULLET_INCLUDE_DIRS}")
message(STATUS "BULLET_LIBRARY_DIRS: ${BULLET_LIBRARY_DIRS}")
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
    ${CMAKE_SOURCE_DIR}/include/meshoptimizer/src
    ${CMAKE_SOURCE_DIR}/include/pybind11/include
    ${CMAKE_SOURCE_DIR}/include/ImGuiColorTextEdit
    ${CMAKE_SOURCE_DIR}/include/ImNodes
)

# Link directories
link_directories(
    ${CMAKE_SOURCE_DIR}/include/meshoptimizer/build
    ${CMAKE_SOURCE_DIR}/include/raylib/src
    ${PYTHON_LIBS_DIR}
    ${FFMPEG_LIBRARY_DIRS}
    ${BULLET_LIBRARY_DIRS}
)

set(LIB_FLAGS
    meshoptimizer
    raylib
    pthread
    ${BULLET_LIBRARIES}
    python311
    ${FFMPEG_LIBRARIES}
    -funroll-loops
    -ftree-vectorize
    -fno-math-errno
    -freciprocal-math
    -fvect-cost-model
)

if(UNIX)
    find_package(Python3 COMPONENTS Interpreter Development)
    include_directories(
        ${Python3_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/include/bullet3/src
    )
    list(APPEND LIB_FLAGS ${Python3_LIBRARIES})
elseif(WIN32)
    list(APPEND LIB_FLAGS
        winmm
        ${BULLET_LIBRARIES}    
    )
    include_directories(${BULLET_INCLUDE_DIRS})
endif()

add_executable(LitEngine ${SRC_FILES} ${IMGUI_SRC_FILES})
add_executable(LitEngineDebug ${SRC_FILES} ${IMGUI_SRC_FILES})
if(UNIX)
    add_executable(LitEngineProfile ${SRC_FILES} ${IMGUI_SRC_FILES})
endif()

target_compile_definitions(LitEngine PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)
target_compile_definitions(LitEngineDebug PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)
if(UNIX)
    target_compile_definitions(LitEngineProfile PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)
endif()

target_link_libraries(LitEngine ${LIB_FLAGS} -O3)
target_link_libraries(LitEngineDebug ${LIB_FLAGS} -g -O0)
if(UNIX)
    target_link_libraries(LitEngineProfile ${LIB_FLAGS} -pg)
endif()

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
    COMMAND ${DEBUGGER} ${CMAKE_BINARY_DIR}/LitEngineDebug
    DEPENDS LitEngineDebug
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

if(UNIX)
    add_custom_target(profile
        COMMAND LitEngineProfile
        DEPENDS LitEngineProfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMAND gprof ${CMAKE_BINARY_DIR}/LitEngineProfile gmon.out > analysis.txt
        COMMAND cat analysis.txt
    )
endif()