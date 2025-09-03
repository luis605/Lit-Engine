/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include "ShaderGenerator.hpp"
#include "MaterialLibrary.hpp"
#include "DynamicRule.hpp"
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <set>
#include <functional>
#include <fstream>

static std::string SanitizeIdentifier(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_')
            out.push_back(c);
        else
            out.push_back('_');
    }
    if (!out.empty() && (out[0] >= '0' && out[0] <= '9')) {
        out = "_" + out;
    }
    return out;
}

static std::string ToLower(const std::string& s) {
    std::string t; t.resize(s.size());
    std::transform(s.begin(), s.end(), t.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return t;
}

static bool IEquals(const std::string& a, const std::string& b) {
    return ToLower(a) == ToLower(b);
}

static bool IsTintKey(const std::string& key) {
    std::string k = ToLower(key);
    return (k == "tint" || k == "colortint" || k == "color_tint");
}

static bool IsColorKey(const std::string& key) {
    std::string k = ToLower(key);
    return (k == "color" || k == "albedo" || k == "basecolor" || k == "base_color" || k == "diffuse" || k == "colour" || k == "basecolour");
}

static bool IsRoughnessKey(const std::string& key) {
    return ToLower(key) == "roughness";
}

static bool IsMetalnessKey(const std::string& key) {
    std::string k = ToLower(key);
    return (k == "metalness" || k == "metallic" || k == "metalness_factor");
}

static bool IsAOKey(const std::string& key) {
    std::string k = ToLower(key);
    return (k == "ao" || k == "ambientocclusion" || k == "ambient_occlusion");
}

static bool IsAlphaKey(const std::string& key) {
    std::string k = ToLower(key);
    return (k == "alpha" || k == "opacity");
}

static std::string EnvUniformName(DynamicRuleLeftOperand left) {
    switch (left) {
        case DynamicRuleLeftOperand::Humidity: return "u_env_humidity";
        case DynamicRuleLeftOperand::Temperature: return "u_env_temperature";
        case DynamicRuleLeftOperand::WindSpeed: return "u_env_windSpeed";
        case DynamicRuleLeftOperand::RainIntensity: return "u_env_rainIntensity";
        case DynamicRuleLeftOperand::Age: return "u_env_age";
        case DynamicRuleLeftOperand::Exposure: return "u_env_exposure";
        case DynamicRuleLeftOperand::Wetness: return "u_env_wetness";
        case DynamicRuleLeftOperand::Slope: return "u_env_slope";
        case DynamicRuleLeftOperand::ProximityToObject: return "u_env_proximity";
        default: return "u_env_param";
    }
}

static const char* OpToGLSLSymbol(DynamicRuleOperator op) {
    switch (op) {
        case DynamicRuleOperator::Equal: return "==";
        case DynamicRuleOperator::NotEqual: return "!=";
        case DynamicRuleOperator::GreaterThan: return ">";
        case DynamicRuleOperator::LessThan: return "<";
        case DynamicRuleOperator::GreaterThanOrEqual: return ">=";
        case DynamicRuleOperator::LessThanOrEqual: return "<=";
        default: return ">";
    }
}

static void Append(std::stringstream& ss, const std::string& line) { ss << line; }
static void AppendLn(std::stringstream& ss, const std::string& line) { ss << line << "\n"; }

static std::string FloatLiteral(float v) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed, std::ios::floatfield);
    oss.precision(6);
    oss << v;
    return oss.str();
}

static std::string Vec3Literal(float x, float y, float z) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed, std::ios::floatfield);
    oss.precision(6);
    oss << "vec3(" << x << "," << y << "," << z << ")";
    return oss.str();
}

static std::string Vec4Literal(float x, float y, float z, float w) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed, std::ios::floatfield);
    oss.precision(6);
    oss << "vec4(" << x << "," << y << "," << z << "," << w << ")";
    return oss.str();
}

static const Vector4* FindParamCI(const std::map<std::string, Vector4>& params, const std::function<bool(const std::string&)>& pred) {
    for (const auto& [k, v] : params) {
        if (pred(k)) return &v;
    }
    return nullptr;
}

static const Vector4* FindParamCI(const std::map<std::string, Vector4>& params, const std::string& exactLowerKey) {
    for (const auto& [k, v] : params) {
        if (ToLower(k) == exactLowerKey) return &v;
    }
    return nullptr;
}

std::string GenerateFragmentShader(const LivingMaterial& mat, ShaderGenReport* outReport) {
    ShaderGenReport report;
    report.codegenVersion = "2.0.0-layered";

    std::vector<int> activeIdx;
    activeIdx.reserve(mat.m_Layers.size());
    for (int i = 0; i < (int)mat.m_Layers.size(); ++i) {
        if (mat.m_Layers[i].m_IsEnabled) activeIdx.emplace_back(i);
    }

    if (activeIdx.empty()) {
        report.errors.emplace_back("No enabled layers.");
        if (outReport) *outReport = report;
        return std::string();
    }
    report.numLayers = (int)activeIdx.size();

    std::stringstream uniforms, functions;

    std::set<std::string> envUniforms;

    auto ParamUniformName = [](int layerIdx, const std::string& param) {
        return "u_layer" + std::to_string(layerIdx) + "_param_" + SanitizeIdentifier(param);
    };

    AppendLn(uniforms, "#define LAYERED_MATERIAL 1");

    for (int order = 0; order < (int)activeIdx.size(); ++order) {
        int layerIndex = activeIdx[order];
        const MaterialLayer& L = mat.m_Layers[layerIndex];

        BaseMaterialDefinition* baseDefPtr = nullptr;
        bool hasBase = false;
        try {
            if (!L.m_BaseMaterialID.empty()) {
                baseDefPtr = &MaterialLibrary::GetInstance().getDefinition(L.m_BaseMaterialID);
                hasBase = true;
            }
        } catch (...) {
        }

        bool hasAlbedoMap = hasBase && !baseDefPtr->m_AlbedoMapPath.empty();
        bool hasNormalMap = hasBase && !baseDefPtr->m_NormalMapPath.empty();
        bool hasRoughMap  = hasBase && !baseDefPtr->m_RoughnessMapPath.empty();
        bool hasMetalMap  = hasBase && !baseDefPtr->m_MetalnessMapPath.empty();
        bool hasAOMap     = hasBase && !baseDefPtr->m_AOMapPath.empty();
        bool hasMask      = !L.m_Mask.pathToMaskTexture.empty();

        if (hasAlbedoMap) {
            AppendLn(uniforms, "uniform sampler2D u_layer" + std::to_string(layerIndex) + "_albedoMap;");
            report.numSamplers++;
        }
        if (hasNormalMap) {
            AppendLn(uniforms, "uniform sampler2D u_layer" + std::to_string(layerIndex) + "_normalMap;");
            report.numSamplers++;
        }
        if (hasRoughMap) {
            AppendLn(uniforms, "uniform sampler2D u_layer" + std::to_string(layerIndex) + "_roughnessMap;");
            report.numSamplers++;
        }
        if (hasMetalMap) {
            AppendLn(uniforms, "uniform sampler2D u_layer" + std::to_string(layerIndex) + "_metalnessMap;");
            report.numSamplers++;
        }
        if (hasAOMap) {
            AppendLn(uniforms, "uniform sampler2D u_layer" + std::to_string(layerIndex) + "_aoMap;");
            report.numSamplers++;
        }
        if (hasMask) {
            AppendLn(uniforms, "uniform sampler2D u_layer" + std::to_string(layerIndex) + "_mask;");
            report.numSamplers++;
        }

        for (const auto& [pname, pval] : L.m_ParameterOverrides) {
            if (IsTintKey(pname)) continue;
            AppendLn(uniforms, "uniform vec4 " + ParamUniformName(layerIndex, pname) + ";");
        }
    }

    for (int order = 0; order < (int)activeIdx.size(); ++order) {
        int layerIndex = activeIdx[order];
        const MaterialLayer& L = mat.m_Layers[layerIndex];
        for (const auto& r : L.m_Rules) {
            if (RuleRequiresBake(r.leftOperand)) continue;
            envUniforms.insert(EnvUniformName(r.leftOperand));
        }
    }
    for (const auto& env : envUniforms) {
        AppendLn(uniforms, "uniform float " + env + ";");
        report.envUniforms.push_back(env);
    }

    auto EmitAlbedo = [&](std::stringstream& ss, int layerIndex, const MaterialLayer& L,
                          const BaseMaterialDefinition* baseDef, bool hasAlbedoMap) {
        const Vector4* ovColor = FindParamCI(L.m_ParameterOverrides, IsColorKey);
        const Vector4* dfColor = baseDef ? FindParamCI(baseDef->m_DefaultParameters, IsColorKey) : nullptr;
        const Vector4* ovTint  = FindParamCI(L.m_ParameterOverrides, IsTintKey);
        const Vector4* dfTint  = baseDef ? FindParamCI(baseDef->m_DefaultParameters, IsTintKey) : nullptr;

        if (hasAlbedoMap) {
            ss << "    vec4 aTex = texture(u_layer" << layerIndex << "_albedoMap, tc);\n";
            ss << "    vec3 la = aTex.rgb;\n";
            ss << "    float lAlpha = aTex.a;\n";
        } else {
            if (ovColor) {
                std::string uName = ParamUniformName(layerIndex, "color");
                for (const auto& [k, v] : L.m_ParameterOverrides) {
                    if (IsColorKey(k)) { uName = ParamUniformName(layerIndex, k); break; }
                }
                ss << "    vec4 aTex = " << uName << ";\n";
            } else if (dfColor) {
                ss << "    vec4 aTex = " << Vec4Literal(dfColor->x, dfColor->y, dfColor->z, (dfColor->w == 0.0f ? 1.0f : dfColor->w)) << ";\n";
            } else {
                ss << "    vec4 aTex = vec4(1.0);\n";
            }
            ss << "    vec3 la = aTex.rgb;\n";
            ss << "    float lAlpha = aTex.a;\n";
        }

        if (ovTint) {
            ss << "    la *= " << Vec3Literal(ovTint->x, ovTint->y, ovTint->z) << ";\n";
        } else if (dfTint) {
            ss << "    la *= " << Vec3Literal(dfTint->x, dfTint->y, dfTint->z) << ";\n";
        }
    };

    auto EmitRoughness = [&](std::stringstream& ss, int layerIndex, const MaterialLayer& L,
                             const BaseMaterialDefinition* baseDef, bool hasRoughMap) {
        if (hasRoughMap) {
            ss << "    float lr = texture(u_layer" << layerIndex << "_roughnessMap, tc).r;\n";
        } else {
            const Vector4* ovR = FindParamCI(L.m_ParameterOverrides, IsRoughnessKey);
            const Vector4* dfR = baseDef ? FindParamCI(baseDef->m_DefaultParameters, IsRoughnessKey) : nullptr;
            if (ovR) {
                std::string uName = ParamUniformName(layerIndex, "roughness");
                for (const auto& [k, v] : L.m_ParameterOverrides) {
                    if (IsRoughnessKey(k)) { uName = ParamUniformName(layerIndex, k); break; }
                }
                ss << "    float lr = " << uName << ".x;\n";
            } else {
                float r = dfR ? dfR->x : 1.0f;
                ss << "    float lr = " << FloatLiteral(r) << ";\n";
            }
        }
        ss << "    lr = clamp(lr, 0.0, 1.0);\n";
    };

    auto EmitMetalness = [&](std::stringstream& ss, int layerIndex, const MaterialLayer& L,
                             const BaseMaterialDefinition* baseDef, bool hasMetalMap) {
        if (hasMetalMap) {
            ss << "    float lm = texture(u_layer" << layerIndex << "_metalnessMap, tc).r;\n";
        } else {
            const Vector4* ovM = FindParamCI(L.m_ParameterOverrides, IsMetalnessKey);
            const Vector4* dfM = baseDef ? FindParamCI(baseDef->m_DefaultParameters, IsMetalnessKey) : nullptr;
            if (ovM) {
                std::string uName = ParamUniformName(layerIndex, "metalness");
                for (const auto& [k, v] : L.m_ParameterOverrides) {
                    if (IsMetalnessKey(k)) { uName = ParamUniformName(layerIndex, k); break; }
                }
                ss << "    float lm = " << uName << ".x;\n";
            } else {
                float m = dfM ? dfM->x : 0.0f;
                ss << "    float lm = " << FloatLiteral(m) << ";\n";
            }
        }
        ss << "    lm = clamp(lm, 0.0, 1.0);\n";
    };

    auto EmitAO = [&](std::stringstream& ss, int layerIndex, const MaterialLayer& L,
                      const BaseMaterialDefinition* baseDef, bool hasAOMap) {
        if (hasAOMap) {
            ss << "    float lAO = texture(u_layer" << layerIndex << "_aoMap, tc).r;\n";
        } else {
            const Vector4* ovAO = FindParamCI(L.m_ParameterOverrides, IsAOKey);
            const Vector4* dfAO = baseDef ? FindParamCI(baseDef->m_DefaultParameters, IsAOKey) : nullptr;
            if (ovAO) {
                std::string uName = ParamUniformName(layerIndex, "ao");
                for (const auto& [k, v] : L.m_ParameterOverrides) {
                    if (IsAOKey(k)) { uName = ParamUniformName(layerIndex, k); break; }
                }
                ss << "    float lAO = " << uName << ".x;\n";
            } else {
                float a = dfAO ? dfAO->x : 1.0f;
                ss << "    float lAO = " << FloatLiteral(a) << ";\n";
            }
        }
        ss << "    lAO = clamp(lAO, 0.0, 1.0);\n";
    };

    auto EmitAlphaIfOverride = [&](std::stringstream& ss, int layerIndex, const MaterialLayer& L,
                                   const BaseMaterialDefinition* baseDef) {
        const Vector4* ovA = FindParamCI(L.m_ParameterOverrides, IsAlphaKey);
        const Vector4* dfA = baseDef ? FindParamCI(baseDef->m_DefaultParameters, IsAlphaKey) : nullptr;
        if (ovA) {
            std::string uName = ParamUniformName(layerIndex, "alpha");
            for (const auto& [k, v] : L.m_ParameterOverrides) {
                if (IsAlphaKey(k)) { uName = ParamUniformName(layerIndex, k); break; }
            }
            ss << "    lAlpha = " << uName << ".x;\n";
        } else if (dfA) {
            ss << "    lAlpha = " << FloatLiteral(dfA->x) << ";\n";
        }
        ss << "    lAlpha = clamp(lAlpha, 0.0, 1.0);\n";
    };

    auto EmitNormal = [&](std::stringstream& ss, int layerIndex, bool hasNormalMap) {
        if (hasNormalMap) {
            ss << "    vec2 nxy = texture(u_layer" << layerIndex << "_normalMap, tc).rg * 2.0 - 1.0;\n";
            ss << "    vec3 nm = vec3(nxy, 1.0);\n";
            ss << "    float xyLenSq = dot(nm.xy, nm.xy);\n";
            ss << "    nm.z = invSqrtFast(max(1.0 - xyLenSq, EPSILON));\n";
            ss << "    vec3 ln = normalize(TBN * nm);\n";
        } else {
            ss << "    vec3 ln = normalize(geomN);\n";
        }
    };

    AppendLn(functions, "void EvaluateLayeredMaterial(const vec2 tc, const vec3 P, const vec3 geomN, inout vec3 N, inout vec3 albedo, inout float roughness, inout float metalness, inout float ao, inout float alpha) {");
    AppendLn(functions, "  const vec3 dp1 = dFdx(P);");
    AppendLn(functions, "  const vec3 dp2 = dFdy(P);");
    AppendLn(functions, "  const vec2 duv1 = dFdx(tc);");
    AppendLn(functions, "  const vec2 duv2 = dFdy(tc);");
    AppendLn(functions, "  const vec3 dPds = dp1 * duv2.t - dp2 * duv1.t;");
    AppendLn(functions, "  const vec3 dPdt = dp2 * duv1.s - dp1 * duv2.s;");
    AppendLn(functions, "  const vec3 T = normalize(dPds - geomN * dot(geomN, dPds));");
    AppendLn(functions, "  const vec3 B = normalize(cross(geomN, T));");
    AppendLn(functions, "  const mat3 TBN = mat3(T, B, geomN);");

    // Substrate
    {
        int idx = activeIdx[0];
        const MaterialLayer& L0 = mat.m_Layers[idx];

        BaseMaterialDefinition* base0 = nullptr;
        bool hasBase0 = false;
        try {
            if (!L0.m_BaseMaterialID.empty()) {
                base0 = &MaterialLibrary::GetInstance().getDefinition(L0.m_BaseMaterialID);
                hasBase0 = true;
            }
        } catch (...) {
        }

        bool albedoMap0 = hasBase0 && !base0->m_AlbedoMapPath.empty();
        bool normalMap0 = hasBase0 && !base0->m_NormalMapPath.empty();
        bool roughMap0  = hasBase0 && !base0->m_RoughnessMapPath.empty();
        bool metalMap0  = hasBase0 && !base0->m_MetalnessMapPath.empty();
        bool aoMap0     = hasBase0 && !base0->m_AOMapPath.empty();

        AppendLn(functions, "  {");
        EmitAlbedo(functions, idx, L0, base0, albedoMap0);
        EmitRoughness(functions, idx, L0, base0, roughMap0);
        EmitMetalness(functions, idx, L0, base0, metalMap0);
        EmitAO(functions, idx, L0, base0, aoMap0);
        EmitAlphaIfOverride(functions, idx, L0, base0);
        EmitNormal(functions, idx, normalMap0);
        AppendLn(functions, "    albedo = saturate(la);");
        AppendLn(functions, "    roughness = max(lr, MIN_ROUGHNESS);");
        AppendLn(functions, "    metalness = saturate(vec3(lm)).x;");
        AppendLn(functions, "    ao = saturate(vec3(lAO)).x;");
        AppendLn(functions, "    alpha = saturate(vec3(lAlpha)).x;");
        AppendLn(functions, "    N = normalize(ln);");
        AppendLn(functions, "  }");
    }

    // Dynamic layers
    for (int order = 1; order < (int)activeIdx.size(); ++order) {
        int idx = activeIdx[order];
        const MaterialLayer& L = mat.m_Layers[idx];

        BaseMaterialDefinition* base = nullptr;
        bool hasBase = false;
        try {
            if (!L.m_BaseMaterialID.empty()) {
                base = &MaterialLibrary::GetInstance().getDefinition(L.m_BaseMaterialID);
                hasBase = true;
            }
        } catch (...) {}

        bool hasAlbedoMap = hasBase && !base->m_AlbedoMapPath.empty();
        bool hasNormalMap = hasBase && !base->m_NormalMapPath.empty();
        bool hasRoughMap  = hasBase && !base->m_RoughnessMapPath.empty();
        bool hasMetalMap  = hasBase && !base->m_MetalnessMapPath.empty();
        bool hasAOMap     = hasBase && !base->m_AOMapPath.empty();
        bool hasMask      = !L.m_Mask.pathToMaskTexture.empty();

        AppendLn(functions, "  {");
        AppendLn(functions, "    float m = 1.0;");
        if (hasMask) {
            AppendLn(functions, "    m *= texture(u_layer" + std::to_string(idx) + "_mask, tc).r;");
        }

        if (!L.m_Rules.empty()) {
            bool hasExpr = false;
            std::stringstream rule;
            for (const auto& r : L.m_Rules) {
                if (RuleRequiresBake(r.leftOperand)) {
                    report.warnings.push_back("Layer " + std::to_string(idx) + " has rules that require baking. Provide a baked mask for slope/proximity.");
                    continue;
                }
                std::string env = EnvUniformName(r.leftOperand);
                envUniforms.insert(env);

                std::string rhs = "0.0";
                switch (r.rightOperand) {
                    case DynamicRuleRightOperand::FloatingPoint: rhs = FloatLiteral(r.rightValueFloat); break;
                    case DynamicRuleRightOperand::Integer: rhs = FloatLiteral((float)r.rightValueInt); break;
                    case DynamicRuleRightOperand::Boolean: rhs = r.rightValueBool ? "1.0" : "0.0"; break;
                    default: rhs = "0.0"; break;
                }
                std::string expr = "(" + env + " " + OpToGLSLSymbol(r.op) + " " + rhs + ")";
                if (hasExpr) rule << " && ";
                rule << expr;
                hasExpr = true;
            }
            if (hasExpr) {
                AppendLn(functions, "    m *= (" + rule.str() + ") ? 1.0 : 0.0;");
            }
        }

        EmitAlbedo(functions, idx, L, base, hasAlbedoMap);
        EmitRoughness(functions, idx, L, base, hasRoughMap);
        EmitMetalness(functions, idx, L, base, hasMetalMap);
        EmitAO(functions, idx, L, base, hasAOMap);
        EmitAlphaIfOverride(functions, idx, L, base);
        EmitNormal(functions, idx, hasNormalMap);

        AppendLn(functions, "    albedo    = mix(albedo, la, m);");
        AppendLn(functions, "    roughness = mix(roughness, max(lr, MIN_ROUGHNESS), m);");
        AppendLn(functions, "    metalness = mix(metalness, lm, m);");
        AppendLn(functions, "    ao        = mix(ao, lAO, m);");
        AppendLn(functions, "    alpha     = mix(alpha, lAlpha, m);");
        AppendLn(functions, "    N         = normalize(mix(N, ln, m));");
        AppendLn(functions, "  }");
    }

    AppendLn(functions, "  albedo    = saturate(albedo);");
    AppendLn(functions, "  roughness = clamp(roughness, MIN_ROUGHNESS, 1.0);");
    AppendLn(functions, "  metalness = saturate(vec3(metalness)).x;");
    AppendLn(functions, "  ao        = saturate(vec3(ao)).x;");
    AppendLn(functions, "  alpha     = saturate(vec3(alpha)).x;");
    AppendLn(functions, "  N = normalize(N);");
    AppendLn(functions, "}");

    // Load the sahder template and inject it to the right places
    std::ifstream file("Engine/Lighting/shaders/lighting_fragment.glsl");
    if (!file.is_open()) {
        TraceLog(LOG_ERROR, "Failed to open default fragment shader during shader generation.");
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string shaderCode = buffer.str();
    file.close();

    const std::string markerUniforms  = "// [INJECT UNIFORMS HERE]";
    const std::string markerFunctions = "// [ INJECT FUNCTIONS HERE ]";

    size_t posU = shaderCode.find(markerUniforms);
    if (posU != std::string::npos) {
        shaderCode.replace(posU, markerUniforms.length(), uniforms.str());
    } else {
        report.warnings.push_back("Uniform injection marker not found.");
    }

    size_t posF = shaderCode.find(markerFunctions);
    if (posF != std::string::npos) {
        shaderCode.replace(posF, markerFunctions.length(), functions.str());
    } else {
        report.warnings.push_back("Function injection marker not found.");
    }

    if (outReport) *outReport = report;
    return shaderCode;
}