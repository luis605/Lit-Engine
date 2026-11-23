module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

module Engine.Terrain;

import Engine.Rng;

namespace {
uint64_t mix64(uint64_t h) {
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 27;
    h *= 0x94D049BB133111EBULL;
    h ^= h >> 31;
    return h;
}

uint64_t cellSeed(uint64_t seed, uint32_t species, int cx, int cz) {
    uint64_t h = mix64(seed ^ (static_cast<uint64_t>(species) + 1) * 0x9E3779B97F4A7C15ULL);
    h = mix64(h ^ static_cast<uint64_t>(static_cast<uint32_t>(cx)));
    return mix64(h ^ (static_cast<uint64_t>(static_cast<uint32_t>(cz)) << 32));
}

float smooth01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}

float terrainBiome(const ScatterConfig& scatter, float x, float z) {
    const float f = scatter.biomeFrequency;
    const float a = terrainValueNoise(scatter.seed ^ 0xB10E5ULL, x * f, z * f);
    const float b = terrainValueNoise(scatter.seed ^ 0xB10E6ULL, x * f * 2.3f + 5.1f, z * f * 2.3f - 3.7f);
    return std::clamp(0.5f + 0.5f * (a * 0.75f + b * 0.25f) * 1.4f, 0.0f, 1.0f);
}

std::vector<ScatterInstance> scatterTile(const TerrainConfig& config, const ScatterConfig& scatter, const TileKey& key) {
    std::vector<ScatterInstance> out;
    const float size = tileWorldSize(config, key);
    const glm::vec3 origin = tileOrigin(config, key);
    const float minX = origin.x;
    const float minZ = origin.z;
    const float maxX = origin.x + size;
    const float maxZ = origin.z + size;
    for (uint32_t index = 0; index < scatter.species.size(); ++index) {
        const ScatterSpecies& species = scatter.species[index];
        const float spacing = std::max(species.spacing, 0.1f);
        const int cx0 = static_cast<int>(std::floor(minX / spacing));
        const int cx1 = static_cast<int>(std::floor(maxX / spacing));
        const int cz0 = static_cast<int>(std::floor(minZ / spacing));
        const int cz1 = static_cast<int>(std::floor(maxZ / spacing));
        for (int cz = cz0; cz <= cz1; ++cz) {
            for (int cx = cx0; cx <= cx1; ++cx) {
                Pcg32 rng(cellSeed(scatter.seed, index, cx, cz), mix64(static_cast<uint64_t>(index) + 77));
                const float jitterX = rng.uniform();
                const float jitterZ = rng.uniform();
                const float roll = rng.uniform();
                const float scaleRoll = rng.uniform();
                const float yaw = rng.uniform() * 6.2831853f;
                const float x = (static_cast<float>(cx) + jitterX) * spacing;
                const float z = (static_cast<float>(cz) + jitterZ) * spacing;
                if (x < minX || x >= maxX || z < minZ || z >= maxZ) continue;
                const float height = terrainHeight(config, x, z);
                if (height < species.minHeight || height > species.maxHeight) continue;
                const glm::vec3 normal = terrainNormal(config, x, z, std::min(spacing, 2.0f));
                if (normal.y < species.minNormalY) continue;
                const float biome = terrainBiome(scatter, x, z);
                if (biome < species.minBiome || biome > species.maxBiome) continue;
                float probability = species.density;
                if (species.clump > 0.0f) {
                    const float clump = smooth01(0.5f + 0.5f * terrainValueNoise(scatter.seed ^ (0xC1A0ULL + index), x * 0.05f, z * 0.05f) * 1.5f);
                    probability *= 1.0f + (clump - 1.0f) * species.clump;
                }
                if (roll >= probability) continue;
                ScatterInstance instance;
                instance.species = index;
                instance.position = glm::vec3(x, height, z);
                instance.normal = normal;
                instance.yaw = yaw;
                instance.scale = species.minScale + (species.maxScale - species.minScale) * scaleRoll;
                instance.biome = biome;
                instance.cellX = static_cast<uint32_t>(cx);
                instance.cellZ = static_cast<uint32_t>(cz);
                instance.position.y += species.yOffset * instance.scale;
                out.push_back(instance);
            }
        }
    }
    return out;
}

std::string scatterInstanceKey(const ScatterConfig& scatter, uint32_t species, int32_t cellX, int32_t cellZ) {
    char seed[24];
    std::snprintf(seed, sizeof(seed), "%llx", static_cast<unsigned long long>(scatter.seed));
    return std::string("scatter/") + seed + "/" + std::to_string(species) + "/" + std::to_string(cellX) + "_" + std::to_string(cellZ);
}

std::string scatterInstanceKey(const ScatterConfig& scatter, const ScatterInstance& instance) {
    return scatterInstanceKey(scatter, instance.species, static_cast<int32_t>(instance.cellX), static_cast<int32_t>(instance.cellZ));
}

std::vector<CellEntity> scatterEntities(const ScatterConfig& scatter, const TileKey& key, const std::vector<ScatterInstance>& instances) {
    std::vector<CellEntity> out;
    out.reserve(instances.size());
    (void)key;
    for (const ScatterInstance& instance : instances) {
        const ScatterSpecies& species = scatter.species[instance.species];
        CellEntity entity;
        entity.key = scatterInstanceKey(scatter, instance);
        entity.desc.name = species.name;
        entity.desc.mesh = species.mesh;
        entity.desc.material = species.material;
        entity.desc.position = instance.position;
        entity.desc.scale = glm::vec3(instance.scale);
        glm::quat rotation = glm::angleAxis(instance.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        if (species.alignToSlope) {
            const glm::vec3 axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), instance.normal);
            const float axisLength = glm::length(axis);
            if (axisLength > 1.0e-5f) rotation = glm::angleAxis(std::asin(std::min(axisLength, 1.0f)), axis / axisLength) * rotation;
        }
        entity.desc.rotation = rotation;
        entity.tag = species.tag;
        entity.bytes = 192;
        out.push_back(std::move(entity));
    }
    return out;
}

std::vector<ScatterBatch> scatterBatches(const ScatterConfig& scatter, const TileKey& key, const std::vector<ScatterInstance>& instances, const TerrainConfig& terrain) {
    std::vector<ScatterBatch> out;
    const glm::vec3 origin = tileOrigin(terrain, key);
    for (uint32_t index = 0; index < scatter.species.size(); ++index) {
        const ScatterSpecies& species = scatter.species[index];
        if (!species.geometry || species.geometry->vertices.empty()) continue;
        const Mesh& source = *species.geometry;
        const size_t vertexCount = source.vertices.size() / kVertexFloats;
        ScatterBatch batch;
        batch.species = index;
        std::vector<float> vertices;
        std::vector<unsigned int> indices;
        for (const ScatterInstance& instance : instances) {
            if (instance.species != index) continue;
            glm::quat rotation = glm::angleAxis(instance.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            if (species.alignToSlope) {
                const glm::vec3 axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), instance.normal);
                const float axisLength = glm::length(axis);
                if (axisLength > 1.0e-5f) rotation = glm::angleAxis(std::asin(std::min(axisLength, 1.0f)), axis / axisLength) * rotation;
            }
            const glm::vec3 base = instance.position - origin;
            const unsigned int first = static_cast<unsigned int>(vertices.size() / kVertexFloats);
            vertices.reserve(vertices.size() + source.vertices.size());
            for (size_t v = 0; v < vertexCount; ++v) {
                const float* in = &source.vertices[v * kVertexFloats];
                const glm::vec3 position = base + rotation * (glm::vec3(in[0], in[1], in[2]) * instance.scale);
                const glm::vec3 normal = rotation * glm::vec3(in[3], in[4], in[5]);
                vertices.insert(vertices.end(), {position.x, position.y, position.z, normal.x, normal.y, normal.z, in[6], in[7]});
            }
            for (const unsigned int i : source.indices) indices.push_back(first + i);
            batch.positions.push_back(base);
            ++batch.instances;
        }
        if (batch.instances == 0) continue;
        batch.mesh = Mesh(std::move(vertices), std::move(indices));
        out.push_back(std::move(batch));
    }
    return out;
}
