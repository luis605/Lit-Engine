cmake_minimum_required(VERSION 3.28)

add_executable(Sandbox)

target_sources(Sandbox
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
            Scene.cppm
    PRIVATE
        Scene.cpp
        main.cpp
)

target_compile_features(Sandbox PRIVATE cxx_std_23)

target_include_directories(Sandbox PRIVATE
    "${PROJECT_SOURCE_DIR}/src/vendors/glfw/include"
    "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(Sandbox PRIVATE Engine glfw)

add_custom_command(
    TARGET Sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources"
            "${CMAKE_BINARY_DIR}/resources"
)
