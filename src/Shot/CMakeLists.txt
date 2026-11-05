cmake_minimum_required(VERSION 3.28)

add_executable(LitShot main.cpp Args.cpp Scenes.cpp GalleryScene.cpp)

target_compile_features(LitShot PRIVATE cxx_std_23)

target_include_directories(LitShot PRIVATE
    "${PROJECT_SOURCE_DIR}/src/vendors/glfw/include"
    "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(LitShot PRIVATE Engine glfw)

add_custom_command(
    TARGET LitShot POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources"
            "${CMAKE_BINARY_DIR}/resources"
)
