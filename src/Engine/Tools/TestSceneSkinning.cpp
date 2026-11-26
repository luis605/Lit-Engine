module;

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

module Engine.TestScenes;

import Engine.engine;
import Engine.glm;
import Engine.World;
import Engine.Rig;
import Engine.Face;
import Engine.Skin;
import Engine.SkinRig;
import Engine.Render.entity;
import Engine.Render.component;

bool buildSkinningTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info);
uint32_t testSceneDynamicSource(Engine& engine, const std::string& key, const DynamicMesh& mesh);
uint32_t testSceneDynamicInstance(Engine& engine, uint32_t source);

namespace {

constexpr uint32_t kInvalidDynamic = 0xFFFFFFFFu;

struct Actor {
    uint32_t instance = kInvalidDynamic;
    EntityHandle entity;
    SkeletalAnimator animator;
    BlendshapePlayer face;
    BlendshapeWeights blendshapes = blendshapeZero();
    std::vector<glm::mat4> palette;
    std::vector<float> morph;
    Actor(const Skeleton& skeleton, const SkeletalLibrary& library) : animator(skeleton, library) {}
};

struct Cloth {
    uint32_t instance = kInvalidDynamic;
    DeformSettings settings;
};

struct SkinningLight {
    EntityHandle entity;
    glm::vec3 center;
};

struct SkinningState {
    std::vector<SkinningLight> lights;
    DynamicMesh character;
    DynamicMesh sheet;
    Skeleton skeleton;
    SkeletalLibrary clips;
    BlendshapeLibrary faceClips;
    std::vector<std::unique_ptr<Actor>> actors;
    std::vector<Cloth> cloths;
    SkinBounds characterBounds;
    float time = 0.0f;
    uint32_t ticks = 0;
    float maxBoundsRadius = 0.0f;
    float minBoundsRadius = 1.0e9f;
    float paletteMax = 0.0f;
    float morphMax = 0.0f;
};

}

bool buildSkinningTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info) {
    World& world = engine.world();
    auto state = std::make_shared<SkinningState>();
    state->character = makeProceduralCharacter();
    state->sheet = makeClothSheet(1.6f, 1.3f, 32, 24);
    state->skeleton = skeletonFromSkin(state->character);
    state->characterBounds = buildSkinBounds(state->character);
    const uint32_t walk = state->clips.add(makeWalkClip(state->skeleton));
    const uint32_t wave = state->clips.add(makeWaveClip(state->skeleton));
    const uint32_t talk = state->faceClips.add(makeTalkClip());

    auto material = [&](const glm::vec3& color, float roughness) { return kit.material ? kit.material(color, glm::vec3(0.0f), 0.0f, roughness) : 0u; };
    EntityDesc floor;
    floor.name = "skinning floor";
    floor.mesh = kit.cube;
    floor.material = material(glm::vec3(0.35f, 0.36f, 0.4f), 0.9f);
    floor.shader = kit.floorShader;
    floor.position = glm::vec3(-8.0f, -0.5f, -8.0f);
    floor.scale = glm::vec3(16.0f, 0.5f, 16.0f);
    world.create(floor);

    const uint32_t characterSource = testSceneDynamicSource(engine, "character", state->character);
    const uint32_t sheetSource = testSceneDynamicSource(engine, "cloth sheet 1.6x1.3", state->sheet);
    auto spawnDynamic = [&](const char* name, uint32_t source, uint32_t mat, const glm::vec3& position, bool twoSided, uint32_t& instanceOut) {
        const uint32_t instance = testSceneDynamicInstance(engine, source);
        instanceOut = instance;
        EntityDesc desc;
        desc.name = name;
        desc.mesh = instance != kInvalidDynamic ? engine.dynamicMeshId(instance) : 0u;
        desc.material = mat;
        desc.position = position;
        desc.scale = glm::vec3(1.0f);
        const EntityHandle entity = world.create(desc);
        if (instance != kInvalidDynamic) engine.setDynamicEntity(instance, entity);
        (void)twoSided;
        return entity;
    };

    const glm::vec3 positions[] = {glm::vec3(-1.3f, 0.0f, 0.0f), glm::vec3(1.3f, 0.0f, 0.0f)};
    const uint32_t clipIds[] = {walk, wave};
    const glm::vec3 colors[] = {glm::vec3(0.8f, 0.45f, 0.35f), glm::vec3(0.35f, 0.55f, 0.85f)};
    for (int i = 0; i < 2; ++i) {
        auto actor = std::make_unique<Actor>(state->skeleton, state->clips);
        actor->entity = spawnDynamic(i == 0 ? "skinned walker" : "skinned waver", characterSource, material(colors[i], 0.55f), positions[i], false, actor->instance);
        actor->animator.play(clipIds[i], 0.0f);
        actor->face.play(talk, i == 0 ? 1.0f : 0.7f);
        state->actors.push_back(std::move(actor));
    }

    const struct {
        const char* name;
        DeformMode mode;
        glm::vec3 position;
        glm::vec3 color;
    } cloths[] = {
        {"flag cloth", DeformMode::Cloth, glm::vec3(-2.6f, 2.6f, -2.0f), glm::vec3(0.85f, 0.15f, 0.15f)},
        {"wave sheet", DeformMode::Wave, glm::vec3(0.0f, 2.2f, -2.0f), glm::vec3(0.2f, 0.7f, 0.4f)},
        {"user twist sheet", DeformMode::User, glm::vec3(2.6f, 2.6f, -2.0f), glm::vec3(0.9f, 0.75f, 0.2f)},
    };
    for (const auto& entry : cloths) {
        Cloth cloth;
        spawnDynamic(entry.name, sheetSource, material(entry.color, 0.6f), entry.position, true, cloth.instance);
        cloth.settings.mode = entry.mode;
        cloth.settings.amplitude = entry.mode == DeformMode::Cloth ? 0.12f : 0.08f;
        cloth.settings.frequency = 5.0f;
        cloth.settings.speed = 3.0f;
        cloth.settings.pinY = 0.0f;
        cloth.settings.pinRange = 1.3f;
        cloth.settings.direction = glm::vec3(1.0f, 0.0f, 0.4f);
        if (entry.mode == DeformMode::User) {
            cloth.settings.userShader = "resources/shaders/deform/twist_wave.comp";
            cloth.settings.user[0] = glm::vec4(0.9f, 0.1f, 2.0f, 0.0f);
            cloth.settings.userBounds = 0.7f;
            cloth.settings.recomputeNormals = true;
        }
        state->cloths.push_back(std::move(cloth));
    }
    for (int i = 0; i < 3; ++i) {
        EntityDesc pole;
        pole.name = "pole";
        pole.mesh = kit.cube;
        pole.material = material(glm::vec3(0.3f, 0.3f, 0.3f), 0.8f);
        pole.position = glm::vec3(-3.4f + 2.6f * static_cast<float>(i), 0.0f, -2.05f);
        pole.scale = glm::vec3(0.05f, i == 1 ? 2.2f : 2.62f, 0.05f);
        world.create(pole);
    }

    EntityDesc wall;
    wall.name = "skinning back wall";
    wall.mesh = kit.cube;
    wall.material = material(glm::vec3(0.7f, 0.7f, 0.72f), 0.9f);
    wall.position = glm::vec3(-6.0f, 0.0f, -3.2f);
    wall.scale = glm::vec3(12.0f, 4.0f, 0.2f);
    world.create(wall);
    struct LightSpec {
        LightComponent::Type type;
        glm::vec3 position;
        glm::vec3 color;
        float intensity;
        float range;
        float pitch;
    };
    const LightSpec lightSpecs[] = {
        {LightComponent::Type::Spot, glm::vec3(0.0f, 3.6f, 3.6f), glm::vec3(1.0f, 0.95f, 0.85f), 2000000.0f, 14.0f, -38.0f},
        {LightComponent::Type::Point, glm::vec3(-3.0f, 2.2f, 1.8f), glm::vec3(1.0f, 0.5f, 0.3f), 400000.0f, 9.0f, 0.0f},
        {LightComponent::Type::Point, glm::vec3(3.0f, 2.2f, 1.8f), glm::vec3(0.4f, 0.6f, 1.0f), 400000.0f, 9.0f, 0.0f},
    };
    for (const LightSpec& spec : lightSpecs) {
        EntityDesc lampDesc;
        lampDesc.name = "skinning light";
        lampDesc.mesh = NO_MESH;
        lampDesc.position = spec.position;
        if (spec.type == LightComponent::Type::Spot) lampDesc.rotation = glm::angleAxis(glm::radians(spec.pitch), glm::vec3(1.0f, 0.0f, 0.0f));
        const EntityHandle lamp = world.create(lampDesc);
        LightComponent component;
        component.type = spec.type;
        component.color = spec.color;
        component.intensity = spec.intensity;
        component.range = spec.range;
        component.innerConeDegrees = 30.0f;
        component.outerConeDegrees = 55.0f;
        world.add<LightComponent>(lamp, component);
        state->lights.push_back({lamp, spec.position});
    }

    Engine* enginePointer = &engine;
    engine.addSystem(Phase::Update, "testscene.skinning", [state, enginePointer](World& world, float dt) {
        state->time += dt;
        ++state->ticks;
        for (auto& actor : state->actors) {
            actor->animator.update(dt);
            computeSkinPalette(state->skeleton, actor->animator.pose(), state->character, actor->palette);
            actor->face.update(dt, state->faceClips, actor->blendshapes);
            morphWeightsFromBlendshapes(state->character, actor->blendshapes, actor->morph);
            DeformSettings none;
            none.recomputeNormals = false;
            for (const glm::mat4& matrix : actor->palette) state->paletteMax = std::max(state->paletteMax, std::fabs(matrix[3].y));
            for (float weight : actor->morph) state->morphMax = std::max(state->morphMax, weight);
            const glm::vec4 bounds = computeDynamicBounds(state->characterBounds, actor->palette, actor->morph, none);
            state->maxBoundsRadius = std::max(state->maxBoundsRadius, bounds.w);
            state->minBoundsRadius = std::min(state->minBoundsRadius, bounds.w);
            if (actor->instance == kInvalidDynamic) continue;
            enginePointer->setDynamicDeform(actor->instance, none);
            enginePointer->setDynamicPose(actor->instance, actor->palette);
            enginePointer->setDynamicMorph(actor->instance, actor->morph);
        }
        for (size_t i = 1; i < state->lights.size(); ++i) {
            const float angle = state->time * 0.6f + static_cast<float>(i) * 2.1f;
            const glm::vec3 position = state->lights[i].center + glm::vec3(std::cos(angle) * 0.8f, 0.0f, std::sin(angle) * 0.8f);
            world.setPosition(state->lights[i].entity, position);
        }
        for (Cloth& cloth : state->cloths) {
            cloth.settings.time = state->time;
            if (cloth.instance != kInvalidDynamic) enginePointer->setDynamicDeform(cloth.instance, cloth.settings);
        }
    });

    info.start = glm::vec3(0.0f, 1.7f, 6.5f);
    info.yaw = -90.0f;
    info.pitch = -6.0f;
    info.nearPlane = 0.1f;
    info.farPlane = 200.0f;
    info.suggestedTicks = 300;
    info.metrics = [state, enginePointer]() {
        TestSceneMetrics metrics;
        metrics.set("local_lights", static_cast<double>(state->lights.size()));
        metrics.set("dynamic_instances", static_cast<double>(state->actors.size() + state->cloths.size()));
        metrics.set("joints", static_cast<double>(state->character.joints.size()));
        metrics.set("character_vertices", static_cast<double>(state->character.vertexCount()));
        metrics.set("morph_targets", static_cast<double>(state->character.morphs.size()));
        metrics.set("palette_max_translation_y", state->paletteMax);
        metrics.set("morph_weight_max", state->morphMax);
        metrics.set("bounds_radius_min", state->minBoundsRadius);
        metrics.set("bounds_radius_max", state->maxBoundsRadius);
        metrics.set("gpu_dispatches_last_frame", static_cast<double>(enginePointer->dynamicStats().dispatches));
        metrics.set("ticks", state->ticks);
        return metrics;
    };
    return true;
}
