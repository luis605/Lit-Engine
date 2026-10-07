module;

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

export module Engine.Save;

import Engine.Json;
import Engine.Observation;
import Engine.Params;
import Engine.State;
import Engine.Render.entity;
import Engine.Storylet;
import Engine.World;

export inline constexpr int kSaveVersion = 1;

export struct SaveContext {
    World* world = nullptr;
    StateStore* state = nullptr;
    StoryletEngine* storylets = nullptr;
    ParamBus* params = nullptr;
    ObservationSystem* observation = nullptr;
};

export class SaveSystem {
  public:
    using SectionSave = std::function<Json()>;
    using SectionLoad = std::function<bool(const Json&, std::string*)>;
    using Migration = std::function<bool(Json& body, std::string* error)>;

    explicit SaveSystem(SaveContext context) : m_ctx(context) {}

    void addSection(std::string name, SectionSave save, SectionLoad load) { m_sections[std::move(name)] = {std::move(save), std::move(load)}; }
    void addMigration(int fromVersion, Migration migration) { m_migrations[fromVersion] = std::move(migration); }

    [[nodiscard]] Json capture(const std::string& label = {}) const;
    bool apply(const Json& body, std::string* error = nullptr);

    bool save(const std::string& path, const std::string& label = {}, std::string* error = nullptr);
    bool load(const std::string& path, std::string* error = nullptr);

    void configureAutosave(std::string directory, float intervalSeconds, uint32_t slots = 3);
    bool autosave(float dt, std::string* error = nullptr);
    bool autosaveNow(std::string* error = nullptr);
    bool loadLatestAutosave(std::string* error = nullptr);
    [[nodiscard]] std::string autosavePath(uint32_t slot) const;
    [[nodiscard]] uint64_t serial() const { return m_serial; }
    [[nodiscard]] EntityHandle remap(EntityHandle saved) const { return saved.index < m_remap.size() ? m_remap[saved.index] : NULL_ENTITY; }

  private:
    struct Section {
        SectionSave save;
        SectionLoad load;
    };

    SaveContext m_ctx;
    std::map<std::string, Section> m_sections;
    std::map<int, Migration> m_migrations;
    uint64_t m_serial = 0;
    std::vector<EntityHandle> m_remap;
    std::string m_autoDir;
    float m_autoInterval = 0.0f;
    float m_autoElapsed = 0.0f;
    uint32_t m_autoSlots = 3;
};
