module;

#include <format>
#include <string>
#include <vector>

#include "Editor/Look/QualityPresets.hpp"

module Editor.application;

import Engine.engine;
import Editor.context;
import Editor.commands;
import Editor.palette;
import Editor.look;

void Application::registerQualityCommands() {
    std::vector<std::string> names;
    for (const quality::Preset& preset : quality::kPresets) {
        const std::string name(preset.name);
        names.push_back(name);
        addCommand("quality." + name, std::format("Quality preset: {}", name), "Render", {}, [this, name] {
            applyQualityPreset(m_engine.renderSettings(), name);
            m_ctx->toast(std::format("Quality: {}", name));
        }).checked = [this, name] { return activeQualityPreset(m_engine.renderSettings()) == name; };
    }
    addCommand("quality.preset", "Quality preset: choose by name", "Render", {}, [this, names] {
        m_palette.prompt({"Quality preset", "", "low, medium, high or ultra", names, [this](const std::string& answer) {
            if (applyQualityPreset(m_engine.renderSettings(), answer)) m_ctx->toast(std::format("Quality: {}", answer));
            else m_ctx->toast(std::format("Unknown quality preset '{}'", answer), ToastKind::Error);
        }});
    });
}
