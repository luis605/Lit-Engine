module;

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

module Editor.hotreload;

import Editor.context;
import Engine.engine;
import Engine.Json;

namespace {

constexpr float kPollSeconds = 0.25f;
constexpr float kOverlayWidth = 520.0f;
constexpr float kOverlayTop = 40.0f;
constexpr size_t kMessageLineLimit = 6;
constexpr size_t kMaxErrors = 8;

std::string firstLines(const std::string& text, size_t lines) {
    size_t cut = 0;
    for (size_t line = 0; line < lines; ++line) {
        cut = text.find('\n', cut);
        if (cut == std::string::npos) return text;
        ++cut;
    }
    return text.substr(0, cut);
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}

long long fileStamp(const std::string& path) {
    std::error_code error;
    const long long stamp = std::filesystem::last_write_time(path, error).time_since_epoch().count();
    return error ? 0 : stamp;
}

void HotReload::attach(Engine& engine) { m_engine = &engine; }

void HotReload::noteSaved(const std::string& path) { m_ownStamps[path] = fileStamp(path); }

bool HotReload::takeSceneChange() {
    const bool changed = m_sceneChanged;
    m_sceneChanged = false;
    return changed;
}

void HotReload::reportError(ReloadKind kind, const std::string& source, std::string message) {
    clearError(kind, source);
    if (m_errors.size() >= kMaxErrors) m_errors.erase(m_errors.begin());
    m_errors.push_back({kind, source, std::move(message), fileStamp(source)});
}

void HotReload::clearError(ReloadKind kind, const std::string& source) {
    std::erase_if(m_errors, [&](const ReloadError& error) { return error.kind == kind && error.source == source; });
}

void HotReload::watchScene(const std::string& path) {
    if (!m_watchedScenes.insert(path).second) return;
    m_engine->watchData(path, [this, path](const std::string&) {
        const auto own = m_ownStamps.find(path);
        if (own != m_ownStamps.end() && own->second == fileStamp(path)) return false;
        m_sceneChanged = true;
        return false;
    });
}

void HotReload::checkDataFiles(EditorContext& ctx) {
    for (const std::string& path : m_engine->watchedFiles()) {
        if (!path.ends_with(".json")) continue;
        const long long stamp = fileStamp(path);
        const auto [slot, fresh] = m_dataStamps.emplace(path, stamp);
        if (fresh || slot->second == stamp) continue;
        slot->second = stamp;
        std::string problem;
        if (Json::parse(readFile(path), &problem)) {
            clearError(ReloadKind::Data, path);
            ctx.toast("Reloaded " + std::filesystem::path(path).filename().string(), ToastKind::Success);
        } else {
            reportError(ReloadKind::Data, path, "invalid JSON, keeping the last good values\n" + problem);
            ctx.toast("Invalid JSON in " + std::filesystem::path(path).filename().string(), ToastKind::Error);
        }
    }
}

void HotReload::expireEditedErrors() {
    std::erase_if(m_errors, [](const ReloadError& error) { return error.kind == ReloadKind::Shader && fileStamp(error.source) != error.stamp; });
}

void HotReload::update(EditorContext& ctx, float deltaTime) {
    if (!m_engine) return;
    if (!ctx.scenePath.empty()) watchScene(ctx.scenePath);
    m_pollTimer += deltaTime;
    if (m_pollTimer < kPollSeconds) return;
    m_pollTimer = 0.0f;
    checkDataFiles(ctx);
    expireEditedErrors();
}

void HotReload::draw(EditorContext&) {
    if (m_errors.empty()) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->GetCenter().x, viewport->Pos.y + kOverlayTop), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(kOverlayWidth, 0.0f), ImVec2(kOverlayWidth, viewport->Size.y * 0.6f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(48, 14, 14, 235));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 90, 90, 255));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("Reload errors###ReloadErrors", nullptr, flags)) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%zu reload error(s), last good state kept", m_errors.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss all")) m_errors.clear();
        for (size_t index = 0; index < m_errors.size(); ++index) {
            const ReloadError& error = m_errors[index];
            ImGui::Separator();
            ImGui::PushID(static_cast<int>(index));
            ImGui::TextUnformatted(std::filesystem::path(error.source).filename().string().c_str());
            ImGui::TextWrapped("%s", firstLines(error.message, kMessageLineLimit).c_str());
            ImGui::PopID();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}
