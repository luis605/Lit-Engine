#include "TestSupport.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

import Engine.engine;
import Engine.World;
import Engine.Physics;
import Engine.Character;
import Engine.Portal;
import Engine.camera;
import Engine.Replay;
import Engine.Render.entity;
import Engine.TestScenes;
import Engine.OrbRun;
import Engine.OrbRunRules;
import Engine.glm;

namespace {

constexpr float kStep = 1.0f / 60.0f;
constexpr float kBodyCenter = 0.9f;
constexpr float kYawEast = 0.0f;
constexpr float kYawNorth = -90.0f;
constexpr float kYawWest = 180.0f;
constexpr int kSettleTicks = 90;
constexpr float kContinuityDistance = 0.2f;
constexpr float kContinuityAlignment = 0.95f;

struct Run {
    std::unique_ptr<Engine> engine;
    TestSceneInfo info;
    OrbRunGame* game = nullptr;
    CharacterController* player = nullptr;
};

Run makeRun() {
    Run run;
    run.engine = std::make_unique<Engine>();
    run.engine->openHeadless();
    run.engine->seed(5);
    const TestSceneKit kit = makeTestSceneKit(*run.engine);
    CHECK(buildTestScene(*run.engine, "orb_run", kit, run.info));
    applyTestSceneCamera(*run.engine, run.info);
    enableOrbRunPlay(*run.engine);
    run.game = orbRunGame(*run.engine);
    run.player = run.engine->character();
    return run;
}

void tick(Run& run, int ticks, int key = -1) {
    run.engine->runHeadless(static_cast<uint64_t>(ticks), kStep, [&](uint64_t) {
        InputState state;
        if (key >= 0) state.keys.set(key);
        run.engine->setInput(state);
    });
}

void teleport(Run& run, const glm::vec3& feet, float yaw) { run.player->spawn(feet, yaw); }

glm::vec3 feetFor(const glm::vec3& center) { return center - glm::vec3(0.0f, kBodyCenter, 0.0f); }

int cellOf(Run& run, const char* name) { return run.engine->portals().findCell(name); }

void testRules() {
    const OrbRunConfig config;
    OrbRunState state = orbRunStart(config);
    CHECK(state.lives == 3 && state.collected == 0 && state.timeLeft == 180.0f);
    CHECK(orbRunCollect(state, config, 2));
    CHECK(!orbRunCollect(state, config, 2));
    CHECK(!orbRunCollect(state, config, 99));
    CHECK(state.collected == 1);
    CHECK(orbRunHit(state, config) && state.lives == 2);
    CHECK(!orbRunHit(state, config) && state.lives == 2);
    orbRunAdvance(state, config.invulnerableSeconds + 0.1f);
    CHECK(orbRunHit(state, config) && orbRunHit(state, config) == false);
    orbRunAdvance(state, 2.1f);
    CHECK(orbRunHit(state, config));
    CHECK(state.phase == OrbRunPhase::Lost && state.loss == OrbRunLoss::NoLives);
    CHECK(!orbRunCollect(state, config, 0));

    OrbRunState timed = orbRunStart(config);
    orbRunAdvance(timed, 179.0f);
    CHECK(timed.phase == OrbRunPhase::Playing);
    orbRunAdvance(timed, 1.5f);
    CHECK(timed.phase == OrbRunPhase::Lost && timed.loss == OrbRunLoss::Timeout && timed.timeLeft == 0.0f);

    OrbRunState winning = orbRunStart(config);
    for (int orb = 0; orb < config.orbTotal; ++orb) CHECK(orbRunCollect(winning, config, orb));
    CHECK(winning.phase == OrbRunPhase::Won);
    orbRunAdvance(winning, 500.0f);
    CHECK(winning.phase == OrbRunPhase::Won);

    const std::vector<glm::vec3> orbs = {glm::vec3(0.0f), glm::vec3(10.0f, 0.0f, 0.0f)};
    OrbRunState pickup = orbRunStart(config);
    CHECK(orbRunPickupIndex(pickup, config, orbs, glm::vec3(0.5f, 0.0f, 0.0f)) == 0);
    CHECK(orbRunPickupIndex(pickup, config, orbs, glm::vec3(5.0f, 0.0f, 0.0f)) == -1);
    CHECK(orbRunTouches(config, glm::vec3(0.0f), glm::vec3(0.5f, 0.0f, 0.0f)));
    CHECK(!orbRunTouches(config, glm::vec3(0.0f), glm::vec3(3.0f, 0.0f, 0.0f)));
    CHECK(orbRunSeenBy(config, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 0.0f, -5.0f)));
    CHECK(!orbRunSeenBy(config, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(5.0f, 0.0f, 0.0f)));
    CHECK(orbRunClockText(180.0f) == "3:00");
    CHECK(orbRunClockText(59.2f) == "1:00");
    CHECK(orbRunClockText(0.0f) == "0:00");
}

void testSceneStructure() {
    Run run = makeRun();
    PortalSystem& portals = run.engine->portals();
    CHECK(run.game->layout.orbs.size() == 8);
    CHECK(run.game->orbs.size() == 8 && run.game->sockets.size() == 8);
    CHECK(run.game->spiders.size() == 3);
    CHECK(portals.cells().size() == 5);
    CHECK(portals.portals().size() == 10);
    for (const char* name : {kOrbRunHub, kOrbRunEmber, kOrbRunFrost, kOrbRunGrotto, kOrbRunCourtyard}) CHECK(cellOf(run, name) >= 0);
    for (const Portal& portal : portals.portals()) {
        const PortalCell& cell = portals.cells()[static_cast<size_t>(portal.cell)];
        const glm::vec3 center = (cell.min + cell.max) * 0.5f;
        CHECK(portal.center().x > cell.min.x && portal.center().x < cell.max.x);
        CHECK(portal.center().z > cell.min.z && portal.center().z < cell.max.z);
        CHECK(glm::dot(portal.normal(), center - portal.center()) > 0.0f || portal.cell == cellOf(run, kOrbRunCourtyard));
    }
    for (const glm::vec3& orb : run.game->layout.orbs) CHECK(portals.cellAt(orb) >= 0);
    CHECK(run.engine->terrain() != nullptr);
    CHECK(std::abs(orbRunTerrainHeight(400.0f, 0.0f)) < 1.0e-3f);
    CHECK(orbRunTerrainHeight(400.0f, 150.0f) > 10.0f);
}

void testCollectAllOrbsWins() {
    Run run = makeRun();
    for (size_t index = 0; index < run.game->layout.orbs.size(); ++index) {
        CHECK(run.game->state.phase == OrbRunPhase::Playing);
        teleport(run, feetFor(run.game->layout.orbs[index]), 0.0f);
        tick(run, 2);
        CHECK(run.game->state.collected == static_cast<int>(index) + 1);
    }
    CHECK(run.game->state.phase == OrbRunPhase::Won);
    CHECK(run.game->state.lives == 3);
    const float timeAtWin = run.game->state.timeLeft;
    tick(run, 30);
    CHECK(run.game->state.timeLeft == timeAtWin);
    for (const OrbRunOrb& orb : run.game->orbs) CHECK(!run.engine->world().isAlive(orb.entity));
}

void testTimeoutLoses() {
    Run run = makeRun();
    run.game->state.timeLeft = 1.0f;
    tick(run, 30);
    CHECK(run.game->state.phase == OrbRunPhase::Playing);
    tick(run, 60);
    CHECK(run.game->state.phase == OrbRunPhase::Lost);
    CHECK(run.game->state.loss == OrbRunLoss::Timeout);
    teleport(run, feetFor(run.game->layout.orbs[0]), 0.0f);
    tick(run, 2);
    CHECK(run.game->state.collected == 0);
}

void testSpiderContactCostsLifeAndRespawns() {
    Run run = makeRun();
    const OrbRunSpider& spider = run.game->spiders[0];
    const glm::vec3 spiderPosition = run.engine->world().getWorldPosition(spider.body);
    teleport(run, glm::vec3(spiderPosition.x, 0.05f, spiderPosition.z), 0.0f);
    tick(run, 2);
    CHECK(run.game->state.lives == 2);
    CHECK(run.game->spiderHits == 1);
    CHECK(run.player->cell() == cellOf(run, kOrbRunHub));
    CHECK(glm::distance(run.player->position(), run.game->layout.spawn.feet) < 1.0f);
    tick(run, 5);
    CHECK(run.game->state.lives == 2);
    for (int hit = 0; hit < 2; ++hit) {
        tick(run, 150);
        const glm::vec3 position = run.engine->world().getWorldPosition(run.game->spiders[1].body);
        teleport(run, glm::vec3(position.x, 0.05f, position.z), 0.0f);
        tick(run, 2);
    }
    CHECK(run.game->state.lives == 0);
    CHECK(run.game->state.phase == OrbRunPhase::Lost);
    CHECK(run.game->state.loss == OrbRunLoss::NoLives);
}

void testSpidersChaseAndFreeze() {
    Run run = makeRun();
    const glm::vec3 emberFloor = run.engine->world().getWorldPosition(run.game->spiders[0].body);
    teleport(run, glm::vec3(emberFloor.x - 8.0f, 0.05f, emberFloor.z), 0.0f);
    tick(run, 5);
    CHECK(run.game->spiders[0].chasing);
    CHECK(run.game->spiders[0].frozen);
    teleport(run, glm::vec3(emberFloor.x - 8.0f, 0.05f, emberFloor.z), 180.0f);
    tick(run, 5);
    CHECK(run.game->spiders[0].chasing);
    CHECK(!run.game->spiders[0].frozen);
    const glm::vec3 before = run.engine->world().getWorldPosition(run.game->spiders[0].body);
    tick(run, 30);
    CHECK(glm::distance(run.engine->world().getWorldPosition(run.game->spiders[0].body), before) > 0.3f);
    CHECK(!run.game->spiders[1].chasing);
}

struct CrossingAudit {
    int crossings = 0;
    float worstDistance = 0.0f;
    float worstAlignment = 1.0f;
};

CrossingAudit walkAudited(Run& run, int ticks) {
    CrossingAudit audit;
    Camera& camera = run.engine->world().camera();
    glm::vec3 previousPosition = camera.getPosition();
    glm::vec3 previousFront = camera.getFront();
    int previousCrossings = run.player->crossings();
    run.engine->runHeadless(static_cast<uint64_t>(ticks), kStep, [&](uint64_t) {
        if (run.player->crossings() != previousCrossings) {
            ++audit.crossings;
            float bestDistance = std::numeric_limits<float>::max();
            float bestAlignment = -1.0f;
            for (const Portal& portal : run.engine->portals().portals()) {
                const glm::vec3 mapped = glm::vec3(portal.transform * glm::vec4(previousPosition, 1.0f));
                const float distance = glm::distance(mapped, camera.getPosition());
                if (distance >= bestDistance) continue;
                bestDistance = distance;
                bestAlignment = glm::dot(glm::normalize(glm::vec3(portal.transform * glm::vec4(previousFront, 0.0f))), camera.getFront());
            }
            audit.worstDistance = std::max(audit.worstDistance, bestDistance);
            audit.worstAlignment = std::min(audit.worstAlignment, bestAlignment);
        }
        previousCrossings = run.player->crossings();
        previousPosition = camera.getPosition();
        previousFront = camera.getFront();
        InputState state;
        state.keys.set(GLFW_KEY_W);
        run.engine->setInput(state);
    });
    return audit;
}

void testPortalLoopReturnsToHub() {
    Run run = makeRun();
    const int hub = cellOf(run, kOrbRunHub);
    teleport(run, glm::vec3(6.0f, 0.05f, 0.0f), kYawEast);
    CrossingAudit drop = walkAudited(run, 90);
    CHECK(drop.crossings == 1);
    CHECK(run.player->cell() == cellOf(run, kOrbRunEmber));
    CHECK(drop.worstDistance < kContinuityDistance && drop.worstAlignment > kContinuityAlignment);
    tick(run, kSettleTicks);
    CHECK(run.player->grounded());
    CHECK(std::abs(run.player->position().y) < 0.2f);
    CHECK(run.player->up().y > 0.99f);

    teleport(run, glm::vec3(40.0f, 0.05f, -3.5f), kYawNorth);
    CrossingAudit ceiling = walkAudited(run, 90);
    CHECK(ceiling.crossings == 1);
    CHECK(run.player->cell() == cellOf(run, kOrbRunFrost));
    CHECK(ceiling.worstDistance < kContinuityDistance && ceiling.worstAlignment > kContinuityAlignment);
    tick(run, kSettleTicks * 2);
    CHECK(run.player->grounded());
    CHECK(run.player->position().y > -0.2f && run.player->position().y < 0.2f);

    teleport(run, glm::vec3(75.5f, 0.05f, 0.0f), kYawWest);
    CrossingAudit rolled = walkAudited(run, 60);
    CHECK(rolled.crossings == 1);
    CHECK(run.player->cell() == hub);
    CHECK(rolled.worstDistance < kContinuityDistance && rolled.worstAlignment > kContinuityAlignment);
    CHECK(run.player->position().z > 5.0f);
    tick(run, kSettleTicks);
    CHECK(std::abs(run.player->roll()) < 1.0f);
    CHECK(run.player->grounded());
}

void testCourtyardReachableAndWalkable() {
    Run run = makeRun();
    teleport(run, glm::vec3(0.0f, 0.05f, -6.0f), kYawNorth);
    CrossingAudit audit = walkAudited(run, 90);
    CHECK(audit.crossings == 1);
    CHECK(run.player->cell() == cellOf(run, kOrbRunCourtyard));
    CHECK(audit.worstDistance < kContinuityDistance);
    tick(run, kSettleTicks);
    CHECK(run.player->grounded());
    CHECK(std::abs(run.player->position().y) < 0.2f);
    CHECK(run.player->position().x > 370.0f);
}

void testGrottoReachable() {
    Run run = makeRun();
    teleport(run, glm::vec3(-6.0f, 0.05f, 0.0f), kYawWest);
    CrossingAudit audit = walkAudited(run, 90);
    CHECK(audit.crossings == 1);
    CHECK(run.player->cell() == cellOf(run, kOrbRunGrotto));
}

void testShoveMovesCrate() {
    Run run = makeRun();
    const OrbRunCrate& crate = run.game->crates[0];
    const glm::vec3 start = run.engine->world().getWorldPosition(crate.entity);
    teleport(run, glm::vec3(start.x, 0.05f, start.z + 2.5f), kYawNorth);
    tick(run, 20);
    tick(run, 3, GLFW_KEY_E);
    tick(run, 40);
    CHECK(glm::distance(run.engine->world().getWorldPosition(crate.entity), start) > 0.5f);
}

void testRestartResets() {
    Run run = makeRun();
    teleport(run, feetFor(run.game->layout.orbs[0]), 0.0f);
    tick(run, 2);
    CHECK(run.game->state.collected == 1);
    run.game->state.timeLeft = 0.1f;
    tick(run, 20);
    CHECK(run.game->state.phase == OrbRunPhase::Lost);
    tick(run, 2, GLFW_KEY_R);
    CHECK(run.game->state.phase == OrbRunPhase::Playing);
    CHECK(run.game->state.collected == 0 && run.game->state.lives == 3 && run.game->state.timeLeft > 179.9f);
    CHECK(run.engine->world().isAlive(run.game->orbs[0].entity));
    CHECK(glm::distance(run.player->position(), run.game->layout.spawn.feet) < 1.0f);
}

glm::vec3 scriptedRun() {
    Run run = makeRun();
    teleport(run, glm::vec3(6.0f, 0.05f, 0.0f), kYawEast);
    walkAudited(run, 200);
    return run.player->position() + glm::vec3(static_cast<float>(run.game->state.collected), run.game->state.timeLeft, 0.0f);
}

void testDeterministic() {
    const glm::vec3 first = scriptedRun();
    const glm::vec3 second = scriptedRun();
    CHECK(first == second);
}

}

int main() {
    testRules();
    testSceneStructure();
    testCollectAllOrbsWins();
    testTimeoutLoses();
    testSpiderContactCostsLifeAndRespawns();
    testSpidersChaseAndFreeze();
    testPortalLoopReturnsToHub();
    testCourtyardReachableAndWalkable();
    testGrottoReachable();
    testShoveMovesCrate();
    testRestartResets();
    testDeterministic();
    return testsupport::finish();
}
