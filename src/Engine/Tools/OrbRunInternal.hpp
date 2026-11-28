#pragma once

#include "OrbRunGeometry.hpp"

namespace orbrun {

inline constexpr float kWallThickness = 0.4f;
inline constexpr float kPortalInset = 0.01f;
inline constexpr float kDoorCenterY = 1.65f;
inline constexpr float kPortholeCenterY = 1.6f;
inline constexpr float kMinSlab = 1.0e-3f;
inline constexpr float kCellMargin = 0.45f;
inline constexpr float kEyeHeight = 1.6f;
inline constexpr float kBodyCenterHeight = 0.9f;
inline constexpr float kCourtyardX = 400.0f;
inline constexpr float kCourtyardArchZ = 22.0f;
inline constexpr float kPlazaHalf = 28.0f;
inline constexpr float kFlameOpacity = 0.95f;

enum class Wall { East, West, North, South };

enum class Finish { Plaster, Masonry, Flagstone, Tiles, Rock, Wood, Ground };

struct SurfaceMapping {
    float tileMeters = 2.0f;
    bool worldAligned = true;
    float normalStrength = 1.0f;
};

[[nodiscard]] inline float finishRelief(Finish finish) {
    constexpr float kReliefMeters[] = {0.0f, 0.015f, 0.013f, 0.004f, 0.022f, 0.0f, 0.0f};
    return kReliefMeters[static_cast<int>(finish)];
}

[[nodiscard]] inline const char* finishName(Finish finish) {
    constexpr const char* kNames[] = {"plaster", "masonry", "flagstone", "tiles", "rock", "wood", "ground"};
    return kNames[static_cast<int>(finish)];
}

[[nodiscard]] inline std::string finishPath(Finish finish, const char* channel) { return std::string("resources/textures/pbr/") + finishName(finish) + "_" + channel + ".png"; }

struct Room {
    std::string name;
    glm::vec3 center{0.0f};
    glm::vec3 size{1.0f};
};

struct RoomStyle {
    glm::vec3 wall{0.7f};
    glm::vec3 floor{0.4f};
    glm::vec3 ceiling{0.8f};
    float floorRoughness = 0.5f;
    Finish floorFinish = Finish::Tiles;
    Finish wallFinish = Finish::Plaster;
    float floorTileMeters = 3.0f;
    glm::vec3 trim{0.4f};
    bool framed = true;
};

struct PairSpec {
    std::string name;
    PortalEnd a;
    PortalEnd b;
    PortalLook look;
};

struct OrbRunLooks {
    uint32_t flame = 0;
    uint32_t orb = 0;
    uint32_t molten = 0;
    uint32_t grass = 0;
    uint32_t glowcap = 0;
};

struct Context {
    Engine& engine;
    World& world;
    const OrbRunKit& kit;
    OrbRunGame& game;
    OrbRunLooks looks{};

    inline uint32_t mesh(const std::string& key, const MeshData& data) const { return kit.portal.mesh ? kit.portal.mesh("orbrun." + key, data.vertices, data.indices) : kit.portal.cube; }

    inline EntityHandle prop(const std::string& name, uint32_t surface, uint32_t shape, const glm::vec3& position, const glm::vec3& scale = glm::vec3(1.0f), const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), uint32_t shader = 0) const {
        EntityDesc description;
        description.name = name;
        description.mesh = shape;
        description.material = surface;
        description.shader = shader;
        description.position = position;
        description.scale = scale;
        description.rotation = rotation;
        return world.create(description);
    }

    inline EntityHandle rounded(const std::string& name, uint32_t surface, const glm::vec3& low, const glm::vec3& high, float bevel = 0.05f, float roughen = 0.0f, uint32_t shader = 0) const {
        const glm::vec3 half = (high - low) * 0.5f;
        const std::string key = "box." + std::to_string(half.x) + "." + std::to_string(half.y) + "." + std::to_string(half.z) + "." + std::to_string(bevel) + "." + std::to_string(roughen);
        return prop(name, surface, mesh(key, roundedBoxMesh(half, bevel, roughen)), low + half, glm::vec3(1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), shader);
    }

    inline EntityHandle roundedBlock(const std::string& name, uint32_t surface, const glm::vec3& low, const glm::vec3& high, float bevel = 0.05f, float roughen = 0.0f) const {
        solid(low, high);
        return rounded(name, surface, low, high, bevel, roughen);
    }

    inline uint32_t material(const Material& material) const { return kit.material ? kit.material(material) : 0u; }

    inline uint32_t paint(const glm::vec3& color, float roughness, float metallic = 0.0f) const {
        Material description;
        description.baseColor = color;
        description.roughness = roughness;
        description.metallic = metallic;
        return material(description);
    }

    inline uint32_t surfaced(Finish finish, const glm::vec3& tint, float roughness, const SurfaceMapping& mapping = {}) const {
        Material description;
        description.baseColor = tint;
        description.roughness = roughness;
        description.triplanar = mapping.worldAligned;
        description.uvScale = 1.0f / mapping.tileMeters;
        description.normalStrength = mapping.normalStrength;
        description.parallaxDepth = mapping.worldAligned ? finishRelief(finish) : 0.0f;
        if (!engine.headless()) {
            description.baseColorTexture = engine.loadTexture(finishPath(finish, "albedo"), true);
            description.normalTexture = engine.loadTexture(finishPath(finish, "normal"), false);
            description.ormTexture = engine.loadTexture(finishPath(finish, "orm"), false);
        }
        return material(description);
    }

    inline uint32_t glow(const glm::vec3& color, float intensity) const {
        Material description;
        description.baseColor = color * 0.4f;
        description.emissive = color;
        description.emissiveIntensity = intensity;
        description.roughness = 0.35f;
        return material(description);
    }

    inline uint32_t fire(const glm::vec3& color, float intensity) const {
        Material description;
        description.baseColor = glm::vec3(0.0f);
        description.opacity = kFlameOpacity;
        description.emissive = color;
        description.emissiveIntensity = intensity;
        description.roughness = 1.0f;
        return material(description);
    }

    inline uint32_t glass(const glm::vec3& tint) const {
        Material description;
        description.baseColor = tint;
        description.roughness = 0.03f;
        description.transmission = 1.0f;
        description.ior = 1.5f;
        description.thickness = 0.5f;
        return material(description);
    }

    inline EntityHandle visual(const std::string& name, uint32_t surface, const glm::vec3& low, const glm::vec3& high, uint32_t shader = 0) const {
        EntityDesc description;
        description.name = name;
        description.mesh = kit.portal.cube;
        description.material = surface;
        description.shader = shader;
        description.position = low;
        description.scale = high - low;
        return world.create(description);
    }

    inline void solid(const glm::vec3& low, const glm::vec3& high) const {
        EntityDesc description;
        description.name = "collider";
        description.position = (low + high) * 0.5f;
        const EntityHandle collider = world.create(description);
        world.add<BoxCollider>(collider, BoxCollider{(high - low) * 0.5f});
        RigidBody body;
        body.isStatic = true;
        world.add<RigidBody>(collider, body);
    }

    inline EntityHandle block(const std::string& name, uint32_t surface, const glm::vec3& low, const glm::vec3& high, uint32_t shader = 0) const {
        solid(low, high);
        return visual(name, surface, low, high, shader);
    }

    inline EntityHandle ball(const std::string& name, uint32_t surface, const glm::vec3& center, float radius) const {
        EntityDesc description;
        description.name = name;
        description.mesh = kit.portal.sphere;
        description.material = surface;
        description.position = center;
        description.scale = glm::vec3(radius);
        return world.create(description);
    }

    inline EntityHandle lamp(const std::string& name, const glm::vec3& position, const glm::vec3& color, float intensity, float range, float radius = 0.2f, float length = 0.0f) const {
        EntityDesc description;
        description.name = name;
        description.position = position;
        const EntityHandle entity = world.create(description);
        LightComponent light;
        light.type = LightComponent::Type::Point;
        light.color = color;
        light.intensity = intensity;
        light.range = range;
        light.sourceRadius = radius;
        light.sourceLength = length;
        world.add<LightComponent>(entity, light);
        return entity;
    }

    inline void probe(const std::string& name, const glm::vec3& center, const glm::vec3& halfExtents) const {
        EntityDesc description;
        description.name = name;
        description.position = center;
        ReflectionProbeComponent component;
        component.halfExtents = halfExtents;
        component.blendDistance = 2.0f;
        world.add<ReflectionProbeComponent>(world.create(description), component);
    }
};

[[nodiscard]] Room hubRoom();
[[nodiscard]] Room emberRoom();
[[nodiscard]] Room frostRoom();
[[nodiscard]] Room grottoRoom();
[[nodiscard]] glm::vec3 interiorMin(const Room& room);
[[nodiscard]] glm::vec3 interiorMax(const Room& room);
[[nodiscard]] glm::vec3 inRoom(const Room& room, float x, float y, float z);
[[nodiscard]] std::vector<PairSpec> portalPairs();
[[nodiscard]] PortalLook portalLook(const glm::vec3& accent, PortalShape shape, float frameWidth);

void buildShell(const Context& context, const Room& room, const RoomStyle& style, uint32_t floorShader);
void buildBrazierBowl(const Context& context, const glm::vec3& top, uint32_t flame, float scale);
void buildPillarTrim(const Context& context, uint32_t stone, const glm::vec3& base, float size, float height);
void decorateEmber(const Context& context, const Room& room);
void decorateFrost(const Context& context, const Room& room);
void decorateGrotto(const Context& context, const Room& room);
void scatterGrass(const Context& context, const glm::vec3& center, const glm::vec2& half, int count, float clearHalf);
void buildHub(const Context& context);
void buildEmber(const Context& context);
void buildFrost(const Context& context);
void buildGrotto(const Context& context);
void buildCourtyard(const Context& context);
void buildPortals(const Context& context);
void buildSpiders(const Context& context);
void buildCrates(const Context& context);
void buildOrbs(const Context& context);
void spawnOrb(World& world, uint32_t sphereMesh, uint32_t shader, OrbRunOrb& orb);
void applyRenderSettings(Engine& engine);

struct PlayerPose {
    glm::vec3 eye{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 feet{0.0f};
    glm::vec3 center{0.0f};
    int cell = -1;
};

[[nodiscard]] PlayerPose playerPose(Engine& engine);
void steerSpiders(Engine& engine, OrbRunGame& game, const PlayerPose& pose);
void tickGame(Engine& engine, OrbRunGame& game, float deltaTime);
void drawHud(Engine& engine, const OrbRunGame& game);

}
