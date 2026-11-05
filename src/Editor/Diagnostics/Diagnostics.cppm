module;

#include <string>
#include <vector>

export module Editor.diagnostics;

import Editor.context;
import Engine.SceneValidator;

export class Diagnostics {
  public:
    void validate(EditorContext& ctx);
    void requestExplain(EditorContext& ctx);
    void update(EditorContext& ctx, float frameMs);
    void draw(EditorContext& ctx);
    [[nodiscard]] const std::vector<ValidationIssue>& issues() const { return m_issues; }
    [[nodiscard]] const std::vector<FrameFinding>& findings() const { return m_findings; }

  private:
    void explainNow(EditorContext& ctx, float frameMs);
    void drawIssues(EditorContext& ctx);
    void drawFindings();

    std::vector<ValidationIssue> m_issues;
    std::vector<FrameFinding> m_findings;
    bool m_open = false;
    bool m_validated = false;
    int m_explainCountdown = 0;
    bool m_restoreProfiler = false;
};
