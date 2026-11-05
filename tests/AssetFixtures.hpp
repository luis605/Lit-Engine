#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace assetfixtures {

inline constexpr float kPi = 3.14159265f;
inline constexpr float kBumpAmplitude = 0.08f;
inline constexpr float kBumpFrequency = 7.0f;

inline void writeBumpySphereObj(const std::string& path, int rings, int segments, float seedOffset) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream file(path, std::ios::trunc);
    char line[160];
    for (int ring = 0; ring <= rings; ++ring) {
        const float v = static_cast<float>(ring) / static_cast<float>(rings);
        const float polar = v * kPi;
        for (int segment = 0; segment <= segments; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(segments);
            const float azimuth = u * 2.0f * kPi;
            const float radius = 1.0f + kBumpAmplitude * std::sin(kBumpFrequency * azimuth + seedOffset) * std::cos(kBumpFrequency * polar);
            const float x = radius * std::sin(polar) * std::cos(azimuth);
            const float y = radius * std::cos(polar);
            const float z = radius * std::sin(polar) * std::sin(azimuth);
            std::snprintf(line, sizeof(line), "v %.6f %.6f %.6f\nvt %.6f %.6f\nvn %.6f %.6f %.6f\n", x, y, z, u, v, x, y, z);
            file << line;
        }
    }
    const int stride = segments + 1;
    for (int ring = 0; ring < rings; ++ring) {
        for (int segment = 0; segment < segments; ++segment) {
            const int a = ring * stride + segment + 1;
            const int b = a + stride;
            std::snprintf(line, sizeof(line), "f %d/%d/%d %d/%d/%d %d/%d/%d\nf %d/%d/%d %d/%d/%d %d/%d/%d\n", a, a, a, b, b, b, a + 1, a + 1, a + 1, a + 1, a + 1, a + 1, b, b, b, b + 1, b + 1, b + 1);
            file << line;
        }
    }
}

inline std::vector<std::string> writeObjSet(const std::string& directory, int count, int rings, int segments) {
    std::vector<std::string> paths;
    for (int index = 0; index < count; ++index) {
        const std::string path = directory + "/model_" + std::to_string(index) + ".obj";
        writeBumpySphereObj(path, rings, segments, static_cast<float>(index));
        paths.push_back(path);
    }
    return paths;
}

}
