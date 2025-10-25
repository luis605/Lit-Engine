cmake_minimum_required(VERSION 3.28)

add_executable(Editor)

set_target_properties(Editor PROPERTIES
    CXX_MODULE_STD ON
)

target_sources(Editor
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
            Application.cppm
    PRIVATE
        Application/Application.cpp
        main.cpp
        ${PROJECT_SOURCE_DIR}/src/vendors/glad/src/glad.c
)

target_compile_features(Editor PRIVATE cxx_std_23)

target_include_directories(Editor PRIVATE
    "${PROJECT_SOURCE_DIR}/src/vendors/glfw/include"
    "${PROJECT_SOURCE_DIR}/src/vendors/glad/include"
)

target_link_libraries(Editor PRIVATE Engine glfw)

set(RESOURCES_DIR "${CMAKE_SOURCE_DIR}/resources")
set(DESTINATION_DIR "${CMAKE_BINARY_DIR}/shaders")

add_custom_command(
    TARGET Editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${RESOURCES_DIR}/shaders"
            "${DESTINATION_DIR}"
)