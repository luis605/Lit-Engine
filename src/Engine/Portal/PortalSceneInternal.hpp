#pragma once

// Shared pieces of the portal test scenes (PortalScene.cpp, PortalTiltScene.cpp): room shells with openings, props, lights.

namespace portal_scene {

struct Builder {
    PortalSystem& portals;
    World& world;
    const PortalSceneKit& kit;

    inline uint32_t material(const glm::vec3& color, float roughness = 0.6f, const glm::vec3& emissive = glm::vec3(0.0f), float intensity = 0.0f) const {
        return kit.material ? kit.material(color, emissive, intensity, roughness) : 0u;
    }

    inline EntityHandle box(const std::string& name, const glm::vec3& lo, const glm::vec3& hi, uint32_t mat, uint32_t shader = 0) const {
        EntityDesc desc;
        desc.name = name;
        desc.mesh = kit.cube;
        desc.material = mat;
        desc.shader = shader;
        desc.position = lo;
        desc.scale = hi - lo;
        return world.create(desc);
    }

    inline EntityHandle light(const std::string& name, const glm::vec3& position, const glm::vec3& color, float intensity, float range) const {
        EntityDesc desc;
        desc.name = name;
        desc.mesh = NO_MESH;
        desc.position = position;
        const EntityHandle entity = world.create(desc);
        LightComponent component;
        component.type = LightComponent::Type::Point;
        component.color = color;
        component.intensity = intensity;
        component.range = range;
        world.add<LightComponent>(entity, component);
        return entity;
    }
};

struct Opening {
    int wall;
    float along;
    float halfWidth;
    float top;
};

struct RoomStyle {
    glm::vec3 wall;
    glm::vec3 floor;
    glm::vec3 ceiling;
    uint32_t floorShader = 0;
};

inline void buildRoom(Builder& builder, const std::string& name, const glm::vec3& center, const glm::vec3& size, const RoomStyle& style, const std::vector<Opening>& openings) {
    const float thickness = 0.4f;
    const float halfX = size.x * 0.5f;
    const float halfZ = size.z * 0.5f;
    const float height = size.y;
    const uint32_t wallMat = builder.material(style.wall, 0.7f);
    const uint32_t floorMat = builder.material(style.floor, 0.55f);
    const uint32_t ceilMat = builder.material(style.ceiling, 0.8f);
    builder.box(name + " floor", center + glm::vec3(-halfX - thickness, -thickness, -halfZ - thickness), center + glm::vec3(halfX + thickness, 0.0f, halfZ + thickness), floorMat, style.floorShader);
    builder.box(name + " ceiling", center + glm::vec3(-halfX - thickness, height, -halfZ - thickness), center + glm::vec3(halfX + thickness, height + thickness, halfZ + thickness), ceilMat);
    for (int wall = 0; wall < 4; ++wall) {
        const bool alongX = wall >= 2;
        const float len = alongX ? size.x : size.z;
        const float pos = (wall == 0 ? halfX : wall == 1 ? -halfX : wall == 2 ? halfZ : -halfZ);
        std::vector<std::pair<float, float>> holes;
        float top = 0.0f;
        for (const Opening& opening : openings) {
            if (opening.wall != wall) continue;
            holes.emplace_back(opening.along - opening.halfWidth, opening.along + opening.halfWidth);
            top = opening.top;
        }
        std::ranges::sort(holes);
        auto slab = [&](float a0, float a1, float y0, float y1, const char* tag) {
            if (a1 - a0 < 1.0e-4f || y1 - y0 < 1.0e-4f) return;
            glm::vec3 lo;
            glm::vec3 hi;
            if (alongX) {
                lo = glm::vec3(a0 - thickness, y0, pos - thickness * 0.5f);
                hi = glm::vec3(a1 + thickness, y1, pos + thickness * 0.5f);
            } else {
                lo = glm::vec3(pos - thickness * 0.5f, y0, a0 - thickness);
                hi = glm::vec3(pos + thickness * 0.5f, y1, a1 + thickness);
            }
            if (a0 > -len * 0.5f + 1.0e-3f) {
                if (alongX) lo.x = a0;
                else lo.z = a0;
            }
            if (a1 < len * 0.5f - 1.0e-3f) {
                if (alongX) hi.x = a1;
                else hi.z = a1;
            }
            builder.box(name + " wall " + std::to_string(wall) + tag, center + lo, center + hi, wallMat);
        };
        float cursor = -len * 0.5f;
        for (const auto& [h0, h1] : holes) {
            slab(cursor, h0, 0.0f, height, " part");
            slab(h0, h1, top, height, " lintel");
            cursor = h1;
        }
        slab(cursor, len * 0.5f, 0.0f, height, " part");
    }
}

inline void props(Builder& builder, const std::string& name, const glm::vec3& center, const glm::vec3& accent, float scale) {
    const uint32_t stone = builder.material(glm::vec3(0.55f, 0.55f, 0.58f), 0.5f);
    const uint32_t glow = builder.material(accent * 0.4f, 0.3f, accent, 4000.0f);
    builder.box(name + " pedestal", center + glm::vec3(-0.5f, 0.0f, -0.5f) * scale, center + glm::vec3(0.5f, 0.9f, 0.5f) * scale, stone);
    builder.box(name + " core", center + glm::vec3(-0.3f, 0.9f, -0.3f) * scale, center + glm::vec3(0.3f, 1.5f, 0.3f) * scale, glow);
}

// Look of a test scene pair: metal frame with a glowing reveal and rim in the pair colour.
inline PortalLook accentLook(const glm::vec3& color, PortalShape shape = PortalShape::Rect) {
    PortalLook look;
    look.shape = shape;
    look.frame.color = glm::vec3(0.78f, 0.76f, 0.72f);
    look.frame.metalness = 0.6f;
    look.frame.roughness = 0.38f;
    look.frame.emissive = color;
    look.frame.emissiveIntensity = 2500.0f;
    look.edge.rimColor = color;
    return look;
}

// portal_tilt (PortalTiltScene.cpp): hub with a wall-to-floor, a ceiling, a 45 degree rolled and a 180 degree flipped portal.
void tiltScene(PortalSystem& portals, World& world, const PortalSceneKit& kit);

}
