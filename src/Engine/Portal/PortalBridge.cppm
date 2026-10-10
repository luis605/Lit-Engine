module;

#include <cstdint>
#include <vector>

export module Engine.PortalBridge;

import Engine.glm;
import Engine.Nav;
import Engine.Portal;
import Engine.Portal.view;
import Engine.AudioTrace;

export struct VisibleCell {
    int cell = -1;
    int depth = 0;
    int node = 0;
    float sun = 1.0f;
    float ambient = 1.0f;
    bool shadows = false;
};

export [[nodiscard]] std::vector<VisibleCell> visibleCells(const PortalFrame& frame, int shadowDepth = 1);

export struct PortalNavSettings {
    float standOff = 0.7f;
    float linkRadius = 0.6f;
    float cost = 0.0f;
    float minUpright = 0.7f;
};

export class PortalNavLinks {
  public:
    size_t sync(NavMesh& nav, const PortalSystem& portals, const PortalNavSettings& settings = {});
    void clear(NavMesh& nav);
    [[nodiscard]] const std::vector<uint32_t>& ids() const { return m_ids; }

  private:
    std::vector<uint32_t> m_ids;
};

export [[nodiscard]] AudioHopFn portalAudioHook(const PortalSystem& portals, float standOff = 0.1f);
