module;

#include <string>
#include <vector>

export module Editor.look;

import Editor.context;

export struct LookState {
    float lodBias = 1.0f;
    int forcedLod = -1;
    float smallObjectThreshold = 0.0f;
    bool debugDepth = false;
    bool animateStress = true;
};

export class LookPanel {
  public:
    void draw(EditorContext& ctx, LookState& state);

  private:
    std::vector<float> m_gpuHistory = std::vector<float>(120, 0.0f);
    size_t m_historyHead = 0;
};
