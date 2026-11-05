module;

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

module Engine.World;

import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.glm;

namespace {

constexpr uint32_t kMaxLightType = 2;
constexpr float kMinCameraFov = 1.0f;
constexpr float kMaxCameraFov = 179.0f;

std::vector<EntityHandle> createPrefabEntities(World& world, const Prefab& prefab, EntityHandle parent, std::unordered_map<uint32_t, EntityHandle>& remap) {
    std::vector<EntityHandle> created;
    created.reserve(prefab.nodes.size());
    for (const PrefabNode& node : prefab.nodes) {
        EntityDesc desc;
        desc.name = node.name;
        desc.mesh = node.mesh;
        desc.material = node.material;
        desc.shader = node.shader;
        desc.alpha = node.alpha;
        const bool hasParentNode = node.parent >= 0 && static_cast<size_t>(node.parent) < created.size();
        desc.parent = hasParentNode ? created[static_cast<size_t>(node.parent)] : parent;
        const EntityHandle handle = world.create(desc);
        world.setLocalMatrix(handle, node.local);
        if (!node.visible) world.setVisible(handle, false);
        world.setLayer(handle, node.layer);
        world.setRenderFlags(handle, node.renderFlags, true);
        world.setTag(handle, node.tag);
        created.push_back(handle);
        remap[node.sourceIndex] = handle;
    }
    return created;
}

void writeLight(const LightComponent& light, std::ostream& out) {
    out << static_cast<uint32_t>(light.type) << ' ' << light.color.x << ' ' << light.color.y << ' ' << light.color.z << ' ' << light.intensity << ' ' << light.range << ' ' << light.specular << ' ' << light.innerConeDegrees << ' ' << light.outerConeDegrees << ' ' << light.sourceRadius << ' ' << light.sourceLength;
}

std::optional<LightComponent> readLight(std::istream& in) {
    LightComponent light;
    uint32_t type = 0;
    if (!(in >> type >> light.color.x >> light.color.y >> light.color.z >> light.intensity >> light.range)) return std::nullopt;
    if (type > kMaxLightType) return std::nullopt;
    light.type = static_cast<LightComponent::Type>(type);
    if (!(in >> light.specular >> light.innerConeDegrees >> light.outerConeDegrees)) {
        const LightComponent defaults;
        light.specular = defaults.specular;
        light.innerConeDegrees = defaults.innerConeDegrees;
        light.outerConeDegrees = defaults.outerConeDegrees;
    }
    if (!(in >> light.sourceRadius >> light.sourceLength)) {
        in.clear();
        light.sourceRadius = 0.0f;
        light.sourceLength = 0.0f;
    }
    return light;
}

void writeReflectionProbe(const ReflectionProbeComponent& probe, std::ostream& out) { out << probe.halfExtents.x << ' ' << probe.halfExtents.y << ' ' << probe.halfExtents.z << ' ' << probe.intensity << ' ' << probe.blendDistance << ' ' << probe.resolution; }

std::optional<ReflectionProbeComponent> readReflectionProbe(std::istream& in) {
    ReflectionProbeComponent probe;
    if (!(in >> probe.halfExtents.x >> probe.halfExtents.y >> probe.halfExtents.z >> probe.intensity >> probe.blendDistance)) return std::nullopt;
    if (!(in >> probe.resolution)) {
        in.clear();
        probe.resolution = ReflectionProbeComponent{}.resolution;
    }
    if (!(probe.halfExtents.x > 0.0f && probe.halfExtents.y > 0.0f && probe.halfExtents.z > 0.0f)) return std::nullopt;
    return probe;
}

void writePlanarReflection(const PlanarReflectionComponent& plane, std::ostream& out) { out << plane.halfSize.x << ' ' << plane.halfSize.y; }

std::optional<PlanarReflectionComponent> readPlanarReflection(std::istream& in) {
    PlanarReflectionComponent plane;
    if (!(in >> plane.halfSize.x >> plane.halfSize.y)) return std::nullopt;
    if (!(plane.halfSize.x > 0.0f && plane.halfSize.y > 0.0f)) return std::nullopt;
    return plane;
}

void writeCamera(const CameraComponent& camera, std::ostream& out) { out << camera.fov << ' ' << camera.nearPlane << ' ' << camera.farPlane; }

std::optional<CameraComponent> readCamera(std::istream& in) {
    CameraComponent camera;
    if (!(in >> camera.fov >> camera.nearPlane >> camera.farPlane)) return std::nullopt;
    if (!(camera.fov > kMinCameraFov && camera.fov < kMaxCameraFov && camera.nearPlane > 0.0f && camera.farPlane > camera.nearPlane)) return std::nullopt;
    return camera;
}

}

void registerCoreComponents(World& world) {
    world.registerComponent<LightComponent>("Light", writeLight, readLight);
    world.registerComponent<CameraComponent>("Camera", writeCamera, readCamera);
    world.registerComponent<ReflectionProbeComponent>("ReflectionProbe", writeReflectionProbe, readReflectionProbe);
    world.registerComponent<PlanarReflectionComponent>("PlanarReflection", writePlanarReflection, readPlanarReflection);
}

std::vector<std::pair<std::string, std::string>> World::serializeScripts(Entity index) const {
    std::vector<std::pair<std::string, std::string>> serialized;
    const auto scriptsIt = m_scripts.find(index);
    if (scriptsIt == m_scripts.end()) return serialized;
    for (const auto& script : scriptsIt->second) {
        const auto nameIt = m_scriptNames.find(std::type_index(typeid(*script)));
        if (nameIt == m_scriptNames.end()) continue;
        std::ostringstream payload;
        if (m_scriptSerializers.at(nameIt->second).write(*script, payload)) serialized.emplace_back(nameIt->second, payload.str());
    }
    return serialized;
}

Prefab World::capture(EntityHandle root) const {
    Prefab prefab;
    if (!valid(root)) return prefab;

    std::vector<std::pair<Entity, int>> stack{{root.index, -1}};
    while (!stack.empty()) {
        const auto [index, parentNode] = stack.back();
        stack.pop_back();

        PrefabNode node;
        const RenderableComponent& renderable = m_db.renderables[index];
        node.name = m_names.get(index);
        node.mesh = m_mesh[index];
        node.material = renderable.material_uuid;
        node.shader = renderable.shaderId;
        node.alpha = renderable.alpha;
        node.visible = m_visible[index] != 0;
        node.layer = m_layer[index];
        node.renderFlags = renderable.flags & ~RENDER_HIDDEN;
        node.tag = m_tags.get(index);
        node.local = m_db.transforms[index].localMatrix;
        node.parent = parentNode;
        node.sourceIndex = index;
        node.sourceGeneration = m_generation[index];
        for (const auto& [name, serializer] : m_serializers) {
            const auto poolIt = m_pools.find(serializer.type);
            if (poolIt == m_pools.end()) continue;
            std::ostringstream payload;
            serializer.write(*poolIt->second, payload, index);
            std::string text = payload.str();
            if (!text.empty()) node.components.emplace_back(name, std::move(text));
        }
        node.scripts = serializeScripts(index);

        const int nodeIndex = static_cast<int>(prefab.nodes.size());
        prefab.nodes.push_back(std::move(node));
        for (Entity child = m_db.firstChild[index]; child != INVALID_ENTITY; child = m_db.nextSibling[child]) stack.emplace_back(child, nodeIndex);
    }
    return prefab;
}

EntityHandle World::instantiate(const Prefab& prefab, EntityHandle parent, std::vector<EntityHandle>* createdOut) {
    std::unordered_map<uint32_t, EntityHandle> remap;
    const std::vector<EntityHandle> created = createPrefabEntities(*this, prefab, parent, remap);

    LoadContext context;
    context.resolveFn = [this, &remap](uint32_t sourceIndex) {
        const auto remapped = remap.find(sourceIndex);
        return remapped != remap.end() ? remapped->second : handleOf(sourceIndex);
    };
    for (size_t nodeIndex = 0; nodeIndex < prefab.nodes.size(); ++nodeIndex) {
        const PrefabNode& node = prefab.nodes[nodeIndex];
        for (const auto& [name, payload] : node.components) {
            const auto serializer = m_serializers.find(name);
            if (serializer == m_serializers.end()) continue;
            std::istringstream in(payload);
            serializer->second.read(*this, created[nodeIndex], in, context);
        }
        for (const auto& [name, payload] : node.scripts) {
            const auto serializer = m_scriptSerializers.find(name);
            if (serializer == m_scriptSerializers.end()) continue;
            std::istringstream in(payload);
            serializer->second.read(*this, created[nodeIndex], in, context);
        }
    }
    if (createdOut) *createdOut = created;
    return created.empty() ? NULL_ENTITY : created.front();
}

std::vector<std::string> World::componentNames() const {
    std::vector<std::string> names;
    names.reserve(m_serializers.size());
    for (const auto& [name, serializer] : m_serializers) names.push_back(name);
    return names;
}

std::optional<std::string> World::componentText(EntityHandle entity, const std::string& componentName) const {
    if (!valid(entity)) return std::nullopt;
    const auto serializer = m_serializers.find(componentName);
    if (serializer == m_serializers.end()) return std::nullopt;
    const auto poolIt = m_pools.find(serializer->second.type);
    if (poolIt == m_pools.end() || !poolIt->second->has(entity.index)) return std::nullopt;
    std::ostringstream payload;
    serializer->second.write(*poolIt->second, payload, entity.index);
    return payload.str();
}

EntityDescription World::describeEntity(EntityHandle entity) const {
    EntityDescription description;
    if (!valid(entity)) return description;
    description.name = m_names.get(entity.index);
    description.tag = m_tags.get(entity.index);
    description.layer = m_layer[entity.index];
    description.mesh = m_mesh[entity.index];
    description.visible = m_visible[entity.index] != 0;
    description.parent = getParent(entity);
    for (Entity child = m_db.firstChild[entity.index]; child != INVALID_ENTITY; child = m_db.nextSibling[child]) ++description.childCount;
    for (const auto& [name, serializer] : m_serializers) {
        if (const auto text = componentText(entity, name)) description.components.emplace_back(name, *text);
    }
    description.scripts = serializeScripts(entity.index);
    return description;
}

bool World::setComponentFromText(EntityHandle entity, const std::string& componentName, const std::string& text) {
    if (!valid(entity)) return false;
    const auto serializer = m_serializers.find(componentName);
    if (serializer == m_serializers.end()) return false;
    LoadContext context;
    context.resolveFn = [this](uint32_t savedIndex) { return handleOf(savedIndex); };
    std::istringstream in(text);
    return serializer->second.read(*this, entity, in, context);
}

bool World::addComponentByName(EntityHandle entity, const std::string& componentName) {
    if (!valid(entity)) return false;
    const auto serializer = m_serializers.find(componentName);
    if (serializer == m_serializers.end() || !serializer->second.create) return false;
    return serializer->second.create(*this, entity);
}

bool World::removeComponentByName(EntityHandle entity, const std::string& componentName) {
    if (!valid(entity)) return false;
    const auto serializer = m_serializers.find(componentName);
    if (serializer == m_serializers.end()) return false;
    const auto poolIt = m_pools.find(serializer->second.type);
    if (poolIt == m_pools.end() || !poolIt->second->has(entity.index)) return false;
    poolIt->second->remove(entity.index);
    return true;
}
