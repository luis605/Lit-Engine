module;

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

export module Editor.palette;

import Editor.context;
import Editor.commands;
import Engine.Render.entity;

export struct PromptRequest {
    std::string title;
    std::string initial;
    std::string hint;
    std::vector<std::string> suggestions;
    std::function<void(const std::string&)> accept;
};

export class Palette {
  public:
    void open(EditorContext& ctx, std::string seed = {});
    void prompt(PromptRequest request);
    void draw(EditorContext& ctx, CommandRegistry& commands);
    [[nodiscard]] bool isOpen() const { return m_open; }

  private:
    struct Result {
        enum class Kind { Command, Entity, Calculator, Suggestion, Help };
        Kind kind = Kind::Command;
        std::string title;
        std::string detail;
        std::string shortcut;
        std::string commandId;
        EntityHandle entity;
        float score = 0.0f;
        bool enabled = true;
    };

    void rebuild(EditorContext& ctx, CommandRegistry& commands);
    void scanEntities(EditorContext& ctx);
    void activate(EditorContext& ctx, CommandRegistry& commands, const Result& result);

    bool m_open = false;
    bool m_focus = false;
    std::string m_text;
    std::string m_builtFor = "\x01";
    std::vector<Result> m_results;
    std::vector<Result> m_entityResults;
    int m_cursor = 0;
    Entity m_scanCursor = 0;
    bool m_scanDone = true;
    std::string m_scanPattern;
    bool m_promptMode = false;
    PromptRequest m_prompt;
    bool m_scrollToCursor = false;
};
