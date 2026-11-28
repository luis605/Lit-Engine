module;

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

export module Engine.OrbRun;

import Engine.engine;
import Engine.glm;
import Engine.World;
import Engine.Portal;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.OrbRunRules;

export inline constexpr const char* kOrbRunHub = "Hub";
export inline constexpr const char* kOrbRunEmber = "Ember";
export inline constexpr const char* kOrbRunFrost = "Frost";
export inline constexpr const char* kOrbRunGrotto = "Grotto";
export inline constexpr const char* kOrbRunCourtyard = "Courtyard";

export inline constexpr const char* kOrbRunDropPortal = "drop";
export inline constexpr const char* kOrbRunCeilingPortal = "ceiling";
export inline constexpr const char* kOrbRunRolledPortal = "rolled";
export inline constexpr const char* kOrbRunCourtyardPortal = "courtyard";
export inline constexpr const char* kOrbRunGrottoPortal = "grotto";

export struct OrbRunKit {
    PortalSceneKit portal;
    std::function<uint32_t(const Material&)> material;
};

export struct OrbRunSpawn {
    glm::vec3 feet{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
};

export struct OrbRunPatrol {
    glm::vec3 home{0.0f};
    std::vector<glm::vec3> waypoints;
};

export struct OrbRunLayout {
    OrbRunSpawn spawn;
    std::vector<glm::vec3> orbs;
    std::vector<OrbRunPatrol> spiders;
    std::vector<glm::vec3> crates;
    glm::vec3 courtyardCenter{0.0f};
    float plazaHalf = 0.0f;
};

export struct OrbRunSpider {
    EntityHandle body;
    OrbRunPatrol route;
    size_t waypoint = 0;
    bool frozen = false;
    bool chasing = false;
};

export struct OrbRunCrate {
    EntityHandle entity;
    glm::vec3 home{0.0f};
};

export struct OrbRunOrb {
    EntityHandle entity;
    glm::vec3 home{0.0f};
    glm::vec3 color{1.0f};
    uint32_t material = 0;
};

export struct OrbRunGame {
    OrbRunConfig config;
    OrbRunState state;
    OrbRunLayout layout;
    std::vector<OrbRunOrb> orbs;
    std::vector<EntityHandle> sockets;
    std::vector<OrbRunSpider> spiders;
    std::vector<OrbRunCrate> crates;
    uint32_t socketLit = 0;
    uint32_t socketDim = 0;
    uint32_t orbMesh = 0;
    uint32_t orbShader = 0;
    uint32_t respawns = 0;
    uint32_t spiderHits = 0;
    bool playable = false;
};

export [[nodiscard]] OrbRunLayout orbRunLayout();
export [[nodiscard]] float orbRunTerrainHeight(float x, float z);
export void buildOrbRun(Engine& engine, const OrbRunKit& kit);
export void enableOrbRunPlay(Engine& engine);
export void restartOrbRun(Engine& engine);
export void respawnOrbRunPlayer(Engine& engine);
export void releaseOrbRun(Engine& engine);
export [[nodiscard]] OrbRunGame* orbRunGame(Engine& engine);
