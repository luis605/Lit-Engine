module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Engine.IK;

export import Engine.IK.Solvers;
import Engine.glm;
import Engine.Json;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.World;
import Engine.History;

export using WalkerGroundFn = std::function<bool(const glm::vec3&, float&, glm::vec3&)>;

export enum class IkAlgorithm : uint8_t { Auto, TwoBone, Fabrik, Ccd };

export struct IkChain {
    std::vector<EntityHandle> joints;
    std::vector<IkJointLimit> limits;
    EntityHandle target = NULL_ENTITY;
    glm::vec3 targetPoint{0.0f};
    bool useTargetPoint = false;
    EntityHandle pole = NULL_ENTITY;
    glm::vec3 polePoint{0.0f};
    bool usePolePoint = false;
    float weight = 1.0f;
    int iterations = 12;
    float tolerance = 1.0e-3f;
    IkAlgorithm algorithm = IkAlgorithm::Auto;
    bool enabled = true;

    std::vector<float> lengths;
    bool bound = false;
    bool solved = false;
    glm::vec3 lastTarget{0.0f};
    glm::vec3 lastPole{0.0f};
    glm::vec3 lastRoot{0.0f};
    glm::vec3 lastTip{0.0f};
    float lastWeight = 0.0f;
    float lastError = 0.0f;
    bool lastReached = false;
};

export struct WalkerLeg {
    EntityHandle chain = NULL_ENTITY;
    EntityHandle foot = NULL_ENTITY;
    glm::vec3 localRest{0.0f};
    glm::vec3 hipLocal{0.0f};
    glm::vec3 planted{0.0f};
    glm::vec3 swingFrom{0.0f};
    glm::vec3 swingTo{0.0f};
    glm::vec3 position{0.0f};
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
    float swingTime = 0.0f;
    float swingDuration = 0.25f;
    float reach = 1.0f;
    int group = 0;
    int sameSideNext = -1;
    int sameSidePrevious = -1;
    int mirror = -1;
    bool swinging = false;
    bool placed = false;
};

export struct Walker {
    EntityHandle body = NULL_ENTITY;
    std::vector<WalkerLeg> legs;
    float stride = 0.3f;
    float stepHeight = 0.25f;
    float bodyHeight = 0.0f;
    float maxSpeed = 1.5f;
    float acceleration = 6.0f;
    float turnRate = 5.0f;
    float arrivalRadius = 0.1f;
    float slowRadius = 1.0f;
    float swingSpeed = 3.5f;
    float minSwingTime = 0.08f;
    float maxSwingTime = 0.5f;
    float heightSmoothing = 12.0f;
    float tiltSmoothing = 8.0f;
    float maxTiltDegrees = 35.0f;
    float probeUp = 1.5f;
    float probeDown = 4.0f;
    int maxSimultaneous = 0;
    bool enabled = true;
    bool hasMoveTarget = false;
    glm::vec3 moveTarget{0.0f};
    glm::vec2 moveInput{0.0f};
    glm::vec3 inputRight{1.0f, 0.0f, 0.0f};
    glm::vec3 inputForward{0.0f, 0.0f, -1.0f};
    WalkerGroundFn ground;

    glm::vec3 velocity{0.0f};
    glm::vec3 heading{0.0f, 0.0f, -1.0f};
    glm::vec3 smoothedUp{0.0f, 1.0f, 0.0f};
    float speed = 0.0f;
    float speedScale = 1.0f;
    int activeGroup = 0;
    bool bound = false;
    bool arrived = false;
    bool settled = false;
};

export struct IkFrameStats {
    uint32_t solved = 0;
    uint32_t skipped = 0;
};

export struct SpiderOptions {
    int legs = 8;
    int segments = 2;
    float legLength = 1.2f;
    float bodyRadius = 0.35f;
    float bodyLength = 0.8f;
    float thickness = 0.06f;
    float stepHeight = 0.0f;
    float stride = 0.0f;
    float maxSpeed = 1.5f;
    float heightRatio = 0.4f;
    float restRatio = 0.7f;
    float splay = 0.6f;
    uint32_t bodyMesh = NO_MESH;
    uint32_t boneMesh = NO_MESH;
    uint32_t material = 0;
    std::string name = "spider";
};

export struct LegPattern {
    std::string prefix = "leg";
    bool ignoreCase = true;
};

export inline constexpr size_t kMaxWalkerLegs = 16;

export struct GroundHit {
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    bool found = false;
};

export using IkLineSink = std::function<void(const glm::vec3&, const glm::vec3&, const glm::vec4&)>;

export IkFrameStats updateIk(World& world, float deltaTime);
export void updateWalkers(World& world, float deltaTime);
export void registerIkComponents(World& world);
export bool bindIkChain(World& world, IkChain& chain);
export bool bindWalker(World& world, Walker& walker);

export EntityHandle createIkChain(World& world, std::span<const EntityHandle> joints, EntityHandle target = NULL_ENTITY);
export EntityHandle spawnIkChain(History& history, std::span<const EntityHandle> joints, const std::string& targetName, const glm::vec3& targetPosition);
export EntityHandle makeSpider(World& world, const glm::vec3& position, const SpiderOptions& options = {});
export EntityHandle makeWalkerFromRecipe(World& world, const glm::vec3& position, std::string_view recipe, uint32_t bodyMesh = NO_MESH, uint32_t boneMesh = NO_MESH);
export std::vector<std::vector<EntityHandle>> detectLegs(World& world, EntityHandle body, const LegPattern& pattern = {});
export bool makeWalkerFromHierarchy(World& world, EntityHandle body, const LegPattern& pattern = {});
export std::vector<EntityHandle> collectRigEntities(World& world, EntityHandle root);
export void setWalkerTarget(World& world, EntityHandle body, const glm::vec3& target);
export void setWalkerInput(World& world, EntityHandle body, const glm::vec2& input);

export std::optional<EntityHandle> loadRigJson(World& world, const Json& rig, const glm::vec3& position, std::string* error = nullptr);
export Json rigToJson(World& world, EntityHandle root);

export GroundHit sampleWalkerGround(World& world, const Walker& walker, float x, float z, float referenceHeight);
export void assignWalkerGait(Walker& walker);
export void updateWalkerBody(World& world, Walker& walker, float deltaTime);
export void updateWalkerLegs(World& world, Walker& walker, float deltaTime);
export void drawIkDebug(World& world, const IkLineSink& line);
