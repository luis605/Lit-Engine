add_executable(WorldTests WorldTests.cpp)
target_compile_features(WorldTests PRIVATE cxx_std_23)
target_link_libraries(WorldTests PRIVATE Engine)
add_test(NAME WorldTests COMMAND WorldTests)

add_executable(GpuSyncTests GpuSyncTests.cpp)
target_compile_features(GpuSyncTests PRIVATE cxx_std_23)
target_link_libraries(GpuSyncTests PRIVATE Engine glfw)
