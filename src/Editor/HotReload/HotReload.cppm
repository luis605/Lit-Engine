module;

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

export module Editor.hotreload;

import Editor.context;
import Engine.engine;

export enum class ReloadKind { Shader, Scene, Data };

export struct ReloadError {
    ReloadKind kind = ReloadKind::Data;
    std::string source;
    std::string message;
    long long stamp = 0;
};

export class HotReload {
  public:
    void attach(Engine& engine);
    void update(EditorContext& ctx, float deltaTime);
    void draw(EditorContext& ctx);
    void noteSaved(const std::string& path);
    bool takeSceneChange();
    void reportError(ReloadKind kind, const std::string& source, std::string message);
    void clearError(ReloadKind kind, const std::string& source);
    [[nodiscard]] const std::vector<ReloadError>& errors() const { return m_errors; }

  private:
    void watchScene(const std::string& path);
    void checkDataFiles(EditorContext& ctx);
    void expireEditedErrors();

    Engine* m_engine = nullptr;
    std::vector<ReloadError> m_errors;
    std::unordered_set<std::string> m_watchedScenes;
    std::unordered_map<std::string, long long> m_ownStamps;
    std::unordered_map<std::string, long long> m_dataStamps;
    bool m_sceneChanged = false;
    float m_pollTimer = 0.0f;
};

export long long fileStamp(const std::string& path);
