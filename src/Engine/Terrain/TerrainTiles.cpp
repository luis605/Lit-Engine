module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

module Engine.Terrain;

namespace {
struct Leaf {
    TileKey key;
    float distance = 0.0f;
};

float boxDistance(const glm::vec3& low, const glm::vec3& high, const glm::vec3& camera) {
    return glm::length(glm::max(glm::max(low - camera, camera - high), glm::vec3(0.0f)));
}

float viewDistanceTo(const glm::vec3& low, const glm::vec3& high, const glm::vec3& camera, const LodParams& lod) {
    float distance = boxDistance(low, high, camera);
    for (const TerrainViewpoint& view : lod.extraViews) distance = std::min(distance, boxDistance(low, high, view.position));
    return distance;
}

bool seenByAnyView(const glm::vec3& low, const glm::vec3& high, const glm::vec3& camera, const LodParams& lod) {
    const auto sees = [&](const glm::vec3& position, const std::array<glm::vec4, 6>& planes) {
        return boxDistance(low, high, position) <= lod.keepRadius || boxInFrustum(planes, low, high, lod.frustumMargin);
    };
    return sees(camera, lod.planes) || std::ranges::any_of(lod.extraViews, [&](const TerrainViewpoint& view) { return sees(view.position, view.planes); });
}

void collectTiles(const TerrainConfig& config, const glm::vec3& camera, const LodParams& lod, const TileKey& key, std::vector<TileKey>& out) {
    const float size = tileWorldSize(config, key);
    const glm::vec3 low(static_cast<float>(key.x) * size, config.minHeight, static_cast<float>(key.z) * size);
    const glm::vec3 high(low.x + size, config.maxHeight, low.z + size);
    const float distance = viewDistanceTo(low, high, camera, lod);
    if (distance > lod.viewDistance) return;
    if (key.level > 0 && distance < lod.splitFactor * size) {
        for (int dz = 0; dz < 2; ++dz) {
            for (int dx = 0; dx < 2; ++dx) collectTiles(config, camera, lod, TileKey{key.x * 2 + dx, key.z * 2 + dz, key.level - 1}, out);
        }
        return;
    }
    out.push_back(key);
}

uint32_t balanceLeaves(std::vector<TileKey>& leaves, int maxLevel) {
    uint32_t splits = 0;
    for (int pass = 0; pass < maxLevel + 2; ++pass) {
        std::map<std::array<int, 3>, size_t> index;
        for (size_t i = 0; i < leaves.size(); ++i) index[{leaves[i].level, leaves[i].x, leaves[i].z}] = i;
        const auto find = [&](int level, long long px, long long pz) -> int {
            for (int l = 0; l <= maxLevel; ++l) {
                const long long shift = l;
                const auto it = index.find({l, static_cast<int>(px >> shift), static_cast<int>(pz >> shift)});
                if (it != index.end()) return static_cast<int>(it->second);
            }
            (void)level;
            return -1;
        };
        std::vector<char> split(leaves.size(), 0);
        bool any = false;
        for (const TileKey& leaf : leaves) {
            const long long extent = 1LL << leaf.level;
            const long long x0 = static_cast<long long>(leaf.x) * extent;
            const long long z0 = static_cast<long long>(leaf.z) * extent;
            const long long mid = extent / 2;
            const long long probes[4][2] = {{x0 + mid, z0 - 1}, {x0 + mid, z0 + extent}, {x0 - 1, z0 + mid}, {x0 + extent, z0 + mid}};
            for (const auto& probe : probes) {
                const int other = find(leaf.level, probe[0], probe[1]);
                if (other >= 0 && leaves[static_cast<size_t>(other)].level > leaf.level + 1 && !split[static_cast<size_t>(other)]) {
                    split[static_cast<size_t>(other)] = 1;
                    any = true;
                }
            }
        }
        if (!any) break;
        std::vector<TileKey> next;
        next.reserve(leaves.size() + 8);
        for (size_t i = 0; i < leaves.size(); ++i) {
            if (!split[i]) {
                next.push_back(leaves[i]);
                continue;
            }
            ++splits;
            for (int dz = 0; dz < 2; ++dz) {
                for (int dx = 0; dx < 2; ++dx) next.push_back(TileKey{leaves[i].x * 2 + dx, leaves[i].z * 2 + dz, leaves[i].level - 1});
            }
        }
        leaves = std::move(next);
    }
    return splits;
}

glm::vec3 outwardHint(int edge) {
    switch (edge) {
    case 0: return glm::vec3(0.0f, 0.0f, -1.0f);
    case 1: return glm::vec3(0.0f, 0.0f, 1.0f);
    case 2: return glm::vec3(-1.0f, 0.0f, 0.0f);
    default: return glm::vec3(1.0f, 0.0f, 0.0f);
    }
}
}

float tileWorldSize(const TerrainConfig& config, const TileKey& key) { return config.tileSize * static_cast<float>(1u << static_cast<unsigned>(std::max(key.level, 0))); }

glm::vec3 tileOrigin(const TerrainConfig& config, const TileKey& key) {
    const float size = tileWorldSize(config, key);
    return glm::vec3(static_cast<float>(key.x) * size, 0.0f, static_cast<float>(key.z) * size);
}

std::string tileName(const TileKey& key) { return "terrain " + std::to_string(key.x) + "," + std::to_string(key.z) + "," + std::to_string(key.level); }

std::array<glm::vec4, 6> frustumPlanes(const glm::mat4& m) {
    const auto row = [&](int r) { return glm::vec4(m[0][r], m[1][r], m[2][r], m[3][r]); };
    std::array<glm::vec4, 6> planes = {row(3) + row(0), row(3) - row(0), row(3) + row(1), row(3) - row(1), row(2), row(3) - row(2)};
    for (glm::vec4& plane : planes) {
        const float length = glm::length(glm::vec3(plane));
        if (length > 1.0e-12f) plane /= length;
    }
    return planes;
}

bool boxInFrustum(const std::array<glm::vec4, 6>& planes, const glm::vec3& low, const glm::vec3& high, float margin) {
    for (const glm::vec4& plane : planes) {
        const glm::vec3 corner(plane.x >= 0.0f ? high.x : low.x, plane.y >= 0.0f ? high.y : low.y, plane.z >= 0.0f ? high.z : low.z);
        if (glm::dot(glm::vec3(plane), corner) + plane.w < -margin) return false;
    }
    return true;
}

std::vector<SelectedTile> selectTerrainTiles(const TerrainConfig& config, const glm::vec3& camera, const LodParams& lodIn) {
    LodParams lod = lodIn;
    lod.maxLevel = std::clamp(lod.maxLevel, 0, 20);
    std::vector<TileKey> leaves;
    const float rootSize = config.tileSize * static_cast<float>(1u << static_cast<unsigned>(lod.maxLevel));
    std::set<std::pair<int, int>> roots;
    const auto addRoots = [&](const glm::vec3& position) {
        const int x0 = static_cast<int>(std::floor((position.x - lod.viewDistance) / rootSize));
        const int x1 = static_cast<int>(std::floor((position.x + lod.viewDistance) / rootSize));
        const int z0 = static_cast<int>(std::floor((position.z - lod.viewDistance) / rootSize));
        const int z1 = static_cast<int>(std::floor((position.z + lod.viewDistance) / rootSize));
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) roots.insert({z, x});
        }
    };
    addRoots(camera);
    for (const TerrainViewpoint& view : lod.extraViews) addRoots(view.position);
    for (const auto& [z, x] : roots) collectTiles(config, camera, lod, TileKey{x, z, lod.maxLevel}, leaves);
    if (lod.balance) balanceLeaves(leaves, lod.maxLevel);

    std::vector<SelectedTile> out;
    out.reserve(leaves.size());
    for (const TileKey& key : leaves) {
        const float size = tileWorldSize(config, key);
        const glm::vec3 low(static_cast<float>(key.x) * size, config.minHeight, static_cast<float>(key.z) * size);
        const glm::vec3 high(low.x + size, config.maxHeight, low.z + size);
        const float distance = viewDistanceTo(low, high, camera, lod);
        if (lod.frustumCull && !seenByAnyView(low, high, camera, lod)) continue;
        float morph = 0.0f;
        if (key.level < lod.maxLevel) {
            const float range = 2.0f * lod.splitFactor * size;
            morph = std::clamp((distance - lod.morphStart * range) / std::max(range * (1.0f - lod.morphStart), 1.0e-6f), 0.0f, 1.0f);
        }
        out.push_back({key, distance, morph, low, high});
    }
    return out;
}

std::shared_ptr<HeightfieldData> buildTileHeightfield(const TerrainConfig& config, const TileKey& key) {
    auto field = std::make_shared<HeightfieldData>();
    const int n = std::max(config.tileCells, 1);
    const float size = tileWorldSize(config, key);
    const glm::vec3 origin = tileOrigin(config, key);
    field->samplesX = n + 1;
    field->samplesZ = n + 1;
    field->cellSize = size / static_cast<float>(n);
    field->heights.resize(static_cast<size_t>(n + 1) * static_cast<size_t>(n + 1));
    for (int j = 0; j <= n; ++j) {
        for (int i = 0; i <= n; ++i) {
            field->heights[static_cast<size_t>(j) * static_cast<size_t>(n + 1) + static_cast<size_t>(i)] = terrainHeight(config, origin.x + static_cast<float>(i) * field->cellSize, origin.z + static_cast<float>(j) * field->cellSize);
        }
    }
    field->updateBounds();
    return field;
}

TerrainTileMesh buildTileMesh(const TerrainConfig& config, const TileKey& key, const TileMeshOptions& options) {
    const int n = std::max(config.tileCells, 2);
    int stride = std::max(options.stride, 1);
    if (n % stride != 0 || (n / stride) < 2) stride = 1;
    const int m = n / stride;
    const float size = tileWorldSize(config, key);
    const float step = size / static_cast<float>(m);
    const glm::vec3 origin = tileOrigin(config, key);
    const size_t row = static_cast<size_t>(m + 1);
    const size_t surface = row * row;

    std::vector<float> heights(surface);
    for (int j = 0; j <= m; ++j) {
        for (int i = 0; i <= m; ++i) heights[static_cast<size_t>(j) * row + static_cast<size_t>(i)] = terrainHeight(config, origin.x + static_cast<float>(i) * step, origin.z + static_cast<float>(j) * step);
    }
    const auto at = [&](int i, int j) { return heights[static_cast<size_t>(std::min(j, m)) * row + static_cast<size_t>(std::min(i, m))]; };

    std::vector<float> vertices;
    std::vector<glm::vec4> splat;
    std::vector<float> morph;
    vertices.reserve((surface + row * 4) * kVertexFloats);
    splat.reserve(surface + row * 4);
    morph.reserve(surface + row * 4);
    glm::vec3 boundsMin(0.0f, 1.0e30f, 0.0f);
    glm::vec3 boundsMax(size, -1.0e30f, size);
    glm::vec4 totalWeights(0.0f);

    const auto pushVertex = [&](const glm::vec3& position, const glm::vec3& normal, const glm::vec4& weights, float morphDelta) {
        const float packed = options.packMorph ? 1.0f + morphDelta / kTerrainMorphScale : 1.0f;
        vertices.insert(vertices.end(), {position.x, position.y, position.z, normal.x * packed, normal.y * packed, normal.z * packed, (origin.x + position.x) * config.uvScale, (origin.z + position.z) * config.uvScale});
        splat.push_back(weights);
        morph.push_back(morphDelta);
        boundsMin.y = std::min(boundsMin.y, position.y);
        boundsMax.y = std::max(boundsMax.y, position.y);
    };

    for (int j = 0; j <= m; ++j) {
        for (int i = 0; i <= m; ++i) {
            const float x = static_cast<float>(i) * step;
            const float z = static_cast<float>(j) * step;
            const float h = at(i, j);
            const glm::vec3 normal = terrainNormal(config, origin.x + x, origin.z + z, step);
            const glm::vec4 weights = splatWeights(h, normal.y, config.splat);
            totalWeights += weights;
            float delta = 0.0f;
            if ((i & 1) || (j & 1)) {
                const int ci = i & ~1;
                const int cj = j & ~1;
                const float fx = static_cast<float>(i - ci) * 0.5f;
                const float fz = static_cast<float>(j - cj) * 0.5f;
                const float h00 = at(ci, cj);
                const float h10 = at(ci + 2, cj);
                const float h01 = at(ci, cj + 2);
                const float h11 = at(ci + 2, cj + 2);
                const float coarse = fz >= fx ? h00 + (h11 - h01) * fx + (h01 - h00) * fz : h00 + (h10 - h00) * fx + (h11 - h10) * fz;
                delta = coarse - h;
            }
            pushVertex(glm::vec3(x, h, z), normal, weights, delta);
        }
    }

    std::vector<unsigned int> indices;
    indices.reserve(static_cast<size_t>(m) * static_cast<size_t>(m) * 6 + (options.skirts ? static_cast<size_t>(m) * 24 : 0));
    const auto vertexIndex = [&](int i, int j) { return static_cast<unsigned int>(static_cast<size_t>(j) * row + static_cast<size_t>(i)); };
    const auto pushTriangle = [&](unsigned int a, unsigned int b, unsigned int c) {
        if (options.flipWinding) std::swap(b, c);
        indices.insert(indices.end(), {a, b, c});
    };
    for (int j = 0; j < m; ++j) {
        for (int i = 0; i < m; ++i) {
            pushTriangle(vertexIndex(i, j), vertexIndex(i, j + 1), vertexIndex(i + 1, j + 1));
            pushTriangle(vertexIndex(i, j), vertexIndex(i + 1, j + 1), vertexIndex(i + 1, j));
        }
    }

    if (options.skirts) {
        const float depth = config.skirtDepth * static_cast<float>(1u << static_cast<unsigned>(std::max(key.level, 0)));
        const auto vertexPosition = [&](unsigned int index) { return glm::vec3(vertices[index * kVertexFloats], vertices[index * kVertexFloats + 1], vertices[index * kVertexFloats + 2]); };
        for (int edge = 0; edge < 4; ++edge) {
            std::vector<unsigned int> top(row);
            std::vector<unsigned int> skirt(row);
            for (int k = 0; k <= m; ++k) {
                const int i = edge < 2 ? k : (edge == 2 ? 0 : m);
                const int j = edge < 2 ? (edge == 0 ? 0 : m) : k;
                const unsigned int source = vertexIndex(i, j);
                top[static_cast<size_t>(k)] = source;
                skirt[static_cast<size_t>(k)] = static_cast<unsigned int>(splat.size());
                const glm::vec3 position = vertexPosition(source) - glm::vec3(0.0f, depth, 0.0f);
                const glm::vec3 normal = glm::normalize(glm::vec3(vertices[source * kVertexFloats + 3], vertices[source * kVertexFloats + 4], vertices[source * kVertexFloats + 5]));
                pushVertex(position, normal, splat[source], morph[source]);
                vertices[(vertices.size() / kVertexFloats - 1) * kVertexFloats + 6] = vertices[source * kVertexFloats + 6];
                vertices[(vertices.size() / kVertexFloats - 1) * kVertexFloats + 7] = vertices[source * kVertexFloats + 7];
            }
            const glm::vec3 hint = outwardHint(edge);
            const auto emit = [&](unsigned int a, unsigned int b, unsigned int c) {
                const glm::vec3 normal = glm::cross(vertexPosition(b) - vertexPosition(a), vertexPosition(c) - vertexPosition(a));
                if (glm::dot(normal, hint) < 0.0f) std::swap(b, c);
                if (options.flipWinding) std::swap(b, c);
                indices.insert(indices.end(), {a, b, c});
            };
            for (int k = 0; k < m; ++k) {
                emit(top[static_cast<size_t>(k)], top[static_cast<size_t>(k + 1)], skirt[static_cast<size_t>(k)]);
                emit(top[static_cast<size_t>(k + 1)], skirt[static_cast<size_t>(k + 1)], skirt[static_cast<size_t>(k)]);
            }
        }
    }

    TerrainTileMesh result{Mesh(std::move(vertices), std::move(indices)), std::move(splat), std::move(morph), boundsMin, boundsMax, static_cast<uint32_t>(surface), dominantLayer(totalWeights)};
    return result;
}

std::vector<uint8_t> bakeTileSplat(const TerrainConfig& config, const TileKey& key, int resolution) {
    const int r = std::max(resolution, 1);
    std::vector<uint8_t> pixels(static_cast<size_t>(r) * static_cast<size_t>(r) * 4);
    const float size = tileWorldSize(config, key);
    const glm::vec3 origin = tileOrigin(config, key);
    const float texel = size / static_cast<float>(r);
    for (int j = 0; j < r; ++j) {
        for (int i = 0; i < r; ++i) {
            const float x = origin.x + (static_cast<float>(i) + 0.5f) * texel;
            const float z = origin.z + (static_cast<float>(j) + 0.5f) * texel;
            const glm::vec4 weights = splatWeights(terrainHeight(config, x, z), terrainNormal(config, x, z, texel).y, config.splat);
            for (int channel = 0; channel < 4; ++channel) pixels[(static_cast<size_t>(j) * static_cast<size_t>(r) + static_cast<size_t>(i)) * 4 + static_cast<size_t>(channel)] = static_cast<uint8_t>(std::lround(std::clamp(weights[channel], 0.0f, 1.0f) * 255.0f));
        }
    }
    return pixels;
}
