cmake_minimum_required(VERSION 3.28)
project(LitEngine CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fdiagnostics-color=always)
endif()

add_subdirectory(src/Engine)
add_subdirectory(src/Editor)

add_custom_target(run
    DEPENDS Editor
    COMMAND ${CMAKE_COMMAND} -E echo "Running Editor..."
    COMMAND $<TARGET_FILE:Editor>
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Builds and runs the Editor executable"
)
