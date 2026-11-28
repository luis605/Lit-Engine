module;

#include <GLFW/glfw3.h>
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
import Engine.Character;
import Engine.camera;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.OrbRunRules;

#include "OrbRunInternal.hpp"

namespace {

constexpr const char* kGameSystem = "testscene.orbrun.game";
constexpr const char* kHudSystem = "testscene.orbrun.hud";

std::unordered_map<const Engine*, std::shared_ptr<OrbRunGame>>& registry() {
    static std::unordered_map<const Engine*, std::shared_ptr<OrbRunGame>> games;
    return games;
}

}

OrbRunGame* orbRunGame(Engine& engine) {
    const auto found = registry().find(&engine);
    return found == registry().end() ? nullptr : found->second.get();
}

void releaseOrbRun(Engine& engine) { registry().erase(&engine); }

void buildOrbRun(Engine& engine, const OrbRunKit& kit) {
    auto game = std::make_shared<OrbRunGame>();
    registry()[&engine] = game;
    game->layout = orbRunLayout();
    game->state = orbRunStart(game->config);
    orbrun::Context context{engine, engine.world(), kit, *game};
    if (!engine.headless()) {
        context.looks.flame = engine.loadShader("Orb Run Flame", "resources/shaders/surfaces/flame.surface");
        context.looks.orb = engine.loadShader("Orb Run Orb", "resources/shaders/surfaces/orb.surface");
        context.looks.molten = engine.loadShader("Orb Run Molten", "resources/shaders/surfaces/molten.surface");
        context.looks.grass = engine.loadShader("Orb Run Grass", "resources/shaders/surfaces/grass.surface");
        context.looks.glowcap = engine.loadShader("Orb Run Glowcap", "resources/shaders/surfaces/glowcap.surface");
    }
    orbrun::buildHub(context);
    orbrun::buildEmber(context);
    orbrun::buildFrost(context);
    orbrun::buildGrotto(context);
    orbrun::buildCourtyard(context);
    orbrun::buildPortals(context);
    orbrun::buildOrbs(context);
    orbrun::buildSpiders(context);
    orbrun::buildCrates(context);
    orbrun::applyRenderSettings(engine);
    engine.addSystem(Phase::Update, kGameSystem, [&engine, game](World&, float deltaTime) { orbrun::tickGame(engine, *game, deltaTime); });
    engine.addSystem(Phase::Update, kHudSystem, [&engine, game](World&, float) { orbrun::drawHud(engine, *game); }, true);
}

void enableOrbRunPlay(Engine& engine) {
    OrbRunGame* game = orbRunGame(engine);
    if (!game) return;
    game->playable = true;
    engine.enableCharacter();
    respawnOrbRunPlayer(engine);
    game->respawns = 0;
    engine.setMouseCaptured(true);
}
