module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <unordered_map>
#include <vector>

module Engine.Nav;

namespace {
constexpr int kKeyBias = 524288;

bool triRect(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float x0, float z0, float x1, float z1) {
    const glm::vec2 p[3] = {{a.x, a.z}, {b.x, b.z}, {c.x, c.z}};
    for (int i = 0; i < 3; ++i) {
        const glm::vec2 e = p[(i + 1) % 3] - p[i];
        const glm::vec2 n(-e.y, e.x);
        float tmin = 1.0e30f;
        float tmax = -1.0e30f;
        for (const glm::vec2& q : p) {
            const float d = n.x * q.x + n.y * q.y;
            tmin = std::min(tmin, d);
            tmax = std::max(tmax, d);
        }
        float rmin = 1.0e30f;
        float rmax = -1.0e30f;
        for (int k = 0; k < 4; ++k) {
            const float d = n.x * ((k & 1) ? x1 : x0) + n.y * ((k & 2) ? z1 : z0);
            rmin = std::min(rmin, d);
            rmax = std::max(rmax, d);
        }
        if (tmax < rmin || rmax < tmin) return false;
    }
    return true;
}

int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}
}

uint64_t NavMesh::tileKey(int tx, int tz) { return (static_cast<uint64_t>(tx + kKeyBias) << 32) | static_cast<uint64_t>(tz + kKeyBias); }

uint64_t NavMesh::nodeKey(const NavNodeId& n) { return (static_cast<uint64_t>(n.cx + kKeyBias) << 36) | (static_cast<uint64_t>(n.cz + kKeyBias) << 8) | (n.layer & 0xFFu); }

NavNodeId NavMesh::nodeFromKey(uint64_t k) { return {static_cast<int>(k >> 36) - kKeyBias, static_cast<int>((k >> 8) & 0xFFFFFFFull) - kKeyBias, static_cast<uint32_t>(k & 0xFFu)}; }

int NavMesh::cellOf(float v, float origin) const { return static_cast<int>(std::floor((v - origin) / m_config.cellSize)); }

int NavMesh::tileOf(int cell) const { return floorDiv(cell, m_config.tileCells); }

void NavMesh::configure(const NavConfig& config) {
    m_config = config;
    m_tiles.clear();
    m_dirty.clear();
    ++m_version;
    if (m_bvh.triangleCount() > 0) setGeometry(m_bvh.soup());
}

void NavMesh::setGeometry(const TriangleSoup& soup) {
    m_bvh.build(soup);
    m_bounds = m_bvh.bounds();
    m_tiles.clear();
    m_dirty.clear();
    if (!m_bounds.valid()) return;
    Aabb b = m_bounds;
    markDirty(b);
}

void NavMesh::buildFromWorld(World& world, uint32_t layerMask) {
    TriangleSoup soup;
    collectColliderGeometry(world, soup, layerMask, 60.0f);
    setGeometry(soup);
    rebuildAll();
}

void NavMesh::markDirty(const Aabb& box) {
    const float pad = (std::ceil(m_config.agentRadius / m_config.cellSize) + 2.0f) * m_config.cellSize;
    const int x0 = tileOf(cellOf(box.lo.x - pad, m_config.originX));
    const int x1 = tileOf(cellOf(box.hi.x + pad, m_config.originX));
    const int z0 = tileOf(cellOf(box.lo.z - pad, m_config.originZ));
    const int z1 = tileOf(cellOf(box.hi.z + pad, m_config.originZ));
    for (int x = x0; x <= x1; ++x) {
        for (int z = z0; z <= z1; ++z) m_dirty.insert({x, z});
    }
}

uint32_t NavMesh::addObstacle(const Aabb& box) {
    const uint32_t id = m_nextObstacle++;
    m_obstacles[id] = {box};
    markDirty(box);
    return id;
}

bool NavMesh::moveObstacle(uint32_t id, const Aabb& box) {
    const auto it = m_obstacles.find(id);
    if (it == m_obstacles.end()) return false;
    markDirty(it->second.box);
    it->second.box = box;
    markDirty(box);
    return true;
}

bool NavMesh::removeObstacle(uint32_t id) {
    const auto it = m_obstacles.find(id);
    if (it == m_obstacles.end()) return false;
    markDirty(it->second.box);
    m_obstacles.erase(it);
    return true;
}

uint32_t NavMesh::addLink(NavLink link) {
    link.id = m_nextLink++;
    m_links[link.id] = link;
    ++m_version;
    return link.id;
}

bool NavMesh::removeLink(uint32_t id) {
    if (!m_links.erase(id)) return false;
    ++m_version;
    return true;
}

bool NavMesh::setLinkEnabled(uint32_t id, bool enabled) {
    const auto it = m_links.find(id);
    if (it == m_links.end()) return false;
    if (it->second.enabled != enabled) ++m_version;
    it->second.enabled = enabled;
    return true;
}

const NavLink* NavMesh::link(uint32_t id) const {
    const auto it = m_links.find(id);
    return it == m_links.end() ? nullptr : &it->second;
}

std::vector<NavLink> NavMesh::links() const {
    std::vector<NavLink> out;
    for (const auto& [id, l] : m_links) out.push_back(l);
    return out;
}

void NavMesh::rebuildAll() {
    if (m_bounds.valid()) markDirty(m_bounds);
    rebuildDirty();
}

size_t NavMesh::rebuildDirty(size_t maxTiles) {
    size_t built = 0;
    while (!m_dirty.empty() && built < maxTiles) {
        const auto [tx, tz] = *m_dirty.begin();
        m_dirty.erase(m_dirty.begin());
        buildTile(tx, tz);
        ++built;
    }
    if (built > 0) ++m_version;
    return built;
}

void NavMesh::buildTile(int tx, int tz) {
    const int T = m_config.tileCells;
    const int r = static_cast<int>(std::ceil(m_config.agentRadius / m_config.cellSize));
    const int pad = r + 1;
    const int W = T + 2 * pad;
    const int rx0 = tx * T - pad;
    const int rz0 = tz * T - pad;
    const float cs = m_config.cellSize;
    const float cosSlope = std::cos(m_config.maxSlopeDegrees * 3.14159265f / 180.0f);

    struct Column {
        std::vector<float> floors;
        std::vector<std::pair<float, float>> blockers;
    };
    std::vector<Column> cols(static_cast<size_t>(W) * W);

    Aabb region;
    region.lo = glm::vec3(m_config.originX + rx0 * cs, -1.0e9f, m_config.originZ + rz0 * cs);
    region.hi = glm::vec3(m_config.originX + (rx0 + W) * cs, 1.0e9f, m_config.originZ + (rz0 + W) * cs);

    std::vector<uint32_t> tris;
    m_bvh.query(region, tris);
    const TriangleSoup& soup = m_bvh.soup();
    for (const uint32_t tri : tris) {
        const glm::vec3& a = soup.vertices[soup.indices[tri * 3]];
        const glm::vec3& b = soup.vertices[soup.indices[tri * 3 + 1]];
        const glm::vec3& c = soup.vertices[soup.indices[tri * 3 + 2]];
        glm::vec3 n = glm::cross(b - a, c - a);
        const float len = glm::length(n);
        if (len < 1.0e-12f) continue;
        n = n / len;
        const bool floor = n.y >= cosSlope;
        const float ylo = std::min({a.y, b.y, c.y});
        const float yhi = std::max({a.y, b.y, c.y});
        const int cx0 = std::max(rx0, cellOf(std::min({a.x, b.x, c.x}), m_config.originX));
        const int cx1 = std::min(rx0 + W - 1, cellOf(std::max({a.x, b.x, c.x}), m_config.originX));
        const int cz0 = std::max(rz0, cellOf(std::min({a.z, b.z, c.z}), m_config.originZ));
        const int cz1 = std::min(rz0 + W - 1, cellOf(std::max({a.z, b.z, c.z}), m_config.originZ));
        for (int cz = cz0; cz <= cz1; ++cz) {
            for (int cx = cx0; cx <= cx1; ++cx) {
                const float x0 = m_config.originX + cx * cs;
                const float z0 = m_config.originZ + cz * cs;
                if (!triRect(a, b, c, x0, z0, x0 + cs, z0 + cs)) continue;
                Column& col = cols[static_cast<size_t>(cz - rz0) * W + (cx - rx0)];
                if (floor) {
                    const float px = x0 + cs * 0.5f;
                    const float pz = z0 + cs * 0.5f;
                    const float y = std::clamp(a.y - (n.x * (px - a.x) + n.z * (pz - a.z)) / n.y, ylo, yhi);
                    col.floors.push_back(y);
                    col.blockers.emplace_back(y - 0.01f, y + 0.01f);
                } else {
                    col.blockers.emplace_back(ylo, yhi);
                }
            }
        }
    }
    for (const auto& [id, ob] : m_obstacles) {
        if (!ob.box.overlaps(region)) continue;
        const int cx0 = std::max(rx0, cellOf(ob.box.lo.x, m_config.originX));
        const int cx1 = std::min(rx0 + W - 1, cellOf(ob.box.hi.x, m_config.originX));
        const int cz0 = std::max(rz0, cellOf(ob.box.lo.z, m_config.originZ));
        const int cz1 = std::min(rz0 + W - 1, cellOf(ob.box.hi.z, m_config.originZ));
        for (int cz = cz0; cz <= cz1; ++cz) {
            for (int cx = cx0; cx <= cx1; ++cx) cols[static_cast<size_t>(cz - rz0) * W + (cx - rx0)].blockers.emplace_back(ob.box.lo.y, ob.box.hi.y);
        }
    }

    std::vector<std::vector<float>> spans(cols.size());
    for (size_t i = 0; i < cols.size(); ++i) {
        std::vector<float>& f = cols[i].floors;
        std::sort(f.begin(), f.end());
        std::vector<float> merged;
        for (const float y : f) {
            if (!merged.empty() && y - merged.back() < 0.15f) continue;
            merged.push_back(y);
        }
        for (const float h : merged) {
            bool blocked = false;
            for (const auto& [lo, hi] : cols[i].blockers) {
                if (lo < h + m_config.agentHeight && hi > h + m_config.maxClimb) {
                    blocked = true;
                    break;
                }
            }
            if (!blocked) spans[i].push_back(h);
        }
    }

    Tile tile;
    tile.start.assign(static_cast<size_t>(T) * T + 1, 0);
    for (int z = 0; z < T; ++z) {
        for (int x = 0; x < T; ++x) {
            tile.start[static_cast<size_t>(z) * T + x] = static_cast<uint32_t>(tile.heights.size());
            const int px = x + pad;
            const int pz = z + pad;
            for (const float h : spans[static_cast<size_t>(pz) * W + px]) {
                bool ok = true;
                for (int dz = -r; dz <= r && ok; ++dz) {
                    for (int dx = -r; dx <= r && ok; ++dx) {
                        if (dx * dx + dz * dz > r * r) continue;
                        bool found = false;
                        for (const float h2 : spans[static_cast<size_t>(pz + dz) * W + (px + dx)]) {
                            if (std::fabs(h2 - h) <= m_config.maxClimb) {
                                found = true;
                                break;
                            }
                        }
                        ok = found;
                    }
                }
                if (ok) tile.heights.push_back(h);
            }
        }
    }
    tile.start[static_cast<size_t>(T) * T] = static_cast<uint32_t>(tile.heights.size());
    m_tiles[tileKey(tx, tz)] = std::move(tile);
}

const NavMesh::Tile* NavMesh::tileAt(int cx, int cz) const {
    const auto it = m_tiles.find(tileKey(tileOf(cx), tileOf(cz)));
    return it == m_tiles.end() ? nullptr : &it->second;
}

uint32_t NavMesh::layersAt(int cx, int cz, const float*& heights) const {
    const Tile* t = tileAt(cx, cz);
    if (!t) return 0;
    const int T = m_config.tileCells;
    const int lx = cx - tileOf(cx) * T;
    const int lz = cz - tileOf(cz) * T;
    const size_t col = static_cast<size_t>(lz) * T + lx;
    heights = t->heights.data() + t->start[col];
    return t->start[col + 1] - t->start[col];
}

size_t NavMesh::walkableCount() const {
    size_t n = 0;
    for (const auto& [k, t] : m_tiles) n += t.heights.size();
    return n;
}

uint64_t NavMesh::hash() const {
    std::vector<uint64_t> keys;
    for (const auto& [k, t] : m_tiles) keys.push_back(k);
    std::sort(keys.begin(), keys.end());
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const uint64_t k : keys) {
        const Tile& t = m_tiles.at(k);
        h = (h ^ k) * 0x100000001b3ULL;
        for (const uint32_t s : t.start) h = (h ^ s) * 0x100000001b3ULL;
        for (const float v : t.heights) h = (h ^ static_cast<uint64_t>(std::llround(v * 1000.0f))) * 0x100000001b3ULL;
    }
    return h;
}

glm::vec3 NavMesh::nodePosition(const NavNodeId& n) const {
    const float* hs = nullptr;
    const uint32_t count = layersAt(n.cx, n.cz, hs);
    const float y = n.layer < count ? hs[n.layer] : 0.0f;
    return glm::vec3(m_config.originX + (n.cx + 0.5f) * m_config.cellSize, y, m_config.originZ + (n.cz + 0.5f) * m_config.cellSize);
}

std::optional<NavNodeId> NavMesh::nearestNode(const glm::vec3& p, float maxDistance) const {
    const int cx = cellOf(p.x, m_config.originX);
    const int cz = cellOf(p.z, m_config.originZ);
    const int range = static_cast<int>(std::ceil(maxDistance / m_config.cellSize));
    std::optional<NavNodeId> best;
    float bestD = maxDistance * maxDistance;
    for (int dz = -range; dz <= range; ++dz) {
        for (int dx = -range; dx <= range; ++dx) {
            const float* hs = nullptr;
            const uint32_t count = layersAt(cx + dx, cz + dz, hs);
            for (uint32_t l = 0; l < count; ++l) {
                if (std::fabs(hs[l] - p.y) > m_config.agentHeight) continue;
                const glm::vec3 q(m_config.originX + (cx + dx + 0.5f) * m_config.cellSize, hs[l], m_config.originZ + (cz + dz + 0.5f) * m_config.cellSize);
                const float d = (q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z) + (q.y - p.y) * (q.y - p.y);
                if (d < bestD) {
                    bestD = d;
                    best = NavNodeId{cx + dx, cz + dz, l};
                }
            }
        }
    }
    return best;
}

std::optional<glm::vec3> NavMesh::nearestPoint(const glm::vec3& p, float maxDistance) const {
    const auto n = nearestNode(p, maxDistance);
    if (!n) return std::nullopt;
    return nodePosition(*n);
}

bool NavMesh::walkable(const glm::vec3& p) const {
    const auto n = nearestNode(p, m_config.cellSize * 0.75f);
    return n.has_value();
}

bool NavMesh::connected(const NavNodeId& from, int dx, int dz, NavNodeId& out) const {
    const float* hs = nullptr;
    const uint32_t count = layersAt(from.cx + dx, from.cz + dz, hs);
    const float* mine = nullptr;
    const uint32_t myCount = layersAt(from.cx, from.cz, mine);
    if (from.layer >= myCount) return false;
    const float h = mine[from.layer];
    int best = -1;
    float bestD = m_config.maxClimb + 1.0e-4f;
    for (uint32_t l = 0; l < count; ++l) {
        const float d = std::fabs(hs[l] - h);
        if (d <= bestD) {
            bestD = d;
            best = static_cast<int>(l);
        }
    }
    if (best < 0) return false;
    out = {from.cx + dx, from.cz + dz, static_cast<uint32_t>(best)};
    return true;
}

bool NavMesh::lineWalkable(const glm::vec3& a, const glm::vec3& b) const {
    const auto start = nearestNode(a, m_config.cellSize * 1.5f);
    if (!start) return false;
    const glm::vec3 d = b - a;
    const float dist = std::sqrt(d.x * d.x + d.z * d.z);
    const int steps = std::max(1, static_cast<int>(std::ceil(dist / (m_config.cellSize * 0.5f))));
    float lastY = nodePosition(*start).y;
    for (int i = 1; i <= steps; ++i) {
        const float f = static_cast<float>(i) / steps;
        const glm::vec3 p(a.x + d.x * f, lastY, a.z + d.z * f);
        const int cx = cellOf(p.x, m_config.originX);
        const int cz = cellOf(p.z, m_config.originZ);
        const float* hs = nullptr;
        const uint32_t count = layersAt(cx, cz, hs);
        int best = -1;
        float bestD = m_config.maxClimb * 0.6f;
        for (uint32_t l = 0; l < count; ++l) {
            const float dd = std::fabs(hs[l] - lastY);
            if (dd <= bestD) {
                bestD = dd;
                best = static_cast<int>(l);
            }
        }
        if (best < 0) return false;
        lastY = hs[best];
    }
    return true;
}

void NavMesh::ensureLinks() const {
    if (m_linkVersion == m_version) return;
    m_linkAdj.clear();
    for (const auto& [id, l] : m_links) {
        if (!l.enabled) continue;
        const auto na = nearestNode(l.a, l.radius);
        const auto nb = nearestNode(l.b, l.radius);
        if (!na || !nb) continue;
        m_linkAdj[nodeKey(*na)].push_back({id, true, nodeKey(*nb)});
        if (l.bidirectional) m_linkAdj[nodeKey(*nb)].push_back({id, false, nodeKey(*na)});
    }
    m_linkVersion = m_version;
}

NavPath NavMesh::findPath(const glm::vec3& from, const glm::vec3& to) const {
    NavPath result;
    ensureLinks();
    const auto s = nearestNode(from, m_config.snapDistance);
    const auto g = nearestNode(to, m_config.snapDistance);
    if (!s || !g) return result;
    const uint64_t sk = nodeKey(*s);
    const uint64_t gk = nodeKey(*g);
    const glm::vec3 goalPos = nodePosition(*g);

    struct Rec {
        float g = 0.0f;
        uint64_t parent = 0;
        int link = -1;
        bool fromA = true;
        bool closed = false;
    };
    std::unordered_map<uint64_t, Rec> recs;
    struct Item {
        float f;
        float g;
        uint64_t key;
        bool operator<(const Item& o) const {
            if (f != o.f) return f > o.f;
            if (g != o.g) return g < o.g;
            return key > o.key;
        }
    };
    std::priority_queue<Item> open;
    const auto heur = [&](const glm::vec3& p) { return glm::length(goalPos - p); };
    recs[sk] = {0.0f, sk, -1, true, false};
    open.push({heur(nodePosition(*s)), 0.0f, sk});
    uint64_t bestKey = sk;
    float bestH = heur(nodePosition(*s));
    uint64_t found = 0;
    bool done = false;
    while (!open.empty() && result.expansions < m_config.maxExpansions) {
        const Item it = open.top();
        open.pop();
        Rec& rec = recs[it.key];
        if (rec.closed || it.g > rec.g + 1.0e-6f) continue;
        rec.closed = true;
        const float curG = rec.g;
        ++result.expansions;
        if (it.key == gk) {
            found = it.key;
            done = true;
            break;
        }
        const NavNodeId n = nodeFromKey(it.key);
        const glm::vec3 np = nodePosition(n);
        const float hh = heur(np);
        if (hh < bestH) {
            bestH = hh;
            bestKey = it.key;
        }
        const auto relax = [&](uint64_t key, float cost, int link, bool fromA) {
            const auto existing = recs.find(key);
            const float ng = curG + cost;
            if (existing != recs.end() && (existing->second.closed || existing->second.g <= ng)) return;
            recs[key] = {ng, it.key, link, fromA, false};
            open.push({ng + heur(nodePosition(nodeFromKey(key))), ng, key});
        };
        static const int dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
        for (const auto& d : dirs) {
            NavNodeId m;
            if (!connected(n, d[0], d[1], m)) continue;
            if (d[0] != 0 && d[1] != 0) {
                NavNodeId t1;
                NavNodeId t2;
                if (!connected(n, d[0], 0, t1) || !connected(n, 0, d[1], t2)) continue;
            }
            relax(nodeKey(m), glm::length(nodePosition(m) - np), -1, true);
        }
        const auto la = m_linkAdj.find(it.key);
        if (la != m_linkAdj.end()) {
            for (const LinkEnd& e : la->second) {
                const NavLink& l = m_links.at(e.link);
                relax(e.target, 0.1f + l.cost, static_cast<int>(e.link), e.fromA);
            }
        }
    }
    if (!done) {
        if (bestKey == sk && sk != gk) return result;
        found = bestKey;
        result.partial = true;
    }
    result.found = true;

    std::vector<uint64_t> chain;
    for (uint64_t k = found;; k = recs[k].parent) {
        chain.push_back(k);
        if (k == sk) break;
    }
    std::reverse(chain.begin(), chain.end());

    std::vector<NavPathPoint> raw;
    raw.push_back({from, -1, false});
    for (size_t i = 0; i < chain.size(); ++i) {
        const Rec& rec = recs[chain[i]];
        if (i > 0 && rec.link >= 0) {
            const NavLink& l = m_links.at(static_cast<uint32_t>(rec.link));
            raw.push_back({rec.fromA ? l.a : l.b, rec.link, false});
            raw.push_back({rec.fromA ? l.b : l.a, -1, true});
        } else {
            raw.push_back({nodePosition(nodeFromKey(chain[i])), -1, false});
        }
    }
    raw.push_back({result.partial ? nodePosition(nodeFromKey(found)) : to, -1, false});

    std::vector<NavPathPoint> out;
    size_t i = 0;
    out.push_back(raw[0]);
    while (i + 1 < raw.size()) {
        if (raw[i].link >= 0) {
            out.push_back(raw[i + 1]);
            i += 1;
            continue;
        }
        size_t far = i + 1;
        for (size_t j = std::min(raw.size() - 1, i + 24); j > i + 1; --j) {
            bool clean = true;
            for (size_t k = i + 1; k <= j; ++k) {
                if (raw[k].link >= 0 || raw[k].linkExit) {
                    clean = false;
                    break;
                }
            }
            if (clean && lineWalkable(raw[i].position, raw[j].position)) {
                far = j;
                break;
            }
        }
        out.push_back(raw[far]);
        i = far;
    }
    for (size_t k = 1; k < out.size(); ++k) {
        if (!out[k].linkExit) result.length += glm::length(out[k].position - out[k - 1].position);
    }
    result.points = std::move(out);
    return result;
}

void navSetTarget(const NavMesh& mesh, NavAgent& agent, const glm::vec3& target) {
    agent.target = target;
    agent.hasTarget = true;
    agent.path = mesh.findPath(agent.position, target);
    agent.waypoint = 1;
    agent.pathVersion = mesh.version();
    agent.state = agent.path.found ? AgentState::Moving : AgentState::Failed;
}

void navStepAgent(const NavMesh& mesh, NavAgent& agent, float dt, const std::vector<NavAgent>* others, const NavLinkTraverseFn& traverse) {
    if (!agent.hasTarget || agent.state == AgentState::Arrived || agent.state == AgentState::Idle) return;
    if (agent.pathVersion != mesh.version()) {
        agent.path = mesh.findPath(agent.position, agent.target);
        agent.waypoint = 1;
        agent.pathVersion = mesh.version();
        ++agent.repaths;
        agent.state = agent.path.found ? AgentState::Moving : AgentState::Failed;
    }
    if (agent.state == AgentState::Failed || agent.path.points.empty()) return;

    while (agent.waypoint < agent.path.points.size()) {
        const NavPathPoint& wp = agent.path.points[agent.waypoint];
        glm::vec3 d = wp.position - agent.position;
        const float flat = std::sqrt(d.x * d.x + d.z * d.z);
        if (wp.link >= 0) {
            const NavLink* l = mesh.link(static_cast<uint32_t>(wp.link));
            const float reach = l ? std::max(l->radius * 0.5f, 0.15f) : 0.15f;
            if (flat <= reach) {
                const glm::vec3 exit = agent.waypoint + 1 < agent.path.points.size() ? agent.path.points[agent.waypoint + 1].position : wp.position;
                if (l && traverse) traverse(agent, *l, exit);
                else agent.position = exit;
                agent.velocity = glm::vec3(0.0f);
                agent.waypoint += 2;
                continue;
            }
        } else if (flat <= std::max(agent.stopDistance, 0.12f)) {
            ++agent.waypoint;
            continue;
        }
        break;
    }
    if (agent.waypoint >= agent.path.points.size()) {
        agent.velocity = glm::vec3(0.0f);
        agent.state = agent.path.partial ? AgentState::Failed : AgentState::Arrived;
        return;
    }

    const NavPathPoint& wp = agent.path.points[agent.waypoint];
    glm::vec3 d = wp.position - agent.position;
    d.y = 0.0f;
    const float dist = glm::length(d);
    const glm::vec3 dir = dist > 1.0e-6f ? d / dist : glm::vec3(0.0f);
    const bool last = agent.waypoint + 1 >= agent.path.points.size();
    float speed = agent.maxSpeed;
    if (last && agent.arriveRadius > 0.0f && dist < agent.arriveRadius) speed *= std::max(dist / agent.arriveRadius, 0.15f);
    glm::vec3 desired = dir * speed;
    if (others) {
        for (const NavAgent& o : *others) {
            if (&o == &agent) continue;
            glm::vec3 away = agent.position - o.position;
            away.y = 0.0f;
            const float dd = glm::length(away);
            const float sum = agent.radius + o.radius;
            if (dd < sum * 2.0f && dd > 1.0e-5f) desired = desired + away / dd * (agent.separationWeight * agent.maxSpeed * (1.0f - dd / (sum * 2.0f)));
        }
    }
    const float dl = glm::length(desired);
    if (dl > agent.maxSpeed) desired = desired * (agent.maxSpeed / dl);
    glm::vec3 dv = desired - agent.velocity;
    const float dvl = glm::length(dv);
    const float maxDv = agent.maxAccel * dt;
    if (dvl > maxDv) dv = dv * (maxDv / dvl);
    agent.velocity = agent.velocity + dv;
    agent.velocity.y = 0.0f;
    float step = glm::length(agent.velocity) * dt;
    glm::vec3 next = agent.position + agent.velocity * dt;
    if (last && dist > 0.0f && step > dist) next = agent.position + dir * dist;
    const auto ground = mesh.nearestPoint(glm::vec3(next.x, agent.position.y, next.z), mesh.config().cellSize * 1.5f);
    if (ground) {
        next.y = ground->y;
        agent.position = next;
    } else {
        agent.velocity = glm::vec3(0.0f);
        agent.pathVersion = 0;
    }
}

void navStepAgents(const NavMesh& mesh, std::vector<NavAgent>& agents, float dt, const NavLinkTraverseFn& traverse) {
    const std::vector<NavAgent> snapshot = agents;
    for (size_t i = 0; i < agents.size(); ++i) {
        std::vector<NavAgent> others;
        for (size_t j = 0; j < snapshot.size(); ++j) {
            if (j != i) others.push_back(snapshot[j]);
        }
        navStepAgent(mesh, agents[i], dt, &others, traverse);
    }
}
