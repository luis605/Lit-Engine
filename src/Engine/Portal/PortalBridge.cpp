module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

module Engine.PortalBridge;

std::vector<VisibleCell> visibleCells(const PortalFrame& frame, int shadowDepth) {
    std::vector<VisibleCell> out;
    for (size_t i = 0; i < frame.nodes.size(); ++i) {
        const PortalViewNode& n = frame.nodes[i];
        if (n.cell < 0) continue;
        auto it = std::find_if(out.begin(), out.end(), [&](const VisibleCell& v) { return v.cell == n.cell; });
        if (it != out.end() && it->depth <= n.depth) continue;
        VisibleCell v;
        v.cell = n.cell;
        v.depth = n.depth;
        v.node = static_cast<int>(i);
        v.sun = n.sunScale;
        v.ambient = n.ambientScale;
        v.shadows = n.depth <= shadowDepth;
        if (it != out.end()) *it = v;
        else out.push_back(v);
    }
    std::sort(out.begin(), out.end(), [](const VisibleCell& a, const VisibleCell& b) { return a.depth < b.depth; });
    return out;
}

namespace {
glm::vec3 standPoint(const Portal& p, float standOff) {
    return p.center() - p.up() * p.half.y + p.normal() * standOff;
}
}

size_t PortalNavLinks::sync(NavMesh& nav, const PortalSystem& portals, const PortalNavSettings& settings) {
    clear(nav);
    const auto& list = portals.portals();
    for (size_t i = 0; i < list.size(); ++i) {
        const Portal& a = list[i];
        if (a.partner < 0 || static_cast<size_t>(a.partner) < i) continue;
        const Portal& b = list[static_cast<size_t>(a.partner)];
        if (std::abs(a.up().y) < settings.minUpright || std::abs(b.up().y) < settings.minUpright) continue;
        NavLink link;
        link.a = standPoint(a, settings.standOff);
        link.b = standPoint(b, settings.standOff);
        link.radius = settings.linkRadius;
        link.cost = settings.cost;
        link.bidirectional = true;
        link.userData = static_cast<int>(i);
        m_ids.push_back(nav.addLink(link));
    }
    return m_ids.size();
}

void PortalNavLinks::clear(NavMesh& nav) {
    for (uint32_t id : m_ids) nav.removeLink(id);
    m_ids.clear();
}

AudioHopFn portalAudioHook(const PortalSystem& portals, float standOff) {
    return [&portals, standOff](const glm::vec3& from, std::vector<AudioHop>& out) {
        const int cell = portals.cellAt(from);
        if (cell < 0) return;
        for (int id : portals.portalsOf(cell)) {
            const Portal& a = portals.portals()[static_cast<size_t>(id)];
            if (a.partner < 0) continue;
            const Portal& b = portals.portals()[static_cast<size_t>(a.partner)];
            AudioHop hop;
            hop.entry = a.center() + a.normal() * standOff;
            hop.exit = b.center() + b.normal() * standOff;
            hop.cost = 0.0f;
            hop.id = id;
            out.push_back(hop);
        }
    };
}
