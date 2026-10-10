module;

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

export module Engine.Delta;

import Engine.glm;
import Engine.Json;
import Engine.Portal;
import Engine.Render.entity;
import Engine.World;

export struct EntityState {
    std::string name;
    std::string tag;
    uint32_t mesh = 0;
    uint32_t material = 0;
    uint32_t shader = 0;
    uint32_t layer = 1;
    uint32_t renderFlags = 0;
    float alpha = 1.0f;
    bool visible = true;
    glm::mat4 local{1.0f};
    std::vector<std::pair<std::string, std::string>> components;
    bool operator==(const EntityState&) const = default;
};

export class DeltaStore {
  public:
    explicit DeltaStore(World& world, PortalSystem* portals = nullptr) : m_world(world), m_portals(portals) {}

    void registerBase(const std::string& cell, std::string key, EntityHandle entity);
    EntityHandle trackAdded(const std::string& cell, EntityHandle root);
    [[nodiscard]] EntityHandle find(const std::string& cell, const std::string& key) const;
    [[nodiscard]] std::vector<EntityHandle> liveEntities(const std::string& cell) const;

    void snapshotParams();
    void captureCell(const std::string& cell);
    void captureAll();
    void applyCell(const std::string& cell);
    void releaseCell(const std::string& cell);
    void clear();

    [[nodiscard]] size_t deltaCount() const;
    [[nodiscard]] Json toJson();
    bool fromJson(const Json& doc, std::string* error = nullptr);

  private:
    struct Base {
        std::string key;
        EntityHandle handle;
        EntityState baseline;
    };
    struct Added {
        std::string key;
        EntityHandle handle;
    };
    struct Live {
        std::vector<Base> bases;
        std::vector<Added> added;
        bool resident = false;
    };
    struct Stored {
        std::map<std::string, EntityState> changed;
        std::set<std::string> removed;
        std::map<std::string, Prefab> added;
        bool hasParams = false;
        Json paramsJson;
        [[nodiscard]] bool empty() const { return changed.empty() && removed.empty() && added.empty() && !hasParams; }
    };

    [[nodiscard]] EntityState read(EntityHandle e) const;
    void write(EntityHandle e, const EntityState& s);
    void captureParams(const std::string& cell);
    void applyParams(const std::string& cell);

    World& m_world;
    PortalSystem* m_portals;
    std::map<std::string, Live> m_live;
    std::map<std::string, Stored> m_stored;
    std::map<std::string, PortalCellParams> m_baseParams;
    uint64_t m_addedSerial = 0;
};
