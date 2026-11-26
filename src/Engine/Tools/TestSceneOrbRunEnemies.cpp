module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

module Engine.OrbRun;

import Engine.engine;
import Engine.glm;
import Engine.World;
import Engine.Portal;
import Engine.Physics;
import Engine.IK;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.OrbRunRules;

#include "OrbRunInternal.hpp"

namespace orbrun {

namespace {

constexpr float kChaseRange = 14.0f;
constexpr float kPatrolSpeed = 1.2f;
constexpr float kChaseSpeed = 2.6f;
constexpr float kWaypointArrival = 0.7f;
constexpr float kSpiderLegLength = 1.1f;
constexpr float kSpiderBodyRadius = 0.4f;

glm::vec3 horizontal(const glm::vec3& vector) { return glm::vec3(vector.x, 0.0f, vector.z); }

glm::vec3 patrolGoal(OrbRunSpider& spider, const glm::vec3& position) {
    const std::vector<glm::vec3>& waypoints = spider.route.waypoints;
    if (waypoints.empty()) return spider.route.home;
    if (glm::distance(horizontal(position), horizontal(waypoints[spider.waypoint])) < kWaypointArrival) spider.waypoint = (spider.waypoint + 1) % waypoints.size();
    return waypoints[spider.waypoint];
}

void steerSpider(Engine& engine, const OrbRunGame& game, OrbRunSpider& spider, const PlayerPose& pose) {
    World& world = engine.world();
    Walker* walker = world.get<Walker>(spider.body);
    if (!walker) return;
    const glm::vec3 position = world.getWorldPosition(spider.body);
    const bool sharesCell = pose.cell >= 0 && engine.portals().cellAt(position) == pose.cell;
    const float distance = glm::distance(horizontal(position), horizontal(pose.feet));
    spider.chasing = game.state.phase == OrbRunPhase::Playing && sharesCell && distance < kChaseRange;
    spider.frozen = spider.chasing && orbRunSeenBy(game.config, pose.eye, pose.forward, position);
    walker->enabled = !spider.frozen;
    walker->maxSpeed = spider.chasing ? kChaseSpeed : kPatrolSpeed;
    setWalkerTarget(world, spider.body, spider.chasing ? pose.feet : patrolGoal(spider, position));
}

}

void buildSpiders(const Context& context) {
    Material material;
    material.name = "Orb Run Spider";
    material.baseColor = glm::vec3(0.1f, 0.07f, 0.1f);
    material.metallic = 0.5f;
    material.roughness = 0.35f;
    material.emissive = glm::vec3(1.0f, 0.08f, 0.05f);
    material.emissiveIntensity = 0.9f;
    SpiderOptions options;
    options.legs = 8;
    options.legLength = kSpiderLegLength;
    options.bodyRadius = kSpiderBodyRadius;
    options.maxSpeed = kPatrolSpeed;
    options.bodyMesh = context.kit.portal.sphere;
    options.boneMesh = context.kit.portal.cube;
    options.material = context.material(material);
    options.name = "orb spider";
    context.game.spiders.clear();
    for (const OrbRunPatrol& route : context.game.layout.spiders) {
        OrbRunSpider spider;
        spider.route = route;
        spider.body = makeSpider(context.world, route.home, options);
        context.game.spiders.push_back(spider);
    }
}

void steerSpiders(Engine& engine, OrbRunGame& game, const PlayerPose& pose) {
    for (OrbRunSpider& spider : game.spiders) steerSpider(engine, game, spider, pose);
}

}
