module;

#include <string>
#include <vector>

export module Editor.hud;

import Editor.context;
import Editor.commands;
import Engine.glm;

export struct PieItem {
    std::string label;
    std::string commandId;
};

export class Hud {
  public:
    void drawStatusBar(EditorContext& ctx, CommandRegistry& commands, float frameMs);
    void drawToasts(EditorContext& ctx, float dt);
    void drawHelp(EditorContext& ctx, CommandRegistry& commands);
    void drawTimeline(EditorContext& ctx);
    bool updatePie(EditorContext& ctx, CommandRegistry& commands, bool allowStart);
    void setPieItems(std::vector<PieItem> items) { m_pieItems = std::move(items); }

  private:
    std::vector<PieItem> m_pieItems;
    bool m_pieArmed = false;
    bool m_pieOpen = false;
    double m_piePressTime = 0.0;
    glm::vec2 m_pieCenter{0.0f};
    int m_pieHover = -1;
    std::string m_helpFilter;
    float m_frameAverage = 16.0f;
};
