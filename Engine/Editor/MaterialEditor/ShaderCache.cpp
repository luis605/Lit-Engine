/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include "ShaderCache.hpp"
#include "MaterialLayer.hpp"
#include "DynamicRule.hpp"
#include "Mask.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

static std::uint64_t fnv1a64(const void* data, size_t len, std::uint64_t seed = 1469598103934665603ull) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(data);
    std::uint64_t hash = seed;
    const std::uint64_t prime = 1099511628211ull;
    for (size_t i = 0; i < len; ++i) {
        hash ^= (std::uint64_t)p[i];
        hash *= prime;
    }
    return hash;
}

static std::string toHex64(std::uint64_t v) {
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << v;
    return ss.str();
}

static json SerializeMaterialToJson(const LivingMaterial& material) {
    json j;
    j["name"] = material.m_Name;
    j["layers"] = json::array();

    for (const auto& L : material.m_Layers) {
        json jl;
        jl["name"] = L.m_Name;
        jl["isEnabled"] = L.m_IsEnabled;
        jl["baseMaterialID"] = L.m_BaseMaterialID;

        json p = json::object();
        for (const auto& [k, v] : L.m_ParameterOverrides) {
            p[k] = { v.x, v.y, v.z, v.w };
        }
        jl["parameterOverrides"] = p;

        jl["mask"] = {
            { "texture", L.m_Mask.pathToMaskTexture.empty() ? "" : L.m_Mask.pathToMaskTexture.string() }
        };

        json rules = json::array();
        for (const auto& r : L.m_Rules) {
            json jr;
            jr["name"] = r.name;
            jr["op"] = static_cast<int>(r.op);
            jr["leftOperand"] = static_cast<int>(r.leftOperand);
            jr["rightOperand"] = static_cast<int>(r.rightOperand);
            switch (r.rightOperand) {
                case DynamicRuleRightOperand::FloatingPoint: jr["valueFloat"] = r.rightValueFloat; break;
                case DynamicRuleRightOperand::Integer: jr["valueInt"] = r.rightValueInt; break;
                case DynamicRuleRightOperand::Boolean: jr["valueBool"] = r.rightValueBool; break;
                case DynamicRuleRightOperand::String: jr["valueString"] = r.rightValueString; break;
                case DynamicRuleRightOperand::Entity: jr["entityId"] = r.rightValueEntity; break;
            }
            rules.push_back(std::move(jr));
        }
        jl["rules"] = rules;

        j["layers"].push_back(std::move(jl));
    }
    return j;
}

static fs::path MaterialBaseFolder(const fs::path& materialJsonPath, const std::string& materialName) {
    if (!materialJsonPath.empty()) {
        fs::path parent = materialJsonPath.parent_path();
        std::string stem = materialJsonPath.stem().string();
        return parent / stem;
    }
    return fs::path("project/Materials") / materialName;
}

ShaderCacheResult CacheCompiledShader(const fs::path& materialJsonPath,
                                      const LivingMaterial& material,
                                      const std::string& shaderSource,
                                      const std::string& codegenVersion) {
    ShaderCacheResult res;

    json j = SerializeMaterialToJson(material);
    j["codegenVersion"] = codegenVersion;
    std::string payload = j.dump();

    std::uint64_t h = fnv1a64(payload.data(), payload.size());
    res.hashHex = toHex64(h);

    fs::path baseFolder = MaterialBaseFolder(materialJsonPath, material.m_Name);
    fs::path compiledFolder = baseFolder / "compiled";
    std::error_code ec;
    fs::create_directories(compiledFolder, ec);

    fs::path outPath = compiledFolder / (res.hashHex + ".bin");
    std::ofstream f(outPath, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!f.is_open()) {
        res.ok = false;
        res.error = "Failed to open output for compiled shader: " + outPath.string();
        return res;
    }
    f.write(shaderSource.data(), (std::streamsize)shaderSource.size());
    f.close();

    res.ok = true;
    res.cachedPath = outPath;
    return res;
}