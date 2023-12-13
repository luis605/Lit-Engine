cmake_minimum_required(VERSION 3.15)
project(LitEngine)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_WIN32_EXECUTABLE OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w -g")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")

# Define source files
set(SRC_FILES
        main.cpp
        ImGuiColorTextEdit/TextEditor.o
        include/rlImGui.o
        ImNodes/ImNodes.o
        ImNodes/ImNodesEz.o)

# Include directories

execute_process(
    COMMAND python -c "import sys; print(sys.prefix + '/include')"
    OUTPUT_VARIABLE PYTHON_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

include_directories(
        ./include
        ./include/nlohmann
        ./imgui
        ./include/bullet3/src
        ./ffmpeg
        ./ImGuiColorTextEdit
        ${PYTHON_INCLUDE_DIR}
        )

include_directories("C:/Users/USER/AppData/Local/Programs/Python/Python311/Lib/site-packages/pybind11/include")

# Set the Python version explicitly
find_package(Python3 REQUIRED COMPONENTS Development)
include_directories(${Python3_INCLUDE_DIRS})

# Build Dear ImGui objects
file(GLOB IMGUI_SOURCES imgui/*.cpp)
foreach (IMGUI_SOURCE ${IMGUI_SOURCES})
    get_filename_component(IMGUI_SOURCE_NAME ${IMGUI_SOURCE} NAME_WE)  # Use NAME_WE to get the name without extension
    set(IMGUI_OBJECT "imgui/${IMGUI_SOURCE_NAME}.o")
    add_custom_command(
            OUTPUT ${IMGUI_OBJECT}
            COMMAND ${CMAKE_CXX_COMPILER} ${CMAKE_CXX_FLAGS} -DIMGUI_IMPL_OPENGL_LOADER_GLAD -c ${IMGUI_SOURCE} -o ${IMGUI_OBJECT}
            DEPENDS ${IMGUI_SOURCE}
    )
    list(APPEND IMGUI_OBJECTS ${IMGUI_OBJECT})
endforeach ()

# Run Python to get pybind11 includes
execute_process(
  COMMAND python -m pybind11 --includes
  OUTPUT_VARIABLE PYBIND11_INCLUDES
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Set the include directories
include_directories(${PYBIND11_INCLUDES})

link_directories(${CMAKE_SOURCE_DIR}/include)
link_directories(${CMAKE_SOURCE_DIR}/include/bullet3/lib)
link_directories(${CMAKE_SOURCE_DIR}/include/ffmpeg/lib)
link_directories(${CMAKE_SOURCE_DIR}/include/ffmpeg/bin)

# Check if the OS is Windows

if (WIN32)
    message("WIN32 Called")
    set(LIB_FLAGS "-lraylib -LC:/Users/USER/AppData/Local/Programs/Python/Python311/libs -lpython311 -fPIC")
    execute_process(COMMAND python -m pybind11 --includes OUTPUT_VARIABLE PYBIND11_INCLUDES)
    string(STRIP "${PYBIND11_INCLUDES}" PYBIND11_INCLUDES)
    set(LIB_FLAGS "${LIB_FLAGS} ${PYBIND11_INCLUDES} -L. -lBulletDynamics -lBulletCollision -lLinearMath -I./include/bullet3/src -lavformat -lavcodec -lavutil -lswscale -lopengl32 -lgdi32 -lwinmm -lpthread")
else ()
    set(LIB_FLAGS "-lraylib -lpython3.11 -fPIC")
    execute_process(COMMAND python3.11 -m pybind11 --includes OUTPUT_VARIABLE PYBIND11_INCLUDES)
    string(STRIP "${PYBIND11_INCLUDES}" PYBIND11_INCLUDES)
    set(LIB_FLAGS "${LIB_FLAGS} ${PYBIND11_INCLUDES} -ldl -lBulletDynamics -lBulletCollision -lLinearMath -I./include/bullet3/src -lavformat -lavcodec -lavutil -lswscale -lz -lm -lpthread -ldrm")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fuse-ld=gold")
endif ()


# Build Lit Engine executable
add_executable(lit_engine ${SRC_FILES} ${IMGUI_OBJECTS})
target_link_libraries(lit_engine avcodec meshoptimizer)
target_link_libraries(lit_engine ${LIB_FLAGS})

# Custom targets
add_custom_target(run COMMAND ./lit_engine)
add_custom_target(build_and_run COMMAND make --no-print-directory build -j8 && make --no-print-directory run)
add_custom_target(debug COMMAND gdb lit_engine)
add_custom_target(bdb DEPENDS build debug)

# Build tests
# add_executable(tests tests.cpp include/rlImGui.o ${IMGUI_OBJECTS})
# target_link_libraries(tests ${LIB_FLAGS})
# add_custom_target(build_tests COMMAND ccache ${CMAKE_CXX_COMPILER} ${CMAKE_CXX_FLAGS} tests.cpp include/rlImGui.o ${IMGUI_OBJECTS} ${LIB_FLAGS} -o tests.out)
# add_custom_target(run_tests COMMAND make --no-print-directory build_tests -j10 && ./tests.out)

# Build sandbox
# add_executable(sandbox sandbox.cpp ${IMGUI_OBJECTS})
# target_link_libraries(sandbox ${LIB_FLAGS})
# add_custom_target(build_sandbox COMMAND ccache ${CMAKE_CXX_COMPILER} ${CMAKE_CXX_FLAGS} sandbox.cpp  ${IMGUI_OBJECTS} ${LIB_FLAGS} -o sandbox.out)
# add_custom_target(run_sandbox COMMAND ./sandbox.out)

# Build dependencies
add_custom_target(build_dependencies COMMAND
        ${CMAKE_CXX_COMPILER} -c ${IMGUI_OBJECTS} -I./imgui -O3 ImNodes/ImNodes.cpp -o ImNodes/ImNodes.o
        COMMAND ${CMAKE_CXX_COMPILER} -c ${IMGUI_OBJECTS} -I./imgui -O3 ImNodes/ImNodesEz.cpp -o ImNodes/ImNodesEz.o
        COMMAND ${CMAKE_CXX_COMPILER} -c ${IMGUI_OBJECTS} -I./imgui -O3 include/rlImGui.cpp -o include/rlImGui.o
        COMMAND ${CMAKE_CXX_COMPILER} -c -O3 include/rlFrustum.cpp -o include/rlFrustum.o
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Clean targets
add_custom_target(clean_all COMMAND find . -name "*.out" | xargs rm -f && find . -name "*.o" | xargs rm -f)
add_custom_target(clean_depend COMMAND find . -name "*.o" | xargs rm -f)