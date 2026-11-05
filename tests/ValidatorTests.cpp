#include "TestSupport.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

import Engine.World;
import Engine.Portal;
import Engine.SceneValidator;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.glm;

static bool hasCode(const std::vector<ValidationIssue>& issues, const std::string& code) {
    for (const ValidationIssue& issue : issues) {
        if (issue.code == code) return true;
    }
    return false;
}

static const ValidationIssue* findCode(const std::vector<ValidationIssue>& issues, const std::string& code) {
    for (const ValidationIssue& issue : issues) {
        if (issue.code == code) return &issue;
    }
    return nullptr;
}

static void testCleanScene() {
    World world;
    world.create("floor", 0, glm::vec3(0.0f), glm::vec3(10.0f, 0.1f, 10.0f));
    ValidatorLimits limits;
    limits.checkAssets = true;
    limits.meshCount = 1;
    limits.materialCount = 4;
    const auto issues = validateScene(world, limits);
    CHECK(countIssues(issues, Severity::Error) == 0);
}

static void testMissingAssets() {
    World world;
    const EntityHandle bad = world.create("ghost", 7);
    ValidatorLimits limits;
    limits.checkAssets = true;
    limits.meshCount = 2;
    limits.materialCount = 1;
    const auto issues = validateScene(world, limits);
    const ValidationIssue* mesh = findCode(issues, "mesh.missing");
    CHECK(mesh != nullptr);
    CHECK(mesh && mesh->entity == bad);
    CHECK(mesh && mesh->severity == Severity::Error);
}

static void testTransformProblems() {
    World world;
    const EntityHandle nan = world.create("nan", 0);
    world.setPosition(nan, glm::vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f));
    const EntityHandle flat = world.create("flat", 0, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 1.0f));
    const EntityHandle huge = world.create("huge", 0, glm::vec3(0.0f), glm::vec3(1.0e6f));
    const auto issues = validateScene(world);
    const ValidationIssue* nanIssue = findCode(issues, "transform.nan");
    CHECK(nanIssue && nanIssue->entity == nan);
    const ValidationIssue* zero = findCode(issues, "scale.zero");
    CHECK(zero && zero->entity == flat);
    const ValidationIssue* big = findCode(issues, "scale.huge");
    CHECK(big && big->entity == huge && big->severity == Severity::Warning);
    CHECK(issues.front().severity == Severity::Error);
}

static void testLights() {
    World world;
    const EntityHandle dark = world.create("dark");
    LightComponent light;
    light.intensity = 0.0f;
    world.add<LightComponent>(dark, light);
    const EntityHandle hidden = world.create("hidden");
    world.add<LightComponent>(hidden, LightComponent{});
    world.setVisible(hidden, false);
    const auto issues = validateScene(world);
    const ValidationIssue* darkIssue = findCode(issues, "light.dark");
    CHECK(darkIssue && darkIssue->entity == dark);
    const ValidationIssue* hiddenIssue = findCode(issues, "light.hidden");
    CHECK(hiddenIssue && hiddenIssue->entity == hidden);
}

static void testDuplicateNamesAndHierarchy() {
    World world;
    const EntityHandle parent = world.create("same");
    const EntityHandle child = world.create("same", NO_MESH, glm::vec3(0.0f), glm::vec3(1.0f), parent);
    CHECK(world.getParent(child) == parent);
    const auto issues = validateScene(world);
    CHECK(hasCode(issues, "name.duplicate"));
    CHECK(!hasCode(issues, "hierarchy.cycle"));
    CHECK(!hasCode(issues, "id.duplicate"));
    CHECK(countIssues(issues, Severity::Error) == 0);
}

static void testPortalOverlap() {
    PortalSystem portals;
    PortalCell cell;
    cell.name = "A";
    portals.addCell(cell);
    PortalEnd first;
    first.cell = "A";
    first.position = glm::vec3(0.0f, 1.0f, 0.0f);
    PortalEnd far = first;
    far.position = glm::vec3(100.0f, 1.0f, 0.0f);
    PortalEnd near = first;
    near.position = glm::vec3(0.5f, 1.0f, 0.0f);
    PortalEnd farther = first;
    farther.position = glm::vec3(0.0f, 1.0f, 200.0f);
    portals.addPair("one", first, far);
    portals.addPair("two", near, farther);
    World world;
    ValidatorLimits limits;
    limits.portals = &portals;
    CHECK(hasCode(validateScene(world, limits), "portal.overlap"));
    limits.portals = nullptr;
    CHECK(!hasCode(validateScene(world, limits), "portal.overlap"));
}

static void testFactsAndExplain() {
    World world;
    world.create("a", 0);
    const EntityHandle faded = world.create("b", 0);
    world.setAlpha(faded, 0.5f);
    const EntityHandle sun = world.create("sun");
    LightComponent light;
    light.type = LightComponent::Type::Directional;
    world.add<LightComponent>(sun, light);
    const SceneFacts facts = gatherSceneFacts(world);
    CHECK(facts.drawables == 2);
    CHECK(facts.transparent == 1);
    CHECK(facts.shadowCasters == 1);
    CHECK(facts.lights == 1);
    CHECK(facts.spotAndPointLights == 0);

    FrameFacts frame;
    frame.gpuMs = 20.0;
    frame.cpuMs = 4.0;
    frame.gpuPasses = {{"Shadow cascades", 12.0}, {"Lighting", 3.0}, {"Bloom", 1.0}};
    frame.cpuScopes = {{"Engine::update", 3.5}, {"physics", 0.5}};
    frame.scene.shadowCasters = 9000;
    frame.streamingPending = 3;
    const auto findings = explainFrame(frame);
    CHECK(findings.size() >= 3);
    CHECK(findings.front().title.find("Shadow cascades") != std::string::npos);
    CHECK(findings.front().advice.find("shadow") != std::string::npos);
    for (size_t index = 1; index < findings.size(); ++index) CHECK(findings[index - 1].weight >= findings[index].weight);
    CHECK(formatFindings(findings).find("streaming") != std::string::npos);

    const auto calm = explainFrame(FrameFacts{});
    CHECK(calm.size() == 1);
}

int main() {
    testCleanScene();
    testMissingAssets();
    testTransformProblems();
    testLights();
    testDuplicateNamesAndHierarchy();
    testPortalOverlap();
    testFactsAndExplain();
    return testsupport::finish();
}
