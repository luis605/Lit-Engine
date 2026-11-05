module;

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
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

constexpr size_t kHeaderProbeBytes = 128;
constexpr size_t kExtraLineReserve = 64;
constexpr size_t kDenseIdSlack = 1024;
constexpr size_t kDenseIdFactor = 4;
constexpr size_t kParallelParseThreshold = 16384;
constexpr size_t kParseGrain = 8192;
constexpr int kMatrixElementCount = 16;
constexpr size_t kCameraKeywordLength = 7;
constexpr size_t kMeshKeywordLength = 5;

struct ParsedEntity {
    long long id = 0;
    long long parent = -1;
    int visible = 1;
    uint32_t mesh = 0;
    uint32_t material = 0;
    uint32_t shader = 0;
    float alpha = 1.0f;
    glm::mat4 local{1.0f};
    uint32_t layer = 1;
    uint32_t flags = 0;
    std::string_view name;
    bool ok = false;
};

bool isEntityLine(std::string_view line) { return !line.empty() && line[0] >= '0' && line[0] <= '9'; }

class HandleTable {
  public:
    HandleTable(long long maxId, size_t count) : m_dense(maxId >= 0 && static_cast<size_t>(maxId) <= count * kDenseIdFactor + kDenseIdSlack) {
        if (m_dense) {
            m_denseHandles.assign(static_cast<size_t>(maxId) + 1, NULL_ENTITY);
        } else {
            m_sparseHandles.reserve(count);
        }
    }

    [[nodiscard]] bool dense() const { return m_dense; }

    void set(long long id, EntityHandle handle) {
        if (id < 0) return;
        if (m_dense) {
            m_denseHandles[static_cast<size_t>(id)] = handle;
        } else {
            m_sparseHandles[id] = handle;
        }
    }

    [[nodiscard]] EntityHandle get(long long id) const {
        if (id < 0) return NULL_ENTITY;
        if (m_dense) return static_cast<size_t>(id) < m_denseHandles.size() ? m_denseHandles[static_cast<size_t>(id)] : NULL_ENTITY;
        const auto found = m_sparseHandles.find(id);
        return found == m_sparseHandles.end() ? NULL_ENTITY : found->second;
    }

  private:
    bool m_dense;
    std::vector<EntityHandle> m_denseHandles;
    std::unordered_map<long long, EntityHandle> m_sparseHandles;
};

void parseEntityLine(std::string_view line, bool trusted, size_t maxNameLength, ParsedEntity& parsed) {
    const size_t tab = line.find('\t');
    std::string_view fields = line.substr(0, tab);
    bool ok = readNumber(fields, parsed.id) && readNumber(fields, parsed.parent) && readNumber(fields, parsed.visible) && readNumber(fields, parsed.mesh) && readNumber(fields, parsed.material) && readNumber(fields, parsed.shader) && readNumber(fields, parsed.alpha);
    float* matrix = &parsed.local[0][0];
    for (int element = 0; ok && element < kMatrixElementCount; ++element) ok = readNumber(fields, matrix[element]) && (trusted || std::isfinite(matrix[element]));
    if (!readNumber(fields, parsed.layer)) parsed.layer = 1;
    if (!readNumber(fields, parsed.flags)) parsed.flags = 0;
    if (tab != std::string_view::npos) {
        parsed.name = line.substr(tab + 1);
        if (parsed.name.size() > maxNameLength) {
            if (!trusted) ok = false;
            parsed.name = parsed.name.substr(0, maxNameLength);
        }
    }
    if (!trusted && (parsed.id < 0 || parsed.parent < -1 || !std::isfinite(parsed.alpha))) ok = false;
    parsed.ok = ok;
}

bool hasDuplicateIds(const std::vector<ParsedEntity>& parsed, const HandleTable& handles, long long maxId) {
    if (handles.dense()) {
        std::vector<uint8_t> seen(static_cast<size_t>(maxId) + 1, 0);
        for (const ParsedEntity& entity : parsed) {
            if (seen[static_cast<size_t>(entity.id)]) return true;
            seen[static_cast<size_t>(entity.id)] = 1;
        }
        return false;
    }
    std::unordered_set<long long> seen;
    seen.reserve(parsed.size());
    for (const ParsedEntity& entity : parsed) {
        if (!seen.insert(entity.id).second) return true;
    }
    return false;
}

}

struct World::SceneLoader {
    World& world;
    bool additive;
    EntityHandle parent;
    bool trusted;
    std::string ownedContent;
    std::string_view content;
    int version = 0;
    size_t count = 0;
    std::vector<std::string_view> lineViews;
    std::vector<std::string> migratedLines;
    std::vector<uint32_t> entityLines;
    std::vector<ParsedEntity> parsed;
    long long maxId = -1;
    std::optional<HandleTable> handles;
    std::vector<std::string> componentLines;
    std::unordered_map<uint32_t, uint32_t> meshRemap;
    std::vector<std::pair<size_t, std::string>> sectionLines;
    std::array<float, 8> cameraValues{};
    bool hasCamera = false;

    SceneLoader(World& target, bool additiveLoad, EntityHandle parentHandle, bool trustedSource) : world(target), additive(additiveLoad), parent(parentHandle), trusted(trustedSource) {}

    bool parseHeader(size_t& bodyStart) {
        std::string magic;
        std::istringstream header(std::string(content.substr(0, std::min<size_t>(content.size(), kHeaderProbeBytes))));
        header >> magic >> version >> count;
        if (!header || magic != "LITSCENE" || version < 1 || version > kSceneVersion || count > kMaxSceneEntities) return false;
        const size_t firstNewline = content.find('\n');
        const size_t secondNewline = firstNewline == std::string::npos ? std::string::npos : content.find('\n', firstNewline + 1);
        if (secondNewline == std::string::npos) return false;
        bodyStart = secondNewline + 1;
        return true;
    }

    bool splitLines(size_t bodyStart) {
        lineViews.reserve(count + kExtraLineReserve);
        for (size_t position = bodyStart; position < content.size();) {
            size_t end = content.find('\n', position);
            if (end == std::string::npos) end = content.size();
            if (!trusted && end - position > kMaxSceneLineLength) return false;
            if (end > position) lineViews.emplace_back(content.data() + position, end - position);
            position = end + 1;
        }
        return true;
    }

    void migrateLines() {
        if (!world.m_sceneMigration || version >= kSceneVersion) return;
        migratedLines.reserve(lineViews.size());
        for (std::string_view view : lineViews) {
            migratedLines.emplace_back(view);
            world.m_sceneMigration(version, migratedLines.back());
        }
        for (size_t line = 0; line < lineViews.size(); ++line) lineViews[line] = migratedLines[line];
    }

    bool collectEntityLines() {
        entityLines.reserve(count);
        for (uint32_t line = 0; line < lineViews.size(); ++line) {
            if (isEntityLine(lineViews[line])) entityLines.push_back(line);
        }
        return entityLines.size() == count;
    }

    void parseEntities() {
        parsed.resize(count);
        const auto parseRange = [&](size_t begin, size_t end) {
            for (size_t entity = begin; entity < end; ++entity) parseEntityLine(lineViews[entityLines[entity]], trusted, kMaxNameLength, parsed[entity]);
        };
        if (world.m_jobs && count > kParallelParseThreshold) {
            world.m_jobs->parallelFor(count, kParseGrain, parseRange);
        } else {
            parseRange(0, count);
        }
    }

    bool checkParsed() {
        for (const ParsedEntity& entity : parsed) {
            if (!entity.ok) {
                if (trusted && !additive) world.clear();
                return false;
            }
            maxId = std::max(maxId, entity.id);
        }
        handles.emplace(maxId, count);
        return trusted || !hasDuplicateIds(parsed, *handles, maxId);
    }

    void classifyExtraLines() {
        for (std::string_view view : lineViews) {
            if (isEntityLine(view)) continue;
            const std::string line(view);
            if (line.rfind("camera ", 0) == 0) {
                hasCamera = parseCameraValues(std::string_view(line).substr(kCameraKeywordLength), cameraValues);
                continue;
            }
            if (line.rfind("meta ", 0) == 0) continue;
            if (line.rfind("mesh ", 0) == 0) {
                const size_t tab = line.find('\t');
                if (tab != std::string::npos && world.m_meshLoad) {
                    meshRemap[static_cast<uint32_t>(std::strtoul(line.c_str() + kMeshKeywordLength, nullptr, 10))] = world.m_meshLoad(line.substr(tab + 1));
                }
                continue;
            }
            if (line.rfind("tag ", 0) == 0 || line.rfind("component ", 0) == 0 || line.rfind("script ", 0) == 0) {
                componentLines.push_back(line);
                continue;
            }
            if (!additive) world.claimSectionLine(line, sectionLines);
        }
    }

    std::vector<EntityHandle> createEntities() {
        std::vector<EntityHandle> created;
        created.reserve(count);
        for (const ParsedEntity& entity : parsed) {
            EntityDesc desc;
            desc.mesh = entity.mesh;
            desc.material = entity.material;
            desc.shader = entity.shader;
            desc.alpha = entity.alpha;
            if (const auto remapped = meshRemap.find(desc.mesh); remapped != meshRemap.end()) desc.mesh = remapped->second;
            const EntityHandle handle = world.createImpl(desc);
            world.m_names.set(handle.index, entity.name);
            world.m_db.transforms[handle.index].localMatrix = entity.local;
            world.syncPosition(handle.index);
            world.m_layer[handle.index] = entity.layer;
            world.m_db.renderables[handle.index].flags |= entity.flags;
            if (!entity.visible) {
                world.m_visible[handle.index] = 0;
                world.m_db.renderables[handle.index].flags |= RENDER_HIDDEN;
            }
            world.queueSpatial(handle.index);
            world.m_events.emit(EntityCreated{handle});
            handles->set(entity.id, handle);
            created.push_back(handle);
        }
        return created;
    }

    std::vector<EntityHandle> linkEntities(const std::vector<EntityHandle>& created) {
        std::vector<EntityHandle> roots;
        for (size_t entity = 0; entity < parsed.size(); ++entity) {
            const EntityHandle self = created[entity];
            const EntityHandle parentHandle = handles->get(parsed[entity].parent);
            if (!parentHandle.isNull() && parentHandle != self) {
                world.setParent(self, parentHandle, false);
                continue;
            }
            roots.push_back(self);
            if (additive && world.valid(parent)) world.setParent(self, parent, false);
        }
        return roots;
    }

    void applyExtras() {
        LoadContext context;
        context.resolveFn = [this](uint32_t savedIndex) { return handles->get(static_cast<long long>(savedIndex)); };
        world.applyExtraLines(componentLines, context);
        if (!additive) world.applySceneSections(sectionLines);
        if (hasCamera && world.m_restoreCameraOnLoad && !additive) world.applyCameraValues(cameraValues);
    }

    bool parseContent() {
        size_t bodyStart = 0;
        if (!parseHeader(bodyStart) || !splitLines(bodyStart)) return false;
        migrateLines();
        if (!collectEntityLines()) return false;
        parseEntities();
        return checkParsed();
    }

    std::optional<std::vector<EntityHandle>> run(std::string_view text, std::vector<EntityHandle>* remap) {
        content = text;
        if (!parseContent()) return std::nullopt;
        if (!additive) world.clear();
        world.reserve(world.m_alive.size() + count);
        classifyExtraLines();
        const std::vector<EntityHandle> created = createEntities();
        std::vector<EntityHandle> roots = linkEntities(created);
        applyExtras();
        if (remap) {
            for (EntityHandle& handle : *remap) handle = handles->get(static_cast<long long>(handle.index));
        }
        world.touchStructure();
        return roots;
    }
};

std::optional<std::vector<EntityHandle>> World::loadSceneImpl(std::istream& in, bool additive, EntityHandle parent, bool trusted, std::vector<EntityHandle>* remap) {
    std::optional<ProfileScope> profile;
    if (m_profiler) profile.emplace(*m_profiler, "World::loadScene");
    if (!in) return std::nullopt;
    SceneLoader loader(*this, additive, parent, trusted);
    loader.ownedContent.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return loader.run(loader.ownedContent, remap);
}

std::optional<std::vector<EntityHandle>> World::loadSceneText(std::string_view text, bool additive, EntityHandle parent, bool trusted, std::vector<EntityHandle>* remap) {
    std::optional<ProfileScope> profile;
    if (m_profiler) profile.emplace(*m_profiler, "World::loadScene");
    SceneLoader loader(*this, additive, parent, trusted);
    return loader.run(text, remap);
}

bool World::loadScene(const std::filesystem::path& path) {
    std::ifstream in(path);
    return loadSceneImpl(in, false, NULL_ENTITY).has_value();
}

bool World::loadScene(std::istream& in) { return loadSceneImpl(in, false, NULL_ENTITY).has_value(); }

std::optional<std::vector<EntityHandle>> World::loadSceneAdditive(const std::filesystem::path& path, EntityHandle parent) {
    std::ifstream in(path);
    return loadSceneImpl(in, true, parent);
}
