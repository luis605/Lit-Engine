#include "TestSupport.hpp"
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

import Engine.engine;
import Engine.World;
import Engine.glm;
import Engine.Render.settings;
import Engine.Render.reflection;

static void testReflectionSettings() {
    RenderSettings settings;
    CHECK(settings.reflectionMode == ReflectionMode::SSRProbes);
    CHECK(setRenderSetting(settings, "reflectionMode", "4"));
    CHECK(settings.reflectionMode == ReflectionMode::WorldSpace);
    CHECK(setRenderSetting(settings, "reflectionMode", "99"));
    CHECK(settings.reflectionMode == ReflectionMode::WorldSpace);
    CHECK(setRenderSetting(settings, "reflectionQuality", "0"));
    CHECK(settings.reflectionQuality == ReflectionQuality::Low);
    CHECK(setRenderSetting(settings, "reflectionIntensity", "9"));
    CHECK(settings.reflectionIntensity == 4.0f);
    CHECK(setRenderSetting(settings, "ssr", "0"));
    CHECK(settings.reflectionMode == ReflectionMode::Off);
    CHECK(setRenderSetting(settings, "ssr", "1"));
    CHECK(settings.reflectionMode == ReflectionMode::SSR);
    CHECK(!setRenderSetting(settings, "reflectionMode", "abc"));
    CHECK(setRenderSetting(settings, "reflectionMode", "3"));
    CHECK(renderSettingValue(settings, "reflectionMode") == "3");
    std::ostringstream out;
    writeRenderSettings(settings, out, "render.");
    RenderSettings restored;
    std::istringstream lines(out.str());
    std::string line;
    while (std::getline(lines, line)) {
        const size_t split = line.find(' ');
        setRenderSetting(restored, std::string_view(line).substr(7, split - 7), std::string_view(line).substr(split + 1));
    }
    CHECK(restored == settings);
}

static void testReflectionPlan() {
    RenderSettings settings;
    settings.reflectionMode = ReflectionMode::Off;
    CHECK(!reflectionPlan(settings).enabled());
    settings.reflectionMode = ReflectionMode::SSR;
    CHECK(reflectionPlan(settings).features == kReflectionModeSsr);
    settings.reflectionMode = ReflectionMode::SSRProbes;
    CHECK(reflectionPlan(settings).has(kReflectionModeProbes));
    CHECK(!reflectionPlan(settings).has(kReflectionModePlanar));
    settings.reflectionMode = ReflectionMode::SSRPlanar;
    CHECK(reflectionPlan(settings).has(kReflectionModePlanar));
    settings.reflectionMode = ReflectionMode::WorldSpace;
    CHECK(reflectionPlan(settings).has(kReflectionModeWorld));
    CHECK(reflectionTier(ReflectionQuality::Low).ssrStepCap < reflectionTier(ReflectionQuality::High).ssrStepCap);
    CHECK(reflectionTier(ReflectionQuality::Low).planarDivisor > reflectionTier(ReflectionQuality::High).planarDivisor);
}

static void testReflectionMath() {
    const glm::vec3 projected = boxProjectDirection(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(5.0f));
    CHECK(std::abs(projected.x - 1.0f) < 1e-4f);
    const glm::vec3 offset = boxProjectDirection(glm::vec3(4.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f), glm::vec3(5.0f));
    CHECK(offset.x > 0.5f && offset.y > 0.5f);

    ReflectionProbe probe;
    probe.halfExtents = glm::vec3(4.0f);
    probe.blendDistance = 2.0f;
    CHECK(probeInfluence(glm::vec3(0.0f), probe) == 1.0f);
    CHECK(probeInfluence(glm::vec3(10.0f, 0.0f, 0.0f), probe) == 0.0f);
    const float edge = probeInfluence(glm::vec3(3.0f, 0.0f, 0.0f), probe);
    CHECK(edge > 0.0f && edge < 1.0f);
    CHECK(strongestProbe({probe}, glm::vec3(0.0f)) == 0);
    CHECK(strongestProbe({probe}, glm::vec3(50.0f)) == -1);

    ReflectionPlane floorPlane;
    ReflectionPlane ceiling;
    ceiling.center = glm::vec3(0.0f, 10.0f, 0.0f);
    ceiling.normal = glm::vec3(0.0f, -1.0f, 0.0f);
    CHECK(selectPlanarPlane({floorPlane, ceiling}, glm::vec3(0.0f, 2.0f, 0.0f)) == 0);
    CHECK(selectPlanarPlane({floorPlane}, glm::vec3(0.0f, -2.0f, 0.0f)) == -1);
    CHECK(selectPlanarPlane({}, glm::vec3(0.0f)) == -1);
}

static void testReflectionVoxels() {
    CHECK(!buildReflectionVoxels({}, 16).valid());
    ReflectionBox floorBox;
    floorBox.center = glm::vec3(0.0f);
    floorBox.halfExtents = glm::vec3(10.0f, 0.1f, 10.0f);
    floorBox.color = glm::vec3(1.0f, 0.0f, 0.0f);
    ReflectionBox pillar;
    pillar.center = glm::vec3(5.0f, 3.0f, 5.0f);
    pillar.halfExtents = glm::vec3(0.5f, 3.0f, 0.5f);
    pillar.color = glm::vec3(0.0f, 1.0f, 0.0f);
    const ReflectionVoxelGrid grid = buildReflectionVoxels({floorBox, pillar}, 16);
    CHECK(grid.valid());
    CHECK(grid.cells.size() == 16u * 16u * 16u);
    const uint32_t floorCell = grid.at(8, 0, 8);
    CHECK((floorCell >> 24) == 0xFFu);
    CHECK((floorCell & 0xFFu) == 0xFFu);
    CHECK(grid.at(-1, 0, 0) == 0u);
    size_t occupied = 0;
    for (uint32_t cell : grid.cells) occupied += (cell >> 24) != 0u ? 1u : 0u;
    CHECK(occupied > 0 && occupied < grid.cells.size());
    CHECK(hashReflectionBoxes({floorBox}) != hashReflectionBoxes({floorBox, pillar}));
    CHECK(hashReflectionBoxes({floorBox}) == hashReflectionBoxes({floorBox}));
}

static void testReflectionComponents() {
    Engine engine;
    engine.openHeadless();
    World& world = engine.world();
    const auto probeEntity = world.create("probe", 0, glm::vec3(1.0f, 2.0f, 3.0f));
    ReflectionProbeComponent probeData;
    probeData.halfExtents = glm::vec3(2.0f, 3.0f, 4.0f);
    probeData.resolution = 64;
    world.add<ReflectionProbeComponent>(probeEntity, probeData);
    const auto planeEntity = world.create("mirror", 0, glm::vec3(0.0f, 0.5f, 0.0f));
    world.add<PlanarReflectionComponent>(planeEntity, PlanarReflectionComponent{});
    ReflectionScene scene;
    collectReflectionVolumes(world, scene);
    CHECK(scene.probes.size() == 1);
    CHECK(scene.planes.size() == 1);
    CHECK(scene.probes[0].center.y == 2.0f);
    CHECK(scene.probes[0].halfExtents.z == 4.0f);
    CHECK(scene.probes[0].resolution == 64);
    CHECK(std::abs(scene.planes[0].normal.y - 1.0f) < 1e-4f);
    engine.renderSettings().reflectionMode = ReflectionMode::WorldSpace;
    engine.tick(0.016f);
    engine.tick(0.016f);
    engine.bakeReflectionProbes();
    engine.tick(0.016f);
    CHECK(true);
}

static void testProbeBake() {
    CHECK(clampProbeResolution(0) == kProbeResolutionMin);
    CHECK(clampProbeResolution(33) == 64);
    CHECK(clampProbeResolution(128) == 128);
    CHECK(clampProbeResolution(100000) == kProbeResolutionMax);
    ReflectionProbe small;
    small.resolution = 64;
    ReflectionProbe large;
    large.resolution = 256;
    CHECK(sceneProbeResolution({}) == kProbeResolutionMin);
    CHECK(sceneProbeResolution({small, large}) == 256);
    ProbeBakeState state;
    CHECK(probeBakeNeeded(state, 1, 128));
    CHECK(!probeBakeNeeded(state, 0, 128));
    state = ProbeBakeState{false, 2, 128};
    CHECK(!probeBakeNeeded(state, 2, 128));
    CHECK(probeBakeNeeded(state, 3, 128));
    CHECK(probeBakeNeeded(state, 2, 256));
    state.requested = true;
    CHECK(probeBakeNeeded(state, 2, 128));
}

static void testProbeResolutionSerialization() {
    World world;
    registerCoreComponents(world);
    const auto entity = world.create("probe", 0);
    ReflectionProbeComponent probe;
    probe.resolution = 256;
    world.add<ReflectionProbeComponent>(entity, probe);
    const auto path = std::filesystem::temp_directory_path() / "lit_probe_resolution.litscene";
    CHECK(world.saveScene(path));
    world.clear();
    CHECK(world.loadScene(path));
    const ReflectionProbeComponent* loaded = world.get<ReflectionProbeComponent>(world.find("probe"));
    CHECK(loaded && loaded->resolution == 256);
    std::filesystem::remove(path);
}

int main() {
    testReflectionSettings();
    testReflectionPlan();
    testReflectionMath();
    testReflectionVoxels();
    testReflectionComponents();
    testProbeBake();
    testProbeResolutionSerialization();
    return testsupport::finish();
}
