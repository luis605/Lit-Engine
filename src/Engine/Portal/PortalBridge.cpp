module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

module Engine.PortalBridge;

std::vector<VisibleCell> visibleCells(const PortalFrame& frame, int shadowDepth) {
    std::vector<VisibleCell> visible;
    for (size_t nodeIndex = 0; nodeIndex < frame.nodes.size(); ++nodeIndex) {
        const PortalViewNode& node = frame.nodes[nodeIndex];
        if (node.cell < 0) continue;
        auto existing = std::find_if(visible.begin(), visible.end(), [&](const VisibleCell& entry) { return entry.cell == node.cell; });
        if (existing != visible.end() && existing->depth <= node.depth) continue;
        VisibleCell entry;
        entry.cell = node.cell;
        entry.depth = node.depth;
        entry.node = static_cast<int>(nodeIndex);
        entry.sun = node.sunScale;
        entry.ambient = node.ambientScale;
        entry.shadows = node.depth <= shadowDepth;
        if (existing != visible.end()) *existing = entry;
        else visible.push_back(entry);
    }
    std::sort(visible.begin(), visible.end(), [](const VisibleCell& left, const VisibleCell& right) { return left.depth < right.depth; });
    return visible;
}

namespace {
glm::vec3 standPoint(const Portal& portal, float standOff) {
    return portal.center() - portal.up() * portal.half.y + portal.normal() * standOff;
}
}

size_t PortalNavLinks::sync(NavMesh& nav, const PortalSystem& portals, const PortalNavSettings& settings) {
    clear(nav);
    const auto& list = portals.portals();
    for (size_t index = 0; index < list.size(); ++index) {
        const Portal& portalA = list[index];
        if (portalA.partner < 0 || static_cast<size_t>(portalA.partner) < index) continue;
        const Portal& portalB = list[static_cast<size_t>(portalA.partner)];
        if (std::abs(portalA.up().y) < settings.minUpright || std::abs(portalB.up().y) < settings.minUpright) continue;
        NavLink link;
        link.a = standPoint(portalA, settings.standOff);
        link.b = standPoint(portalB, settings.standOff);
        link.radius = settings.linkRadius;
        link.cost = settings.cost;
        link.bidirectional = true;
        link.userData = static_cast<int>(index);
        m_ids.push_back(nav.addLink(link));
    }
    return m_ids.size();
}

void PortalNavLinks::clear(NavMesh& nav) {
    for (uint32_t linkId : m_ids) nav.removeLink(linkId);
    m_ids.clear();
}

AudioHopFn portalAudioHook(const PortalSystem& portals, float standOff) {
    return [&portals, standOff](const glm::vec3& from, std::vector<AudioHop>& out) {
        const int cell = portals.cellAt(from);
        if (cell < 0) return;
        for (int portalId : portals.portalsOf(cell)) {
            const Portal& portalA = portals.portals()[static_cast<size_t>(portalId)];
            if (portalA.partner < 0) continue;
            const Portal& portalB = portals.portals()[static_cast<size_t>(portalA.partner)];
            AudioHop hop;
            hop.entry = portalA.center() + portalA.normal() * standOff;
            hop.exit = portalB.center() + portalB.normal() * standOff;
            hop.cost = 0.0f;
            hop.id = portalId;
            out.push_back(hop);
        }
    };
}
