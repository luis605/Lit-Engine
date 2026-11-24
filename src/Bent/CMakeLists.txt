cmake_minimum_required(VERSION 3.28)

# `bent`: property-based testing and fuzzing of the engine against laws written in .bent files (docs/Bent.md).
add_executable(bent main.cpp)
target_compile_features(bent PRIVATE cxx_std_23)
target_link_libraries(bent PRIVATE BentEngine)
