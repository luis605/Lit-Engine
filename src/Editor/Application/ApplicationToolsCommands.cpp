module;

#include <format>
#include <string>

module Editor.application;

import Engine.engine;
import Engine.World;
import Editor.context;
import Editor.commands;
import Editor.hotreload;

void Application::updateTools(float deltaTime) {
    m_hotReload.update(*m_ctx, deltaTime);
    if (m_hotReload.takeSceneChange()) reloadSceneFromDisk();
    m_profilerPanel.update(m_engine, m_frameMs);
    m_diagnostics.update(*m_ctx, m_frameMs);
}

void Application::reloadSceneFromDisk() {
    EditorContext& ctx = *m_ctx;
    const std::string path = ctx.scenePath;
    if (ctx.playing) return;
    if (ctx.dirty()) {
        ctx.toast(std::format("{} changed on disk; unsaved edits kept", path), ToastKind::Warning);
        return;
    }
    const WorldSnapshot lastGood = ctx.world.captureSnapshot();
    if (!ctx.world.loadScene(path)) {
        ctx.world.restoreSnapshot(lastGood);
        m_hotReload.reportError(ReloadKind::Scene, path, "the scene file could not be loaded; the previous scene is still active");
        ctx.toast("Scene reload failed, kept the previous scene", ToastKind::Error);
        return;
    }
    m_engine.portals().clear();
    afterSceneReplaced();
    m_hotReload.clearError(ReloadKind::Scene, path);
    ctx.toast("Reloaded " + path, ToastKind::Success);
}

void Application::registerToolCommands() {
    m_hotReload.attach(m_engine);
    addCommand("profiler.toggle", "Profiler panel", "Profile", {}, [this] { m_profilerPanel.toggle(m_engine); }, {}, CommandScope::Global).checked = [this] { return m_profilerPanel.open(); };
    addCommand("profiler.export", "Export profile to JSON", "Profile", {}, [this] {
        const std::string path = "resources/profile.json";
        if (m_profilerPanel.exportJson(path)) m_ctx->toast("Profile written to " + path, ToastKind::Success);
        else m_ctx->toast("Could not write " + path, ToastKind::Error);
    }, [this] { return m_profilerPanel.open(); }, CommandScope::Global);
    addCommand("scene.validate", "Validate scene", "Scene", {}, [this] { m_diagnostics.validate(*m_ctx); }, {}, CommandScope::Global);
    addCommand("frame.explain", "Explain this frame", "Profile", {}, [this] { m_diagnostics.requestExplain(*m_ctx); }, {}, CommandScope::Global);
    addCommand("reload.scene", "Reload scene from disk", "File", {}, [this] { reloadSceneFromDisk(); }, {}, CommandScope::Global);
}
