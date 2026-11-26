add_executable(WorldTests
    WorldTests.cpp
    WorldEntityTests.cpp
    WorldSpatialTests.cpp
    WorldSpatialFarTests.cpp
    WorldSerializationTests.cpp
    WorldRobustnessTests.cpp
    WorldRuntimeTests.cpp
    WorldPhysicsAnimationTests.cpp
    WorldEditorToolsTests.cpp)
target_compile_features(WorldTests PRIVATE cxx_std_23)
target_link_libraries(WorldTests PRIVATE Engine)
add_test(NAME WorldTests COMMAND WorldTests)

add_executable(AssetCacheTests AssetCacheTests.cpp)
target_compile_features(AssetCacheTests PRIVATE cxx_std_23)
target_link_libraries(AssetCacheTests PRIVATE Engine)
add_test(NAME AssetCacheTests COMMAND AssetCacheTests)

add_executable(MeshletTests MeshletTests.cpp)
target_compile_features(MeshletTests PRIVATE cxx_std_23)
target_link_libraries(MeshletTests PRIVATE Engine)
add_test(NAME MeshletTests COMMAND MeshletTests)

add_executable(PortalTests PortalTests.cpp)
target_compile_features(PortalTests PRIVATE cxx_std_23)
target_link_libraries(PortalTests PRIVATE Engine)
add_test(NAME PortalTests COMMAND PortalTests)

add_executable(GpuSyncTests GpuSyncTests.cpp)
target_compile_features(GpuSyncTests PRIVATE cxx_std_23)
target_link_libraries(GpuSyncTests PRIVATE Engine glfw)
add_test(NAME GpuSyncTests COMMAND GpuSyncTests WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(ToolingTests
    ToolingTests.cpp
    ToolingRngJsonTests.cpp
    ToolingReplayTests.cpp
    ToolingDumpParamsTests.cpp
    ToolingScenesTests.cpp)
target_compile_features(ToolingTests PRIVATE cxx_std_23)
target_link_libraries(ToolingTests PRIVATE Engine)
add_test(NAME ToolingTests COMMAND ToolingTests)

add_executable(JsonGoldenTests JsonGoldenTests.cpp)
target_compile_features(JsonGoldenTests PRIVATE cxx_std_23)
target_link_libraries(JsonGoldenTests PRIVATE Engine)
target_include_directories(JsonGoldenTests PRIVATE "${PROJECT_SOURCE_DIR}/src/vendors/DiligentEngine/DiligentTools/ThirdParty/json/include")
add_test(NAME JsonGoldenTests COMMAND JsonGoldenTests)

add_executable(NarrativeTests
    NarrativeTests.cpp
    NarrativeStateTests.cpp
    NarrativeStoryletTests.cpp
    NarrativeObservationTests.cpp
    NarrativeSaveTests.cpp
    NarrativeCameraRigTests.cpp)
target_compile_features(NarrativeTests PRIVATE cxx_std_23)
target_link_libraries(NarrativeTests PRIVATE Engine)
add_test(NAME NarrativeTests COMMAND NarrativeTests)

add_executable(SystemsTests
    SystemsTests.cpp
    SystemsAiTests.cpp
    SystemsNavGeometryTests.cpp
    SystemsRigFaceTests.cpp
    SystemsAudioTests.cpp)
target_compile_features(SystemsTests PRIVATE cxx_std_23)
target_link_libraries(SystemsTests PRIVATE Engine)
add_test(NAME SystemsTests COMMAND SystemsTests)

foreach(scene portal_torture streaming_route crowd vista skinning)
    add_test(NAME TestScene_${scene} COMMAND Headless --testscene ${scene} --ticks 120 --metrics ${CMAKE_CURRENT_BINARY_DIR}/scene_${scene}.json)
endforeach()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME HeadlessCli COMMAND Headless --ticks 120 --seed 3 --perf ${CMAKE_CURRENT_BINARY_DIR}/cli_perf.csv --dump ${CMAKE_CURRENT_BINARY_DIR}/cli_state.json)
    add_test(NAME PerfCheckPasses COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/perf_check.py ${CMAKE_CURRENT_BINARY_DIR}/cli_perf.csv ${CMAKE_CURRENT_SOURCE_DIR}/data/perf_thresholds.txt --skip 5)
    add_test(NAME PerfCheckFails COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/perf_check.py ${CMAKE_CURRENT_BINARY_DIR}/cli_perf.csv ${CMAKE_CURRENT_SOURCE_DIR}/data/perf_thresholds_fail.txt --skip 5)
    set_tests_properties(PerfCheckPasses PerfCheckFails PROPERTIES DEPENDS HeadlessCli)
    set_tests_properties(PerfCheckFails PROPERTIES WILL_FAIL TRUE)
endif()

add_executable(ValidatorTests ValidatorTests.cpp)
target_compile_features(ValidatorTests PRIVATE cxx_std_23)
target_link_libraries(ValidatorTests PRIVATE Engine)
add_test(NAME ValidatorTests COMMAND ValidatorTests)

add_executable(DeterminismTests DeterminismTests.cpp)
target_compile_features(DeterminismTests PRIVATE cxx_std_23)
target_link_libraries(DeterminismTests PRIVATE Engine)
add_test(NAME DeterminismTests COMMAND DeterminismTests)

add_executable(PhysicsTests PhysicsTests.cpp)
target_compile_features(PhysicsTests PRIVATE cxx_std_23)
target_link_libraries(PhysicsTests PRIVATE Engine)
add_test(NAME PhysicsTests COMMAND PhysicsTests)

add_executable(PhysicsShapesTests PhysicsShapesTests.cpp)
target_compile_features(PhysicsShapesTests PRIVATE cxx_std_23)
target_link_libraries(PhysicsShapesTests PRIVATE Engine)
add_test(NAME PhysicsShapesTests COMMAND PhysicsShapesTests)

add_executable(InstancingTests InstancingTests.cpp)
target_compile_features(InstancingTests PRIVATE cxx_std_23)
target_link_libraries(InstancingTests PRIVATE Engine)
add_test(NAME InstancingTests COMMAND InstancingTests)

add_executable(TerrainTests TerrainTests.cpp)
target_compile_features(TerrainTests PRIVATE cxx_std_23)
target_link_libraries(TerrainTests PRIVATE Engine)
add_test(NAME TerrainTests COMMAND TerrainTests)

add_executable(SkinningTests SkinningTests.cpp)
target_compile_features(SkinningTests PRIVATE cxx_std_23)
target_link_libraries(SkinningTests PRIVATE Engine)
add_test(NAME SkinningTests COMMAND SkinningTests)

add_executable(GalleryTests GalleryTests.cpp)
target_compile_features(GalleryTests PRIVATE cxx_std_23)
target_link_libraries(GalleryTests PRIVATE Engine glfw)
add_test(NAME GalleryTests COMMAND GalleryTests)

add_executable(IkTestSceneTests IkTestSceneTests.cpp)
target_compile_features(IkTestSceneTests PRIVATE cxx_std_23)
target_link_libraries(IkTestSceneTests PRIVATE Engine)
add_test(NAME IkTestSceneTests COMMAND IkTestSceneTests)

add_executable(StreamingTests StreamingTests.cpp)
target_compile_features(StreamingTests PRIVATE cxx_std_23)
target_link_libraries(StreamingTests PRIVATE Engine)
add_test(NAME StreamingTests COMMAND StreamingTests)

add_executable(ResidencyTests ResidencyTests.cpp)
target_compile_features(ResidencyTests PRIVATE cxx_std_23)
target_link_libraries(ResidencyTests PRIVATE Engine)
add_test(NAME ResidencyTests COMMAND ResidencyTests)

add_executable(LargeWorldTests LargeWorldTests.cpp)
target_compile_features(LargeWorldTests PRIVATE cxx_std_23)
target_link_libraries(LargeWorldTests PRIVATE Engine)
add_test(NAME LargeWorldTests COMMAND LargeWorldTests)

add_executable(LargeWorldRenderTests LargeWorldRenderTests.cpp)
target_compile_features(LargeWorldRenderTests PRIVATE cxx_std_23)
target_link_libraries(LargeWorldRenderTests PRIVATE Engine glfw lit_stb)
target_include_directories(LargeWorldRenderTests PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/src/vendors/glfw/include")
add_test(NAME LargeWorldRenderTests COMMAND LargeWorldRenderTests WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(ObservationGpuTests ObservationGpuTests.cpp)
target_compile_features(ObservationGpuTests PRIVATE cxx_std_23)
target_link_libraries(ObservationGpuTests PRIVATE Engine glfw)
add_test(NAME ObservationGpuTests COMMAND ObservationGpuTests WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(LitBench
    bench/Main.cpp
    bench/Harness.cpp
    bench/Report.cpp
    bench/AllocCounter.cpp
    bench/BenchWorld.cpp
    bench/BenchSpatial.cpp
    bench/BenchNav.cpp
    bench/BenchAudio.cpp
    bench/BenchAi.cpp
    bench/BenchAnimation.cpp
    bench/BenchPortal.cpp
    bench/BenchStreaming.cpp
    bench/BenchData.cpp
    bench/BenchAsset.cpp)
target_compile_features(LitBench PRIVATE cxx_std_23)
target_link_libraries(LitBench PRIVATE Engine)
target_include_directories(LitBench PRIVATE bench)
option(LIT_BENCH_CTEST "Register the LitBench smoke test with ctest" OFF)
if(LIT_BENCH_CTEST)
    add_test(NAME BenchSmoke COMMAND LitBench --filter nav.find_path)
    set_tests_properties(BenchSmoke PROPERTIES LABELS bench)
endif()

add_executable(IKTests IKTests.cpp)
target_compile_features(IKTests PRIVATE cxx_std_23)
target_link_libraries(IKTests PRIVATE Engine)
add_test(NAME IKTests COMMAND IKTests)

add_executable(QualityTests QualityTests.cpp)
target_compile_features(QualityTests PRIVATE cxx_std_23)
target_link_libraries(QualityTests PRIVATE Engine)
add_test(NAME QualityTests COMMAND QualityTests)

add_executable(RenderGraphTests RenderGraphTests.cpp)
target_compile_features(RenderGraphTests PRIVATE cxx_std_23)
target_link_libraries(RenderGraphTests PRIVATE Engine)
add_test(NAME RenderGraphTests COMMAND RenderGraphTests)

add_executable(ReflectionTests ReflectionTests.cpp)
target_compile_features(ReflectionTests PRIVATE cxx_std_23)
target_link_libraries(ReflectionTests PRIVATE Engine)
add_test(NAME ReflectionTests COMMAND ReflectionTests)

add_executable(HeadlessRenderTests HeadlessRenderTests.cpp)
target_compile_features(HeadlessRenderTests PRIVATE cxx_std_23)
target_link_libraries(HeadlessRenderTests PRIVATE Engine)
add_test(NAME HeadlessRenderTests COMMAND HeadlessRenderTests)

add_executable(NavTests NavTests.cpp)
target_compile_features(NavTests PRIVATE cxx_std_23)
target_link_libraries(NavTests PRIVATE Engine)
add_test(NAME NavTests COMMAND NavTests)

add_executable(EdgeTests
    EdgeTests.cpp
    EdgeGeometryTests.cpp
    EdgeGizmoTextTests.cpp
    EdgeEventsSelectionTests.cpp
    EdgeHistoryProfilerTests.cpp)
target_compile_features(EdgeTests PRIVATE cxx_std_23)
target_link_libraries(EdgeTests PRIVATE Engine)
add_test(NAME EdgeTests COMMAND EdgeTests)

add_executable(ReviewTests
    ReviewTests.cpp
    ReviewJsonTests.cpp
    ReviewWorldTests.cpp
    ReviewIkTests.cpp
    ReviewToolsTests.cpp)
target_compile_features(ReviewTests PRIVATE cxx_std_23)
target_link_libraries(ReviewTests PRIVATE Engine)
add_test(NAME ReviewTests COMMAND ReviewTests)
set_tests_properties(ReviewTests PROPERTIES TIMEOUT 300)

add_executable(Review3Tests
    Review3Tests.cpp
    Review3AiTests.cpp
    Review3AudioTests.cpp
    Review3NarrativeTests.cpp
    Review3PortalTests.cpp
    Review3AnimationTests.cpp)
target_compile_features(Review3Tests PRIVATE cxx_std_23)
target_link_libraries(Review3Tests PRIVATE Engine)
add_test(NAME Review3Tests COMMAND Review3Tests)
set_tests_properties(Review3Tests PROPERTIES TIMEOUT 300)

add_executable(ShaderCacheTests ShaderCacheTests.cpp)
target_compile_features(ShaderCacheTests PRIVATE cxx_std_23)
target_include_directories(ShaderCacheTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME ShaderCacheTests COMMAND ShaderCacheTests)

add_executable(Review2Tests
    Review2Tests.cpp
    Review2NavTests.cpp
    Review2CrowdTests.cpp
    Review2GeometryTests.cpp
    Review2AssetTests.cpp
    Review2SaveTests.cpp
    Review2SaveRollbackTests.cpp
    Review2StreamingTests.cpp)
target_compile_features(Review2Tests PRIVATE cxx_std_23)
target_link_libraries(Review2Tests PRIVATE Engine)
add_test(NAME Review2Tests COMMAND Review2Tests)
set_tests_properties(Review2Tests PROPERTIES TIMEOUT 300)

add_executable(EditorQualityTests EditorQualityTests.cpp)
target_compile_features(EditorQualityTests PRIVATE cxx_std_23)
target_link_libraries(EditorQualityTests PRIVATE Engine)
target_include_directories(EditorQualityTests PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_test(NAME EditorQualityTests COMMAND EditorQualityTests)

# Bent (docs/Bent.md): language unit tests, then every law in tests/bent/laws with a fixed seed and a small budget plus every
# regression in tests/bent/regressions (examples marked `known` are expected failures).
add_executable(BentTests BentTests.cpp)
target_compile_features(BentTests PRIVATE cxx_std_23)
target_link_libraries(BentTests PRIVATE BentLang)
set_target_properties(BentTests PROPERTIES CXX_SCAN_FOR_MODULES OFF)
add_test(NAME BentTests COMMAND BentTests)
add_test(NAME BentLaws COMMAND bent suite --laws ${CMAKE_CURRENT_SOURCE_DIR}/bent/laws --regressions ${CMAKE_CURRENT_SOURCE_DIR}/bent/regressions --cases 25 --seed 1
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(BentLaws PROPERTIES TIMEOUT 1200)
# Memory safety (docs/Bent.md): the whole suite under a sanitizer build of bent (build-san by default, BENT_MEMSAFE_BIN overrides). Never builds it;
# skipped with 77 when that binary is missing.
add_test(NAME BentMemSafe COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/bent/memsafe_ctest.sh ${CMAKE_SOURCE_DIR}
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(BentMemSafe PROPERTIES TIMEOUT 1800 SKIP_RETURN_CODE 77)
# GPU mode (docs/Bent.md, "GPU mode"): laws that render real frames through LitShot (320x180, deterministic). Skipped with code 77 when
# LitShot is not built or no device works; the default BentLaws suite never runs them (they live in tests/bent/laws/gpu).
add_test(NAME BentGpuLaws COMMAND bent suite --gpu --laws ${CMAKE_CURRENT_SOURCE_DIR}/bent/laws/gpu --cases 4 --seed 1 --jobs 1
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(BentGpuLaws PROPERTIES TIMEOUT 1800 SKIP_RETURN_CODE 77)

# Bindless texture table (docs/RenderGraph.md): the slot allocator is tested headless; the GPU parity test renders the `textures`
# scene with the table on and off and requires identical pixels (skipped, exit 77, without LitShot or a usable GPU).
add_executable(TextureTableTests TextureTableTests.cpp)
target_compile_features(TextureTableTests PRIVATE cxx_std_23)
target_include_directories(TextureTableTests PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_test(NAME TextureTableTests COMMAND TextureTableTests)
add_test(NAME BindlessTextureParity COMMAND ${PROJECT_SOURCE_DIR}/scripts/bindless_check.sh --assert)
set_tests_properties(BindlessTextureParity PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900 ENVIRONMENT "BUILD_DIR=${CMAKE_BINARY_DIR};SCENES=textures")

# Static memory-safety scan (scripts/memsafe_scan.sh): clang-tidy ratchet against scripts/memsafe_baseline.txt; skipped with 77 without clang-tidy.
add_test(NAME MemSafeStatic COMMAND ${CMAKE_SOURCE_DIR}/scripts/memsafe_scan.sh WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(MemSafeStatic PROPERTIES TIMEOUT 3600 SKIP_RETURN_CODE 77 ENVIRONMENT "MEMSAFE_BUILD_DIR=${CMAKE_BINARY_DIR}")
