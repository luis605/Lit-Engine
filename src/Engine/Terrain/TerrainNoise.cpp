module;

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

module Engine.Terrain;

namespace {
uint32_t hashCell(uint64_t seed, int x, int z) {
    uint64_t h = seed * 0x9E3779B97F4A7C15ULL;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(x)) * 0xC2B2AE3D27D4EB4FULL;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(z)) * 0x165667B19E3779F9ULL;
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 32;
    h *= 0x94D049BB133111EBULL;
    h ^= h >> 29;
    return static_cast<uint32_t>(h);
}

float cellValue(uint64_t seed, int x, int z) { return static_cast<float>(hashCell(seed, x, z) >> 8) * (2.0f / 16777216.0f) - 1.0f; }

struct Noised {
    float value;
    float dx;
    float dz;
};

Noised noised(uint64_t seed, float x, float z) {
    const float fx = std::floor(x);
    const float fz = std::floor(z);
    const int ix = static_cast<int>(fx);
    const int iz = static_cast<int>(fz);
    const float u = x - fx;
    const float v = z - fz;
    const float su = u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f);
    const float sv = v * v * v * (v * (v * 6.0f - 15.0f) + 10.0f);
    const float du = 30.0f * u * u * (u - 1.0f) * (u - 1.0f);
    const float dv = 30.0f * v * v * (v - 1.0f) * (v - 1.0f);
    const float a = cellValue(seed, ix, iz);
    const float b = cellValue(seed, ix + 1, iz);
    const float c = cellValue(seed, ix, iz + 1);
    const float d = cellValue(seed, ix + 1, iz + 1);
    const float k1 = b - a;
    const float k2 = c - a;
    const float k4 = a - b - c + d;
    return {a + k1 * su + k2 * sv + k4 * su * sv, du * (k1 + k4 * sv), dv * (k2 + k4 * su)};
}

float smoothstep(float low, float high, float value) {
    if (high <= low) return value >= high ? 1.0f : 0.0f;
    const float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}

float terrainValueNoise(uint64_t seed, float x, float z) { return noised(seed, x, z).value; }

HeightFn makeNoiseHeight(const NoiseParams& params) {
    return [params](float wx, float wz) {
        float x = wx * params.frequency;
        float z = wz * params.frequency;
        if (params.warp > 0.0f) {
            const float wxn = noised(params.seed ^ 0x51ED270BULL, x * 0.6f + 11.3f, z * 0.6f + 4.7f).value;
            const float wzn = noised(params.seed ^ 0xA5A5A5A5ULL, x * 0.6f - 7.9f, z * 0.6f + 19.1f).value;
            x += params.warp * wxn;
            z += params.warp * wzn;
        }
        float sum = 0.0f;
        float norm = 0.0f;
        float amplitude = 1.0f;
        float derivativeX = 0.0f;
        float derivativeZ = 0.0f;
        float frequency = 1.0f;
        constexpr float kCos = 0.8f;
        constexpr float kSin = 0.6f;
        for (int octave = 0; octave < params.octaves; ++octave) {
            const Noised n = noised(params.seed + static_cast<uint64_t>(octave) * 0x9E37ULL, x * frequency, z * frequency);
            derivativeX += n.dx;
            derivativeZ += n.dz;
            const float ridge = 1.0f - std::abs(n.value);
            const float shaped = n.value + (ridge * ridge * 2.0f - 1.0f - n.value) * params.ridgeMix;
            const float damp = 1.0f / (1.0f + params.erosion * (derivativeX * derivativeX + derivativeZ * derivativeZ));
            sum += amplitude * shaped * damp;
            norm += amplitude;
            amplitude *= params.gain;
            frequency *= params.lacunarity;
            const float rx = x * kCos - z * kSin;
            z = x * kSin + z * kCos;
            x = rx;
        }
        float t = norm > 0.0f ? sum / norm : 0.0f;
        const float sign = t < 0.0f ? -1.0f : 1.0f;
        t = sign * std::pow(std::abs(t), params.flatten);
        return params.baseHeight + params.amplitude * t;
    };
}

float HeightImage::sample01(float u, float v) const {
    if (!valid()) return 0.0f;
    const float x = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(width - 1);
    const float z = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(depth - 1);
    const int i = std::min(static_cast<int>(x), width - 2);
    const int j = std::min(static_cast<int>(z), depth - 2);
    const float fx = x - static_cast<float>(i);
    const float fz = z - static_cast<float>(j);
    const auto at = [&](int a, int b) { return static_cast<float>(samples[static_cast<size_t>(b) * static_cast<size_t>(width) + static_cast<size_t>(a)]) / 65535.0f; };
    const float top = at(i, j) * (1.0f - fx) + at(i + 1, j) * fx;
    const float bottom = at(i, j + 1) * (1.0f - fx) + at(i + 1, j + 1) * fx;
    return top * (1.0f - fz) + bottom * fz;
}

std::optional<HeightImage> HeightImage::fromRaw16(const std::vector<uint8_t>& bytes, int width, int depth, bool bigEndian) {
    if (width < 2 || depth < 2) return std::nullopt;
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(depth);
    if (bytes.size() < count * 2) return std::nullopt;
    HeightImage image;
    image.width = width;
    image.depth = depth;
    image.samples.resize(count);
    for (size_t index = 0; index < count; ++index) {
        const uint16_t first = bytes[index * 2];
        const uint16_t second = bytes[index * 2 + 1];
        image.samples[index] = bigEndian ? static_cast<uint16_t>((first << 8) | second) : static_cast<uint16_t>((second << 8) | first);
    }
    return image;
}

std::optional<HeightImage> HeightImage::fromPgm(const std::vector<uint8_t>& bytes) {
    size_t position = 0;
    const auto token = [&]() -> std::string {
        while (position < bytes.size()) {
            if (bytes[position] == '#') {
                while (position < bytes.size() && bytes[position] != '\n') ++position;
            } else if (std::isspace(bytes[position])) {
                ++position;
            } else {
                break;
            }
        }
        std::string out;
        while (position < bytes.size() && !std::isspace(bytes[position])) out.push_back(static_cast<char>(bytes[position++]));
        return out;
    };
    if (token() != "P5") return std::nullopt;
    int width = 0;
    int depth = 0;
    int maxValue = 0;
    try {
        width = std::stoi(token());
        depth = std::stoi(token());
        maxValue = std::stoi(token());
    } catch (...) {
        return std::nullopt;
    }
    if (width < 2 || depth < 2 || maxValue <= 0 || maxValue > 65535) return std::nullopt;
    ++position;
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(depth);
    const bool wide = maxValue > 255;
    if (bytes.size() < position + count * (wide ? 2 : 1)) return std::nullopt;
    HeightImage image;
    image.width = width;
    image.depth = depth;
    image.samples.resize(count);
    for (size_t index = 0; index < count; ++index) {
        uint32_t value = wide ? (static_cast<uint32_t>(bytes[position + index * 2]) << 8) | bytes[position + index * 2 + 1] : bytes[position + index];
        value = std::min<uint32_t>(value, static_cast<uint32_t>(maxValue));
        image.samples[index] = static_cast<uint16_t>(value * 65535u / static_cast<uint32_t>(maxValue));
    }
    return image;
}

namespace {
std::optional<std::vector<uint8_t>> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
}

std::optional<HeightImage> HeightImage::loadRaw16(const std::string& path, int width, int depth, bool bigEndian) {
    const auto bytes = readFile(path);
    return bytes ? fromRaw16(*bytes, width, depth, bigEndian) : std::nullopt;
}

std::optional<HeightImage> HeightImage::loadPgm(const std::string& path) {
    const auto bytes = readFile(path);
    return bytes ? fromPgm(*bytes) : std::nullopt;
}

HeightFn makeImageHeight(std::shared_ptr<const HeightImage> image, const HeightImagePlacement& placement) {
    return [image = std::move(image), placement](float x, float z) {
        if (!image || !image->valid()) return placement.minHeight;
        const float extentX = static_cast<float>(image->width - 1) * placement.cellSize;
        const float extentZ = static_cast<float>(image->depth - 1) * placement.cellSize;
        const float s = image->sample01((x - placement.origin.x) / extentX, (z - placement.origin.y) / extentZ);
        return placement.minHeight + s * (placement.maxHeight - placement.minHeight);
    };
}

glm::vec4 splatWeights(float height, float slope, const SplatRules& rules) {
    const float rock = smoothstep(rules.rockMinSlope, rules.rockFullSlope, 1.0f - slope);
    const float snow = smoothstep(rules.snowMinHeight - rules.snowBlend, rules.snowMinHeight + rules.snowBlend, height) * (1.0f - rock * 0.7f);
    const float sand = (1.0f - smoothstep(rules.sandMaxHeight - rules.sandBlend, rules.sandMaxHeight + rules.sandBlend, height)) * (1.0f - rock);
    float grass = std::max(0.0f, 1.0f - rock - snow - sand);
    glm::vec4 weights(grass, rock, sand, snow);
    const float total = weights.x + weights.y + weights.z + weights.w;
    return total > 1.0e-6f ? weights / total : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
}

TerrainLayer dominantLayer(const glm::vec4& weights) {
    int best = 0;
    for (int index = 1; index < 4; ++index) {
        if (weights[index] > weights[best]) best = index;
    }
    return static_cast<TerrainLayer>(best);
}

float terrainHeight(const TerrainConfig& config, float x, float z) { return config.height ? config.height(x, z) : 0.0f; }

glm::vec3 terrainNormal(const TerrainConfig& config, float x, float z, float step) {
    const float left = terrainHeight(config, x - step, z);
    const float right = terrainHeight(config, x + step, z);
    const float down = terrainHeight(config, x, z - step);
    const float up = terrainHeight(config, x, z + step);
    return glm::normalize(glm::vec3(left - right, 2.0f * step, down - up));
}
