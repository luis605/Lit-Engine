module;

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Engine/Log/Log.hpp"

export module Editor.context;

import Engine.engine;
import Engine.World;
import Engine.History;
import Engine.Selection;
import Engine.Render.entity;
import Engine.camera;
import Engine.glm;

export enum class ToastKind { Info, Success, Warning, Error };

export struct Toast {
    std::string text;
    ToastKind kind = ToastKind::Info;
    float age = 0.0f;
};

export struct CameraBookmark {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool valid = false;
};

export enum class HandleMode { Translate, Rotate, Scale, Off };

export struct CameraFlight {
    bool active = false;
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f};
    float fromYaw = 0.0f, toYaw = 0.0f, fromPitch = 0.0f, toPitch = 0.0f;
    float t = 0.0f;
    float duration = 0.25f;
};

export struct EditorContext {
    EditorContext(Engine& engine) : engine(engine), world(engine.world()), history(std::make_unique<History>(engine.world())) {}

    Engine& engine;
    World& world;
    std::unique_ptr<History> history;
    Selection selection;

    HandleMode handles = HandleMode::Translate;
    bool localSpace = false;
    bool snapping = false;
    float snapMove = 0.5f;
    float snapAngle = 15.0f;
    float snapScale = 0.1f;

    bool playing = false;
    bool paused = false;
    WorldSnapshot playSnapshot;

    bool showOutliner = true;
    bool showInspector = true;
    bool pinInspector = false;
    bool showLook = false;
    bool showStats = true;
    bool showHelp = false;
    bool showTimeline = false;
    bool zen = false;
    bool showGrid = true;
    bool openPalette = false;
    std::string paletteSeed;
    bool openQuickAdd = false;
    bool requestQuit = false;

    std::array<std::vector<EntityHandle>, 10> selectionSets{};
    std::array<CameraBookmark, 10> bookmarks{};
    std::vector<Prefab> clipboard;

    std::string scenePath = "resources/scene.litscene";
    uint64_t savedRevision = 0;

    std::vector<Toast> toasts;

    EntityHandle hovered;
    glm::vec3 cursor{0.0f};
    bool cursorOnSurface = false;
    glm::vec2 mouse{0.0f};
    bool viewportHovered = true;
    bool outlinerFocused = false;
    bool modalActive = false;
    bool flying = false;
    float flySpeed = 8.0f;
    CameraFlight flight;

    std::string modeLabel = "EDIT";
    std::vector<std::pair<std::string, std::string>> hints;

    uint32_t cubeMesh = 0;
    uint32_t sphereMesh = 0;
    EntityHandle stressParent;

    void toast(std::string text, ToastKind kind = ToastKind::Info) {
        if (toasts.size() >= 6) toasts.erase(toasts.begin());
        if (kind == ToastKind::Error) {
            Lit::Log::Error("[toast] {}", text);
        } else {
            Lit::Log::Info("[toast] {}", text);
        }
        toasts.push_back({std::move(text), kind, 0.0f});
    }

    void hint(std::string keys, std::string action) { hints.emplace_back(std::move(keys), std::move(action)); }

    [[nodiscard]] bool dirty() const { return history->state() != savedRevision; }

    [[nodiscard]] EntityHandle primary() const { return selection.primary(); }

    void select(EntityHandle e) {
        if (e.isNull()) {
            selection.clear();
        } else {
            selection.set(e);
        }
    }

    [[nodiscard]] std::vector<EntityHandle> topLevelSelection() const {
        std::vector<EntityHandle> out;
        for (EntityHandle e : selection.items()) {
            if (!world.isAlive(e)) continue;
            bool nested = false;
            for (EntityHandle p = world.getParent(e); !p.isNull(); p = world.getParent(p)) {
                if (selection.contains(p)) {
                    nested = true;
                    break;
                }
            }
            if (!nested) out.push_back(e);
        }
        return out;
    }
};

export std::string displayName(World& world, EntityHandle e);
export std::string meshLabel(Engine& engine, uint32_t mesh);
export void applyEditorTheme(float scale);
export void flyTo(EditorContext& ctx, const glm::vec4& sphere);
export void updateFlight(EditorContext& ctx, float dt);
export glm::vec3 cameraForward(const Camera& camera);
export std::optional<double> evaluateExpression(const std::string& text);
export float fuzzyScore(const std::string& pattern, const std::string& text);
