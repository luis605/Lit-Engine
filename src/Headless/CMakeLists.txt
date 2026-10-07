cmake_minimum_required(VERSION 3.28)

add_executable(Headless main.cpp)
target_compile_features(Headless PRIVATE cxx_std_23)
target_link_libraries(Headless PRIVATE Engine)
