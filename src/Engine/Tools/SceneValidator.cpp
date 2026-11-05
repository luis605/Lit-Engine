module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

module Engine.SceneValidator;

import Engine.World;
import Engine.Portal;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.glm;

namespace {

constexpr float kPortalCoplanarTolerance = 0.05f;
constexpr float kPortalParallelDot = 0.98f;
constexpr size_t kMaxPortalPairs = 4000;
constexpr float kMinLightIntensity = 1.0e-4f;

bool finiteMatrix(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) return false;
        }
    }
    return true;
}

struct Collector {
    std::vector<ValidationIssue> issues;
    size_t limit;

    void add(Severity severity, const char* code, std::string message, EntityHandle entity = NULL_ENTITY) {
        if (issues.size() < limit) issues.push_back({severity, code, std::move(message), entity});
    }
};

std::string label(World& world, EntityHandle entity) {
    const std::string& name = world.getName(entity);
    return (name.empty() ? "entity" : name) + " #" + std::to_string(entity.index);
}

void checkTransform(World& world, EntityHandle entity, const ValidatorLimits& limits, Collector& out) {
    if (!finiteMatrix(world.getLocalMatrix(entity))) {
        out.add(Severity::Error, "transform.nan", label(world, entity) + " has a NaN or infinite transform", entity);
        return;
    }
    const glm::vec3 scale = world.getScale(entity);
    const float smallest = std::min({scale.x, scale.y, scale.z});
    const float largest = std::max({scale.x, scale.y, scale.z});
    const bool drawn = world.getMesh(entity) != NO_MESH;
    if (smallest < limits.minScale) out.add(drawn ? Severity::Error : Severity::Warning, "scale.zero", label(world, entity) + " has a zero or degenerate scale", entity);
    if (largest > limits.maxScale) out.add(Severity::Warning, "scale.huge", label(world, entity) + " has a huge scale (" + std::to_string(largest) + ")", entity);
}

void checkAssets(World& world, EntityHandle entity, const ValidatorLimits& limits, Collector& out) {
    const uint32_t mesh = world.getMesh(entity);
    if (mesh == NO_MESH) return;
    const bool missing = limits.meshesFromBounds ? world.getWorldBounds(entity).w <= 0.0f : mesh >= limits.meshCount;
    if (missing) out.add(Severity::Error, "mesh.missing", label(world, entity) + " references missing mesh " + std::to_string(mesh), entity);
    const uint32_t material = world.getRenderable(entity).material_uuid;
    if (material >= limits.materialCount) out.add(Severity::Error, "material.missing", label(world, entity) + " references missing material " + std::to_string(material), entity);
}

void checkHierarchy(World& world, EntityHandle entity, Collector& out) {
    const EntityHandle parent = world.getParent(entity);
    if (parent.isNull()) return;
    if (!world.isAlive(parent)) {
        out.add(Severity::Error, "hierarchy.orphan", label(world, entity) + " has a dead parent", entity);
        return;
    }
    size_t steps = 0;
    for (EntityHandle ancestor = parent; !ancestor.isNull(); ancestor = world.getParent(ancestor)) {
        if (ancestor == entity || ++steps > world.aliveCount()) {
            out.add(Severity::Error, "hierarchy.cycle", label(world, entity) + " is part of a parent cycle", entity);
            return;
        }
    }
}

void checkLight(World& world, EntityHandle entity, const LightComponent& light, Collector& out) {
    const bool dark = !(light.intensity >= kMinLightIntensity) || !(glm::dot(light.color, light.color) > 0.0f);
    if (dark) out.add(Severity::Warning, "light.dark", label(world, entity) + " is a light with zero intensity or black color", entity);
    if (light.type != LightComponent::Type::Directional && !(light.range > 0.0f)) out.add(Severity::Warning, "light.range", label(world, entity) + " is a light with no range", entity);
    if (!world.isVisible(entity)) out.add(Severity::Info, "light.hidden", label(world, entity) + " is a light on a hidden entity", entity);
    if (light.type == LightComponent::Type::Spot && light.outerConeDegrees < light.innerConeDegrees) out.add(Severity::Warning, "light.cone", label(world, entity) + " is a spot light with inner cone wider than outer", entity);
}

void checkDuplicateIds(World& world, Collector& out) {
    std::unordered_map<uint32_t, EntityHandle> seenIds;
    std::unordered_map<std::string, int> seenNames;
    world.forEach([&](EntityHandle entity) {
        const auto [slot, fresh] = seenIds.emplace(world.getRenderable(entity).objectId, entity);
        if (!fresh) out.add(Severity::Error, "id.duplicate", label(world, entity) + " shares object id " + std::to_string(slot->first) + " with #" + std::to_string(slot->second.index), entity);
        const std::string& name = world.getName(entity);
        if (!name.empty() && ++seenNames[name] == 2) out.add(Severity::Info, "name.duplicate", "name '" + name + "' is used by several entities", entity);
    });
}

bool portalsOverlap(const Portal& first, const Portal& second) {
    if (std::abs(glm::dot(first.normal(), second.normal())) < kPortalParallelDot) return false;
    const glm::vec3 offset = second.center() - first.center();
    if (std::abs(glm::dot(first.normal(), offset)) > kPortalCoplanarTolerance) return false;
    return std::abs(glm::dot(first.right(), offset)) < first.half.x + second.half.x && std::abs(glm::dot(first.up(), offset)) < first.half.y + second.half.y;
}

void checkPortals(const PortalSystem& system, Collector& out) {
    const std::vector<Portal>& portals = system.portals();
    size_t pairs = 0;
    for (size_t first = 0; first < portals.size(); ++first) {
        for (size_t second = first + 1; second < portals.size() && pairs < kMaxPortalPairs; ++second, ++pairs) {
            if (portals[first].cell != portals[second].cell) continue;
            if (portalsOverlap(portals[first], portals[second])) out.add(Severity::Warning, "portal.overlap", "portals '" + portals[first].name + "' and '" + portals[second].name + "' overlap");
        }
    }
}

}

const char* severityName(Severity severity) {
    switch (severity) {
        case Severity::Info: return "info";
        case Severity::Warning: return "warning";
        case Severity::Error: return "error";
    }
    return "info";
}

size_t countIssues(const std::vector<ValidationIssue>& issues, Severity severity) {
    return static_cast<size_t>(std::count_if(issues.begin(), issues.end(), [severity](const ValidationIssue& issue) { return issue.severity == severity; }));
}

std::vector<ValidationIssue> validateScene(World& world, const ValidatorLimits& limits) {
    Collector out{{}, limits.maxIssues};
    for (const std::string& problem : world.validate()) out.add(Severity::Error, "world.integrity", problem);
    world.forEach([&](EntityHandle entity) {
        checkTransform(world, entity, limits, out);
        checkHierarchy(world, entity, out);
        if (limits.checkAssets) checkAssets(world, entity, limits, out);
    });
    world.view<LightComponent>([&](EntityHandle entity, LightComponent& light) { checkLight(world, entity, light, out); });
    checkDuplicateIds(world, out);
    if (limits.portals) checkPortals(*limits.portals, out);
    std::stable_sort(out.issues.begin(), out.issues.end(), [](const ValidationIssue& left, const ValidationIssue& right) { return left.severity > right.severity; });
    return std::move(out.issues);
}

SceneFacts gatherSceneFacts(World& world) {
    SceneFacts facts;
    facts.entities = world.aliveCount();
    world.forEach([&](EntityHandle entity) {
        if (world.getMesh(entity) == NO_MESH || !world.isVisible(entity)) return;
        ++facts.drawables;
        if (world.getRenderable(entity).alpha < 1.0f) ++facts.transparent;
        else ++facts.shadowCasters;
    });
    world.view<LightComponent>([&](EntityHandle, LightComponent& light) {
        ++facts.lights;
        if (light.type != LightComponent::Type::Directional) ++facts.spotAndPointLights;
    });
    return facts;
}
