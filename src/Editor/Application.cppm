module;

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

export module Editor.application;

import Engine.engine;
import Engine.History;
import Engine.Render.entity;
import Engine.glm;
import Engine.Render.settings;
import Editor.context;
import Editor.commands;
import Editor.viewport;
import Editor.gizmo;
import Editor.hierarchy;
import Editor.inspector;
import Editor.palette;
import Editor.hud;
import Editor.look;
import Editor.autopilot;

export class Application {
  public:
    Application();
    ~Application();
    void update();
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] int exitCode() const { return m_autopilot.failures() > 0 ? 1 : 0; }

  private:
    void buildStressScene();
    void buildShowcaseScene();
    void registerCommands();
    void setPlaying(bool playing);
    void saveScene(const std::string& path);
    void loadScene(const std::string& path);
    void afterSceneReplaced();
    void duplicateSelection(bool grab);
    void deleteSelection();
    void copySelection();
    void paste();
    EntityHandle createPrimitive(const std::string& kind);
    void groupSelection();
    void parentToActive();
    void walk(int direction);
    void frameSelection();
    void frameAll();
    void setView(const glm::vec3& direction);
    void storeSelectionSet(int slot);
    void recallSelectionSet(int slot);
    void storeBookmark(int slot);
    void recallBookmark(int slot);
    void selectSimilar(bool byMaterial);
    void drawQuickAdd();
    void drawRecovery();
    void drawUnsavedPrompt();
    void guarded(std::function<void()> action);
    void trackRenderSettings();
    void drawStats(float frameMs);
    void updateTitle();
    std::vector<std::string> sceneFiles() const;

    Engine m_engine;
    GLFWwindow* m_window = nullptr;
    std::unique_ptr<EditorContext> m_ctx;
    std::unique_ptr<History> m_editHistory;
    CommandRegistry m_commands;
    Viewport m_viewport;
    TransformTool m_tool;
    Outliner m_outliner;
    Inspector m_inspector;
    Palette m_palette;
    Hud m_hud;
    LookPanel m_look;
    Autopilot m_autopilot;
    LookState m_lookState;
    bool m_recoveryPending = false;
    float m_autosaveTimer = 0.0f;
    float m_autosaveInterval = 60.0f;
    double m_lastFrame = 0.0;
    float m_frameMs = 16.0f;
    uint64_t m_framesPresented = 0;
    size_t m_reportedOverCapacity = 0;
    double m_lastInputTime = 0.0;
    std::vector<EntityHandle> m_playSelection;
    std::function<void()> m_pendingAction;
    bool m_unsavedPromptOpen = false;
    bool m_discardConfirmed = false;
    RenderSettings m_stableSettings;
    std::vector<glm::vec3> m_basePositions;
    uint32_t m_movingObjectCount = 0;
    EntityHandle m_lastPrimary;
    std::string m_lastTitle;
    bool m_stepOnce = false;
};
