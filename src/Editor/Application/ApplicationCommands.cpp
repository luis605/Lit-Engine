module;

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "Engine/Log/Log.hpp"
#include "Editor/Application/ApplicationInternal.hpp"

module Editor.application;

import Engine.engine;
import Engine.mesh;
import Engine.World;
import Engine.Portal;
import Engine.History;
import Engine.Physics;
import Engine.Animation;
import Engine.Render.component;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.Render.settings;
import Engine.Render.custom;
import Engine.camera;
import Engine.Overlay;
import Engine.Observation;
import Engine.TestScenes;
import Engine.Params;
import Engine.input;
import Engine.GizmoMath;
import Engine.glm;
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

void Application::registerCommands() {
    registerFileCommands();
    registerEditCommands();
    registerMaterialCommands();
    registerSelectCommands();
    registerSelectionSetCommands();
    registerTransformCommands();
    registerNudgeCommands();
    registerViewCommands();
    registerPanelCommands();
    registerCreateCommands();
    registerVisibilityCommands();
    registerPlayCommands();
    registerSceneCommands();
    registerRenderCommands();
    registerDebugViewCommands();
    registerLightingCommands();
    registerRenderToggleCommands();
    registerPassCommands();
    registerShaderCommands();
    registerProfileCommands();
    registerIkCommands();
    registerQualityCommands();
    registerProbeOverlay();
    registerToolCommands();
}

Command& Application::addCommand(std::string id, std::string title, std::string category, std::vector<Chord> chords, std::function<void()> run, std::function<bool()> enabled, CommandScope scope) {
    Command command;
    command.id = std::move(id);
    command.title = std::move(title);
    command.category = std::move(category);
    command.chords = std::move(chords);
    command.run = std::move(run);
    command.enabled = std::move(enabled);
    command.scope = scope;
    return m_commands.add(std::move(command));
}

void Application::createEmptyScene() {
    if (m_ctx->playing) setPlaying(false);
    m_ctx->world.clear();
    m_engine.setAnimation(0.0f, 0, 1);
    m_movingObjectCount = 0;
    afterSceneReplaced();
    m_ctx->scenePath = "resources/untitled.litscene";
    m_ctx->toast("New scene", ToastKind::Success);
}

void Application::registerFileCommands() {
    addCommand("file.save", "Save scene", "File", {ctrl(ImGuiKey_S)}, [this] { saveScene(m_ctx->scenePath); }, {}, CommandScope::Global);
    addCommand("file.saveAs", "Save scene as...", "File", {ctrlShift(ImGuiKey_S)}, [this] {
        m_palette.prompt({"Save scene as", m_ctx->scenePath, "resources/level.litscene", sceneFiles(), [this](const std::string& path) { saveScene(path); }});
    }, {}, CommandScope::Global);
    addCommand("file.open", "Open scene...", "File", {ctrl(ImGuiKey_O)}, [this] {
        m_palette.prompt({"Open scene", "", "type or pick a .litscene", sceneFiles(), [this](const std::string& path) { guarded([this, path] { loadScene(path); }); }});
    }, {}, CommandScope::Global);
    addCommand("file.new", "New empty scene", "File", {ctrl(ImGuiKey_N)}, [this] {
        const std::string question = std::format("Clear all {} entities? This cannot be undone. Type yes", m_ctx->world.aliveCount());
        m_palette.prompt({question, "", "yes", {}, [this](const std::string& answer) {
                              if (answer == "yes") createEmptyScene();
                          }});
    }, {}, CommandScope::Global);
    addCommand("file.quit", "Quit", "File", {ctrl(ImGuiKey_Q)}, [this] { guarded([this] { m_ctx->requestQuit = true; }); }, {}, CommandScope::Global);
}

void Application::registerEditCommands() {
    addCommand("edit.undo", "Undo", "Edit", {ctrl(ImGuiKey_Z)}, [this] {
        if (!m_ctx->history->undo()) m_ctx->toast("Nothing to undo");
        m_ctx->selection.prune(m_ctx->world);
    }, {}, CommandScope::Global).repeat = true;
    addCommand("edit.redo", "Redo", "Edit", {ctrlShift(ImGuiKey_Z), ctrl(ImGuiKey_Y)}, [this] {
        if (!m_ctx->history->redo()) m_ctx->toast("Nothing to redo");
        m_ctx->selection.prune(m_ctx->world);
    }, {}, CommandScope::Global).repeat = true;
    addCommand("edit.duplicate", "Duplicate and move", "Edit", {shift(ImGuiKey_D)}, [this] { duplicateSelection(true); }, selectionRequired());
    addCommand("edit.duplicateInPlace", "Duplicate in place", "Edit", {ctrl(ImGuiKey_D)}, [this] { duplicateSelection(false); }, selectionRequired());
    addCommand("edit.delete", "Delete", "Edit", {key(ImGuiKey_Delete)}, [this] { deleteSelection(); }, selectionRequired());
    addCommand("edit.copy", "Copy", "Edit", {ctrl(ImGuiKey_C)}, [this] { copySelection(); }, selectionRequired());
    addCommand("edit.cut", "Cut", "Edit", {ctrl(ImGuiKey_X)}, [this] {
        copySelection();
        deleteSelection();
    }, selectionRequired());
    addCommand("edit.paste", "Paste at cursor", "Edit", {ctrl(ImGuiKey_V)}, [this] { paste(); }, [this] { return !m_ctx->clipboard.empty(); });
    addCommand("edit.rename", "Rename", "Edit", {key(ImGuiKey_F2)}, [this] {
        m_ctx->showInspector = true;
        m_inspector.focusName();
    }, selectionRequired());
    addCommand("edit.group", "Group under new parent", "Edit", {ctrl(ImGuiKey_G)}, [this] { groupSelection(); }, selectionRequired());
    addCommand("edit.parent", "Parent selection to active", "Edit", {key(ImGuiKey_P)}, [this] { parentToActive(); }, selectionRequired());
    addCommand("edit.unparent", "Clear parent", "Edit", {alt(ImGuiKey_P)}, [this] {
        m_ctx->history->beginGroup("Unparent");
        for (EntityHandle entity : m_ctx->topLevelSelection()) m_ctx->history->setParent(entity, NULL_ENTITY);
        m_ctx->history->endGroup();
    }, selectionRequired());
}

void Application::registerMaterialCommands() {
    addCommand("material.assign", "Assign material...", "Material", {key(ImGuiKey_M)}, [this] {
        std::vector<std::string> names;
        names.reserve(m_engine.materialCount());
        for (uint32_t id = 0; id < m_engine.materialCount(); ++id) names.push_back(std::format("{}  #{}", m_engine.material(id).name, id));
        m_palette.prompt({"Assign material", "", "material name or #id", names, [this](const std::string& picked) { assignMaterialFromPrompt(picked); }});
    }, selectionRequired());
    addCommand("material.new", "New material for selection", "Material", {shift(ImGuiKey_M)}, [this] {
        Material material = m_engine.material(m_ctx->world.getRenderable(m_ctx->primary()).material_uuid);
        material.name = std::format("Material {}", m_engine.materialCount());
        const uint32_t id = m_engine.createMaterial(material);
        if (id == 0) return;
        m_ctx->history->beginGroup("New material");
        setSelectionMaterial(id);
        m_ctx->history->endGroup();
        m_ctx->showInspector = true;
    }, selectionRequired());
}

void Application::registerSelectCommands() {
    addCommand("select.all", "Select all", "Select", {ctrl(ImGuiKey_A)}, [this] {
        std::vector<EntityHandle> all;
        m_ctx->world.forEach([&](EntityHandle entity) {
            if (all.size() < kSelectionLimit) all.push_back(entity);
        });
        m_ctx->selection.setAll(all);
        if (m_ctx->world.aliveCount() > kSelectionLimit) m_ctx->toast(std::format("Selected the first {} of {}", kSelectionLimit, m_ctx->world.aliveCount()), ToastKind::Warning);
    });
    addCommand("select.none", "Select none", "Select", {alt(ImGuiKey_A), key(ImGuiKey_Escape)}, [this] { m_ctx->selection.clear(); }, selectionRequired());
    addCommand("select.parent", "Select parent", "Select", {shift(ImGuiKey_LeftBracket)}, [this] { walk(kWalkParent); }, selectionRequired());
    addCommand("select.child", "Select first child", "Select", {shift(ImGuiKey_RightBracket)}, [this] { walk(kWalkChild); }, selectionRequired());
    addCommand("select.next", "Select next sibling", "Select", {key(ImGuiKey_RightBracket)}, [this] { walk(kWalkNext); }).repeat = true;
    addCommand("select.previous", "Select previous sibling", "Select", {key(ImGuiKey_LeftBracket)}, [this] { walk(kWalkPrevious); }).repeat = true;
    addCommand("select.children", "Select children", "Select", {ctrl(ImGuiKey_RightBracket)}, [this] {
        std::vector<EntityHandle> children;
        for (EntityHandle entity : m_ctx->selection.items()) {
            for (EntityHandle child : m_ctx->world.getChildren(entity)) {
                if (children.size() < kSelectionLimit) children.push_back(child);
            }
        }
        if (!children.empty()) m_ctx->selection.setAll(children);
    }, selectionRequired());
    addCommand("select.sameMesh", "Select same mesh", "Select", {ctrl(ImGuiKey_L)}, [this] { selectSimilar(false); }, selectionRequired());
    addCommand("select.sameMaterial", "Select same material", "Select", {ctrlShift(ImGuiKey_L)}, [this] { selectSimilar(true); }, selectionRequired());
    addCommand("select.find", "Find entity...", "Select", {ctrl(ImGuiKey_F)}, [this] {
        m_ctx->openPalette = true;
        m_ctx->paletteSeed = "@";
    }, {}, CommandScope::Global);
}

void Application::registerSelectionSetCommands() {
    constexpr int kSlotCount = 9;
    const ImGuiKey digits[kSlotCount + 1] = {ImGuiKey_0, ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9};
    for (int slot = 1; slot <= kSlotCount; ++slot) {
        const bool first = slot == 1;
        auto describeAsGroup = [first](Command& command, const char* keysLabel) {
            command.palette = first;
            command.help = first;
            if (first) command.keysLabel = keysLabel;
        };
        auto& store = addCommand(std::format("sets.store{}", slot), first ? "Store selection set 1-9" : std::format("Store selection set {}", slot), "Selection sets", {ctrl(digits[slot])}, [this, slot] { storeSelectionSet(slot); }, selectionRequired());
        describeAsGroup(store, "Ctrl+1..9");
        describeAsGroup(addCommand(std::format("sets.recall{}", slot), first ? "Recall selection set 1-9 (Shift also frames)" : std::format("Recall selection set {}", slot), "Selection sets", {key(digits[slot]), shift(digits[slot])}, [this, slot] { recallSelectionSet(slot); }), "1..9");
        describeAsGroup(addCommand(std::format("view.store{}", slot), first ? "Store camera view 1-9" : std::format("Store camera view {}", slot), "Camera views", {ctrlAlt(digits[slot])}, [this, slot] { storeBookmark(slot); }), "Ctrl+Alt+1..9");
        describeAsGroup(addCommand(std::format("view.recall{}", slot), first ? "Recall camera view 1-9" : std::format("Recall camera view {}", slot), "Camera views", {alt(digits[slot])}, [this, slot] { recallBookmark(slot); }), "Alt+1..9");
    }
}

void Application::registerVisibilityCommands() {
    addCommand("hide.selected", "Hide / show selection", "Visibility", {key(ImGuiKey_H)}, [this] {
        const bool visible = !m_ctx->world.isVisible(m_ctx->primary());
        m_ctx->history->beginGroup(visible ? "Show" : "Hide");
        for (EntityHandle entity : m_ctx->topLevelSelection()) m_ctx->history->setVisible(entity, visible);
        m_ctx->history->endGroup();
    }, selectionRequired());
    addCommand("hide.revealAll", "Show everything hidden", "Visibility", {alt(ImGuiKey_H)}, [this] { revealHiddenEntities(); });
}

void Application::registerPlayCommands() {
    addCommand("play.toggle", "Play / stop", "Play", {key(ImGuiKey_F5)}, [this] { setPlaying(!m_ctx->playing); }, {}, CommandScope::Global);
    addCommand("play.pause", "Pause / resume", "Play", {key(ImGuiKey_F6)}, [this] {
        m_ctx->paused = !m_ctx->paused;
        m_engine.setPaused(m_ctx->paused);
    }, [this] { return m_ctx->playing; }, CommandScope::Global);
    addCommand("play.step", "Step one frame", "Play", {key(ImGuiKey_F7)}, [this] { m_stepOnce = true; }, [this] { return m_ctx->playing && m_ctx->paused; }, CommandScope::Global);
}
