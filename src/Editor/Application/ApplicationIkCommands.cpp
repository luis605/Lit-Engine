module;

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "Editor/Application/ApplicationInternal.hpp"

module Editor.application;

import Engine.engine;
import Engine.World;
import Engine.IK;
import Engine.IkTestScene;
import Engine.Render.material;
import Engine.Render.entity;
import Engine.glm;
import Editor.context;
import Editor.commands;

namespace {

constexpr float kSpiderWalkDistance = 6.0f;
constexpr const char* kDebugSystem = "ik debug";

size_t depthOf(World& world, EntityHandle entity) {
    size_t depth = 0;
    for (EntityHandle parent = world.getParent(entity); !parent.isNull(); parent = world.getParent(parent)) ++depth;
    return depth;
}

std::vector<EntityHandle> chainFromSelection(EditorContext& ctx) {
    World& world = ctx.world;
    std::vector<EntityHandle> joints;
    for (EntityHandle entity : ctx.selection.items()) {
        if (world.isAlive(entity)) joints.push_back(entity);
    }
    if (joints.size() == 1) {
        EntityHandle current = joints.front();
        while (joints.size() < static_cast<size_t>(kMaxIkJoints)) {
            const std::vector<EntityHandle> children = world.getChildren(current);
            if (children.empty()) break;
            current = children.front();
            joints.push_back(current);
        }
    }
    std::stable_sort(joints.begin(), joints.end(), [&](EntityHandle first, EntityHandle second) { return depthOf(world, first) < depthOf(world, second); });
    return joints;
}

std::vector<EntityHandle> buildSpider(World& world, const glm::vec3& position, const SpiderOptions& options) {
    const EntityHandle spider = makeSpider(world, position, options);
    setWalkerTarget(world, spider, position + glm::vec3(0.0f, 0.0f, -kSpiderWalkDistance));
    return collectRigEntities(world, spider);
}

}

void Application::buildIkTestScene() {
    IkTestKit kit;
    kit.cube = m_ctx->cubeMesh;
    kit.sphere = m_ctx->sphereMesh;
    kit.material = [this](const Material& material) { return m_engine.createMaterial(material); };
    ::buildIkTestScene(m_engine, kit);
    m_engine.setSystemEnabled(kDebugSystem, true);
}

void Application::registerIkCommands() {
    m_engine.addSystem(Phase::PostUpdate, kDebugSystem, [this](World& world, float) {
        drawIkDebug(world, [this](const glm::vec3& from, const glm::vec3& to, const glm::vec4& color) { m_engine.debugLine(from, to, color, true); });
    }, true);
    m_engine.setSystemEnabled(kDebugSystem, false);

    addCommand("ik.spider", "IK: spawn a walking spider", "IK", {}, [this] {
        EditorContext& ctx = *m_ctx;
        SpiderOptions options;
        options.bodyMesh = ctx.sphereMesh;
        options.boneMesh = ctx.cubeMesh;
        const glm::vec3 position = spawnPoint(ctx).position;
        World& world = ctx.world;
        const std::vector<EntityHandle> created = ctx.history->spawnRebuildable("Spawn Spider", [&world, position, options] { return buildSpider(world, position, options); });
        const EntityHandle spider = created.front();
        ctx.selection.set(spider);
        ctx.toast("Spider spawned; it walks toward a point ahead (set Walker move target to steer)");
    });
    addCommand("scene.ikTest", "Load IK test scene", "File", {}, [this] { replaceSceneWith([this] { buildIkTestScene(); }, "IK test scene"); });
    addCommand("ik.chain", "IK: create chain from selection (root to tip)", "IK", {}, [this] {
        EditorContext& ctx = *m_ctx;
        const std::vector<EntityHandle> joints = chainFromSelection(ctx);
        if (joints.size() < 2) {
            ctx.toast("Select a root joint (its first-child path is used) or several joints", ToastKind::Error);
            return;
        }
        const std::string targetName = ctx.world.getName(joints.front()) + ".target";
        const EntityHandle target = spawnIkChain(*ctx.history, joints, targetName, ctx.world.getWorldPosition(joints.back()));
        ctx.selection.set(target);
        ctx.toast("IK chain created; move the target entity to drive it");
    }, selectionRequired());
    addCommand("ik.walker", "IK: turn selected hierarchy into a walker (children named leg*)", "IK", {}, [this] {
        EditorContext& ctx = *m_ctx;
        if (makeWalkerFromHierarchy(ctx.world, ctx.primary())) ctx.toast("Walker created from leg* children");
        else ctx.toast("No leg* children with joint chains found under the selection", ToastKind::Error);
    }, selectionRequired());
    addCommand("ik.debug", "IK: draw chains, targets and foot placements", "IK", {}, [this] {
        m_engine.setSystemEnabled(kDebugSystem, !m_engine.isSystemEnabled(kDebugSystem));
    }).checked = [this] { return m_engine.isSystemEnabled(kDebugSystem); };
}
