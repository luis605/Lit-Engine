file(GLOB_RECURSE EDITOR_MODULES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cppm"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.ixx"
)

file(GLOB_RECURSE EDITOR_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(Editor ${EDITOR_SOURCES})

# Ensure module interface units are compiled as CXX modules and produced
target_sources(Editor
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        FILES ${EDITOR_MODULES}
)

target_link_libraries(Editor PRIVATE Engine)
target_compile_features(Editor PRIVATE cxx_std_23)
