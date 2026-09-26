cmake_minimum_required(VERSION 3.28)

add_executable(Editor)

target_sources(Editor
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
            Application.cppm
            Context.cppm
            Commands.cppm
            Viewport.cppm
            Palette.cppm
            Hud.cppm
            Look.cppm
            Autopilot.cppm
            Inspector/Inspector.cppm
            Gizmo/Gizmo.cppm
            Hierarchy/Hierarchy.cppm
    PRIVATE
        Application/Application.cpp
        Context/Context.cpp
        Commands/Commands.cpp
        Viewport/Viewport.cpp
        Palette/Palette.cpp
        Hud/Hud.cpp
        Look/Look.cpp
        Autopilot/Autopilot.cpp
        Inspector/Inspector.cpp
        Gizmo/Gizmo.cpp
        Hierarchy/Hierarchy.cpp
        main.cpp

)

target_compile_features(Editor PRIVATE cxx_std_23)

target_include_directories(Editor PRIVATE
    "${PROJECT_SOURCE_DIR}/src/vendors/glfw/include"

    "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(Editor PRIVATE Engine glfw)
target_compile_definitions(Editor PRIVATE LIT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

set(RESOURCES_DIR "${CMAKE_SOURCE_DIR}/resources")
set(DESTINATION_DIR "${CMAKE_BINARY_DIR}/resources")

add_custom_command(
    TARGET Editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_newer
            "${RESOURCES_DIR}"
            "${DESTINATION_DIR}"
)