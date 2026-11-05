module;

#include <cmath>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

module Engine.World;

import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.Profiler;
import Engine.camera;
import Engine.glm;

#include "Engine/World/WorldInternal.hpp"

namespace {

constexpr size_t kEntityChunk = 1 << 16;
constexpr size_t kBytesPerEntityEstimate = 96;
constexpr size_t kCameraKeywordLength = 7;
constexpr int kMatrixElementCount = 16;
constexpr std::streamsize kSceneFloatPrecision = 9;

std::string buildCameraLine(const Camera& camera) {
    std::string line = "camera";
    const glm::vec3 position = camera.getPosition();
    for (float value : {position.x, position.y, position.z, camera.getYaw(), camera.getPitch(), camera.getFov(), camera.getNearPlane(), camera.getFarPlane()}) {
        line.push_back(' ');
        appendNumber(line, value);
    }
    return line;
}

}

void World::clear() {
    m_db.transforms.clear();
    m_db.hierarchies.clear();
    m_db.renderables.clear();
    m_db.sortedHierarchyList.clear();
    m_db.transformDirty.reset();
    m_db.renderableDirty.reset();
    m_db.structureDirty.reset();
    touchStructure();
    m_alive.clear();
    for (uint32_t generation : m_generation) m_generationBase = std::max(m_generationBase, generation + 1);
    m_generation.clear();
    m_visible.clear();
    m_mesh.clear();
    m_layer.clear();
    m_position.clear();
    m_tags.clear();
    m_tagIndex.clear();
    m_db.firstChild.clear();
    m_db.nextSibling.clear();
    m_prevSibling.clear();
    m_names.clear();
    m_freeList.clear();
    m_worldCache.clear();
    m_worldDirty.clear();
    m_spatial.clear();
    m_spatialQueued.clear();
    m_spatialPending.clear();
    m_spatialRoots.clear();
    m_spatialRootQueued.clear();
    m_cells.clear();
    m_coarseCells.clear();
    m_largeEntities.clear();
    m_spatialActive = false;
    m_maxSmallRadius = 0.0f;
    m_scripts.clear();
    for (auto& [type, componentPool] : m_pools) componentPool->clear();
    m_pendingDestroy.clear();
    m_pendingRemoveScripts.clear();
    m_timers.clear();
    m_firstRoot = INVALID_ENTITY;
    m_aliveCount = 0;
}

void World::appendEntityLine(std::string& line, Entity index) const {
    const RenderableComponent& renderable = m_db.renderables[index];
    const Entity parent = m_db.hierarchies[index].parent;
    appendNumber(line, index);
    line.push_back(' ');
    appendNumber(line, parent == INVALID_ENTITY ? -1LL : static_cast<long long>(parent));
    line.push_back(' ');
    appendNumber(line, static_cast<int>(m_visible[index]));
    line.push_back(' ');
    appendNumber(line, m_mesh[index]);
    line.push_back(' ');
    appendNumber(line, renderable.material_uuid);
    line.push_back(' ');
    appendNumber(line, renderable.shaderId);
    line.push_back(' ');
    appendNumber(line, renderable.alpha);
    const float* matrix = &m_db.transforms[index].localMatrix[0][0];
    for (int element = 0; element < kMatrixElementCount; ++element) {
        line.push_back(' ');
        appendNumber(line, matrix[element]);
    }
    line.push_back(' ');
    appendNumber(line, m_layer[index]);
    line.push_back(' ');
    appendNumber(line, renderable.flags & ~RENDER_HIDDEN);
    line.push_back('\t');
    line.append(m_names.get(index));
    line.push_back('\n');
}

void World::appendEntityLines(std::string& out) const {
    const size_t slots = m_alive.size();
    const size_t chunks = (slots + kEntityChunk - 1) / kEntityChunk;
    out.reserve(out.size() + m_aliveCount * kBytesPerEntityEstimate);
    if (m_jobs && chunks > 1) {
        std::vector<std::string> buffers(chunks);
        m_jobs->parallelFor(chunks, 1, [&](size_t begin, size_t end) {
            for (size_t chunk = begin; chunk < end; ++chunk) {
                std::string& buffer = buffers[chunk];
                buffer.reserve(kEntityChunk * kBytesPerEntityEstimate);
                const size_t last = std::min(slots, (chunk + 1) * kEntityChunk);
                for (size_t slot = chunk * kEntityChunk; slot < last; ++slot) {
                    if (m_alive[slot]) appendEntityLine(buffer, static_cast<Entity>(slot));
                }
            }
        });
        for (const std::string& buffer : buffers) out.append(buffer);
        return;
    }
    for (Entity index = 0; index < slots; ++index) {
        if (m_alive[index]) appendEntityLine(out, index);
    }
}

void World::appendMeshTable(std::string& out) const {
    if (!m_meshNameOf) return;
    std::vector<uint32_t> usedMeshes;
    for (Entity index = 0; index < m_alive.size(); ++index) {
        if (m_alive[index]) usedMeshes.push_back(m_mesh[index]);
    }
    std::sort(usedMeshes.begin(), usedMeshes.end());
    usedMeshes.erase(std::unique(usedMeshes.begin(), usedMeshes.end()), usedMeshes.end());
    for (uint32_t meshId : usedMeshes) {
        const std::string name = m_meshNameOf(meshId);
        if (name.empty()) continue;
        out.append("mesh ");
        appendNumber(out, meshId);
        out.push_back('\t');
        out.append(name);
        out.push_back('\n');
    }
}

void World::appendSections(std::string& out) const {
    std::ostringstream stream;
    stream.precision(kSceneFloatPrecision);
    for (const SceneSection& section : m_sceneSections) {
        if (section.write) section.write(stream);
    }
    out.append(stream.view());
}

void World::appendTags(std::string& out) const {
    for (Entity index = 0; index < m_alive.size(); ++index) {
        if (!m_alive[index]) continue;
        const std::string_view tag = m_tags.get(index);
        if (tag.empty()) continue;
        out.append("tag ");
        appendNumber(out, index);
        out.push_back('\t');
        out.append(tag);
        out.push_back('\n');
    }
}

void World::appendScene(std::string& out) const {
    out.append("LITSCENE ");
    appendNumber(out, kSceneVersion);
    out.push_back('\n');
    appendNumber(out, m_aliveCount);
    out.push_back('\n');
    out.append(buildCameraLine(m_camera));
    out.push_back('\n');
    appendMeshTable(out);
    appendSections(out);
    appendEntityLines(out);
    appendTags(out);
    std::ostringstream extras;
    extras.precision(kSceneFloatPrecision);
    writeExtras(extras);
    out.append(extras.view());
}

bool World::saveScene(const std::filesystem::path& path) const {
    std::ofstream out(path);
    if (!out) return false;
    return saveScene(out);
}

bool World::saveScene(std::ostream& out) const {
    std::optional<ProfileScope> profile;
    if (m_profiler) profile.emplace(*m_profiler, "World::saveScene");
    std::string text;
    appendScene(text);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

void World::applyExtraLines(const std::vector<std::string>& componentLines, const LoadContext& context) {
    for (const std::string& componentLine : componentLines) {
        const size_t tab = componentLine.find('\t');
        if (tab == std::string::npos) continue;
        std::istringstream head(componentLine.substr(0, tab));
        std::string keyword, name;
        long long id = -1;
        if (componentLine.rfind("tag ", 0) == 0) {
            head >> keyword >> id;
            const EntityHandle handle = id < 0 ? NULL_ENTITY : context.resolve(static_cast<uint32_t>(id));
            if (head && !handle.isNull()) setTag(handle, componentLine.substr(tab + 1));
            continue;
        }
        head >> keyword >> name >> id;
        const EntityHandle handle = id < 0 ? NULL_ENTITY : context.resolve(static_cast<uint32_t>(id));
        if (!head || handle.isNull()) continue;
        std::istringstream payload(componentLine.substr(tab + 1));
        if (keyword == "script") {
            const auto scriptSerializer = m_scriptSerializers.find(name);
            if (scriptSerializer != m_scriptSerializers.end()) scriptSerializer->second.read(*this, handle, payload, context);
            continue;
        }
        const auto serializer = m_serializers.find(name);
        if (serializer != m_serializers.end()) serializer->second.read(*this, handle, payload, context);
    }
}

void World::writeExtras(std::ostream& out) const {
    for (const auto& [name, serializer] : m_serializers) {
        const auto poolIt = m_pools.find(serializer.type);
        if (poolIt == m_pools.end()) continue;
        serializer.owners(*poolIt->second, [&](Entity entity) {
            if (!m_alive[entity]) return;
            out << "component " << name << ' ' << entity << '\t';
            serializer.write(*poolIt->second, out, entity);
            out << '\n';
        });
    }
    std::vector<Entity> scripted;
    scripted.reserve(m_scripts.size());
    for (const auto& [index, scripts] : m_scripts) scripted.push_back(index);
    std::sort(scripted.begin(), scripted.end());
    for (Entity index : scripted) {
        if (!m_alive[index]) continue;
        for (const auto& [scriptName, payload] : serializeScripts(index)) out << "script " << scriptName << ' ' << index << '\t' << payload << '\n';
    }
}

bool World::claimSectionLine(const std::string& line, std::vector<std::pair<size_t, std::string>>& sectionLines) const {
    for (size_t section = 0; section < m_sceneSections.size(); ++section) {
        const std::string& keyword = m_sceneSections[section].keyword;
        if (line.size() > keyword.size() && line.compare(0, keyword.size(), keyword) == 0 && line[keyword.size()] == ' ') {
            sectionLines.emplace_back(section, line.substr(keyword.size() + 1));
            return true;
        }
    }
    return false;
}

void World::applySceneSections(const std::vector<std::pair<size_t, std::string>>& sectionLines) {
    for (const SceneSection& section : m_sceneSections) {
        if (section.begin) section.begin();
    }
    for (const auto& [section, text] : sectionLines) {
        if (m_sceneSections[section].read) m_sceneSections[section].read(text);
    }
    for (const SceneSection& section : m_sceneSections) {
        if (section.end) section.end();
    }
}

void World::applyCameraValues(const std::array<float, 8>& values) {
    m_camera.setPos(glm::vec3(values[0], values[1], values[2]));
    m_camera.setOrientation(values[3], values[4]);
    m_camera.setFov(values[5]);
    m_camera.setNearPlane(values[6]);
    m_camera.setFarPlane(values[7]);
}

WorldSnapshot World::captureSnapshot() const {
    WorldSnapshot snapshot;
    snapshot.transforms = m_db.transforms;
    snapshot.hierarchies = m_db.hierarchies;
    snapshot.renderables = m_db.renderables;
    snapshot.firstChild = m_db.firstChild;
    snapshot.nextSibling = m_db.nextSibling;
    snapshot.prevSibling = m_prevSibling;
    snapshot.alive = m_alive;
    snapshot.visible = m_visible;
    snapshot.generation = m_generation;
    snapshot.mesh = m_mesh;
    snapshot.layer = m_layer;
    snapshot.tags = m_tags;
    snapshot.names = m_names;
    snapshot.freeList = m_freeList;
    snapshot.firstRoot = m_firstRoot;
    snapshot.aliveCount = m_aliveCount;
    std::ostringstream extras;
    extras.precision(kSceneFloatPrecision);
    const glm::vec3 position = m_camera.getPosition();
    extras << "camera " << position.x << ' ' << position.y << ' ' << position.z << ' ' << m_camera.getYaw() << ' ' << m_camera.getPitch() << ' ' << m_camera.getFov() << ' ' << m_camera.getNearPlane() << ' ' << m_camera.getFarPlane() << '\n';
    for (const SceneSection& section : m_sceneSections) {
        if (section.write) section.write(extras);
    }
    writeExtras(extras);
    snapshot.extras = extras.str();
    snapshot.valid = true;
    return snapshot;
}

void World::restoreSnapshotArrays(const WorldSnapshot& snapshot) {
    m_db.transforms = snapshot.transforms;
    m_db.hierarchies = snapshot.hierarchies;
    m_db.renderables = snapshot.renderables;
    m_db.firstChild = snapshot.firstChild;
    m_db.nextSibling = snapshot.nextSibling;
    m_prevSibling = snapshot.prevSibling;
    m_alive = snapshot.alive;
    m_visible = snapshot.visible;
    m_generation = snapshot.generation;
    m_mesh = snapshot.mesh;
    m_layer = snapshot.layer;
    rebuildPositions();
    m_names = snapshot.names;
    m_freeList = snapshot.freeList;
    m_firstRoot = snapshot.firstRoot;
    m_aliveCount = snapshot.aliveCount;
    m_tags.clear();
    m_tags.resize(m_alive.size());
    for (Entity index = 0; index < snapshot.tags.size() && index < m_alive.size(); ++index) {
        if (m_alive[index] && !snapshot.tags.get(index).empty()) setTag({index, m_generation[index]}, snapshot.tags.get(index));
    }
}

void World::restoreSnapshotExtras(const std::string& extras) {
    std::vector<std::string> extraLines;
    std::vector<std::pair<size_t, std::string>> sectionLines;
    std::array<float, 8> cameraValues{};
    bool hasCamera = false;
    std::istringstream lines(extras);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.rfind("camera ", 0) == 0) {
            hasCamera = parseCameraValues(std::string_view(line).substr(kCameraKeywordLength), cameraValues);
            continue;
        }
        if (!claimSectionLine(line, sectionLines)) extraLines.push_back(line);
    }
    LoadContext context;
    context.resolveFn = [this](uint32_t index) { return index < m_alive.size() && m_alive[index] ? EntityHandle{index, m_generation[index]} : NULL_ENTITY; };
    applyExtraLines(extraLines, context);
    applySceneSections(sectionLines);
    if (hasCamera && m_restoreCameraOnLoad) applyCameraValues(cameraValues);
}

void World::restoreSnapshot(const WorldSnapshot& snapshot) {
    if (!snapshot.valid) return;
    std::optional<ProfileScope> profile;
    if (m_profiler) profile.emplace(*m_profiler, "World::restoreSnapshot");
    const uint32_t generationBase = m_generationBase;
    clear();
    m_generationBase = std::max(m_generationBase, generationBase);
    restoreSnapshotArrays(snapshot);
    restoreSnapshotExtras(snapshot.extras);
    m_db.markTransformsDirty();
    touchStructure();
}

std::string World::snapshot() const {
    std::optional<ProfileScope> profile;
    if (m_profiler) profile.emplace(*m_profiler, "World::saveScene");
    std::string text;
    appendScene(text);
    return text;
}

bool World::restore(const std::string& snapshotText, std::vector<EntityHandle>* remap) {
    return loadSceneText(snapshotText, false, NULL_ENTITY, true, remap).has_value();
}

void World::unloadGroup(const std::vector<EntityHandle>& roots) { destroyBatch(roots); }
