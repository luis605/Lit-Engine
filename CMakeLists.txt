cmake_minimum_required(VERSION 3.15)
project(LitEngine)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_WIN32_EXECUTABLE OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")

# Define source files
set(SRC_FILES
        main.cpp
        include/ImGuiColorTextEdit/TextEditor.o
        include/rlImGui.o
        include/ImNodes/ImNodes.o
        include/ImNodes/ImNodesEz.o
)
    
execute_process(
    COMMAND python -c "import sys; print(sys.prefix + '/include')"
    OUTPUT_VARIABLE PYTHON_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

find_package(Python3 REQUIRED COMPONENTS Development)

include_directories(
        ./include
        ./include/raylib/src
        ./include/nlohmann/include
        ./include/imgui
        ./include/bullet3/src
        ./include/ffmpeg
        ./include/pybind11/include
        ./include/ImGuiColorTextEdit
        ./include/pybind11
        ${PYTHON_INCLUDE_DIR}
)

# Build Dear ImGui objects
file(GLOB IMGUI_SOURCES include/imgui/*.cpp)
foreach (IMGUI_SOURCE ${IMGUI_SOURCES})
    get_filename_component(IMGUI_SOURCE_NAME ${IMGUI_SOURCE} NAME_WE)  # Use NAME_WE to get the name without extension
    set(IMGUI_OBJECT "include/imgui/${IMGUI_SOURCE_NAME}.o")
    add_custom_command(
            OUTPUT ${IMGUI_OBJECT}
            COMMAND ${CMAKE_CXX_COMPILER} ${CMAKE_CXX_FLAGS} -DIMGUI_IMPL_OPENGL_LOADER_GLAD -c ${IMGUI_SOURCE} -o ${IMGUI_OBJECT}
            DEPENDS ${IMGUI_SOURCE}
    )
    list(APPEND IMGUI_OBJECTS ${IMGUI_OBJECT})
endforeach ()

set(LIB_FLAGS "-fPIC -L. -L./libs -v")

# Build Lit Engine executable
add_executable(lit_engine ${SRC_FILES} ${IMGUI_OBJECTS})

target_link_libraries(lit_engine
    raylib
    bcrypt
    winmm
    strmiids
    avcodec
    avformat
    avutil
    swscale
    swresample
    opengl32
    gdi32
    pthread
    python311
    meshoptimizer
    BulletCollision
    BulletDynamics
    LinearMath
    ${LIB_FLAGS}
)

# Custom targets
add_custom_target(run COMMAND ./lit_engine)
add_custom_target(brun COMMAND make --no-print-directory build -j8 && make --no-print-directory run)
add_custom_target(debug COMMAND gdb lit_engine)
add_custom_target(bdb DEPENDS build debug)

# Clean targets
add_custom_target(clean_all COMMAND find . -name "*.out" | xargs rm -f && find . -name "*.o" | xargs rm -f)