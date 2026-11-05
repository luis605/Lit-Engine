#pragma once

#include "DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Shader.h"
#include "DiligentCore/Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "DiligentCore/Primitives/interface/DataBlob.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <thread>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Engine/Log/Log.hpp"
#include "ShaderCacheCore.hpp"

namespace litgpu {

using Diligent::RefCntAutoPtr;

using ShaderMacros = std::vector<std::pair<std::string, std::string>>;

inline constexpr size_t kMaxQueuedDiagnostics = 32;
inline constexpr size_t kErrorPrefixLength = 6;
inline constexpr int kUserSourceString = 1;

inline std::string readText(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return {};
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

inline Diligent::IShaderSourceInputStreamFactory* includeFactory() {
    static RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> factory = [] {
        RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> created;
        Diligent::GetEngineFactoryVk()->CreateDefaultShaderSourceStreamFactory("resources/shaders/include;resources/shaders", &created);
        return created;
    }();
    return factory;
}

struct ShaderDiagnostic {
    std::string source;
    std::string message;
};

inline std::mutex& diagnosticsMutex() {
    static std::mutex mutex;
    return mutex;
}

inline std::vector<ShaderDiagnostic>& diagnosticsQueue() {
    static std::vector<ShaderDiagnostic> queue;
    return queue;
}

inline void reportShaderError(std::string source, std::string message) {
    std::lock_guard lock(diagnosticsMutex());
    auto& queue = diagnosticsQueue();
    for (const ShaderDiagnostic& existing : queue) {
        if (existing.source == source && existing.message == message) return;
    }
    if (queue.size() >= kMaxQueuedDiagnostics) queue.erase(queue.begin());
    queue.push_back({std::move(source), std::move(message)});
}

inline std::vector<ShaderDiagnostic> takeShaderDiagnostics() {
    std::lock_guard lock(diagnosticsMutex());
    std::vector<ShaderDiagnostic> taken;
    taken.swap(diagnosticsQueue());
    return taken;
}

inline std::string& lastCompilerOutput() {
    thread_local std::string text;
    return text;
}

inline std::string& lastCompileErrors() {
    thread_local std::string text;
    return text;
}

inline std::string userLine(int line) { return std::format("\n#line {} {}\n", line, kUserSourceString); }

inline std::string injectUserCode(const std::string& templateSource, const std::string& code) {
    const std::string marker = "#pragma lit_user_code";
    const size_t markerPosition = templateSource.find(marker);
    if (markerPosition == std::string::npos) return templateSource;
    const auto line = std::count(templateSource.begin(), templateSource.begin() + static_cast<std::ptrdiff_t>(markerPosition), '\n') + 1;
    return templateSource.substr(0, markerPosition) + code + std::format("\n#line {} 0\n", line) + templateSource.substr(markerPosition + marker.size());
}

inline bool isReportableCompilerLine(const std::string& line) {
    return line.rfind("ERROR:", 0) == 0 && line.find("compilation errors") == std::string::npos && line.find("compilation terminated") == std::string::npos;
}

inline std::string compilerErrors(const std::string& output, const std::string& name, const std::string& userPath) {
    const std::string userTag = std::format("{}:", kUserSourceString);
    std::string errors;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (!isReportableCompilerLine(line)) continue;
        std::string text = line.substr(kErrorPrefixLength);
        text.erase(0, text.find_first_not_of(' '));
        if (!userPath.empty() && text.rfind(userTag, 0) == 0) {
            text = userPath + ":" + text.substr(userTag.size());
        } else if (text.rfind("0:", 0) == 0) {
            text = name + ":" + text.substr(2);
        }
        if (!errors.empty()) errors += '\n';
        errors += text;
    }
    return errors;
}

inline std::string readInclude(const std::string& name) {
    for (const char* directory : {"resources/shaders/include/", "resources/shaders/"}) {
        std::string text = readText(directory + name);
        if (!text.empty()) return text;
    }
    return {};
}

inline std::string compilerTag() { return std::format("diligent-{}", DILIGENT_API_VERSION); }

inline std::filesystem::path shaderCacheDirectory() {
    if (const char* override = std::getenv("LIT_SHADER_CACHE_DIR")) return override;
    return cache::kShaderCacheDirectory;
}

inline uint64_t sourceCacheKey(std::string_view source, Diligent::SHADER_TYPE type, Diligent::SHADER_SOURCE_LANGUAGE language, bool combinedSamplers, const ShaderMacros& macros) {
    static const std::string tag = compilerTag();
    cache::KeyInputs inputs;
    inputs.shaderType = static_cast<int>(type);
    inputs.sourceLanguage = static_cast<int>(language);
    inputs.combinedSamplers = combinedSamplers;
    inputs.source = source;
    inputs.macros = &macros;
    inputs.compilerTag = tag;
    return cache::computeKey(inputs, readInclude);
}

inline uint64_t shaderCacheKey(const Diligent::ShaderCreateInfo& createInfo, const ShaderMacros& macros) {
    return sourceCacheKey(createInfo.Source, createInfo.Desc.ShaderType, createInfo.SourceLanguage, createInfo.Desc.UseCombinedTextureSamplers, macros);
}

inline ShaderMacros macrosOf(const Diligent::ShaderCreateInfo& createInfo) {
    ShaderMacros macros;
    for (Diligent::Uint32 macroIndex = 0; macroIndex < createInfo.Macros.Count; ++macroIndex) {
        const Diligent::ShaderMacro& macro = createInfo.Macros[macroIndex];
        macros.emplace_back(macro.Name, macro.Definition ? macro.Definition : "");
    }
    return macros;
}

inline RefCntAutoPtr<Diligent::IShader> loadCachedShader(Diligent::IRenderDevice* device, const Diligent::ShaderCreateInfo& createInfo, uint64_t key) {
    RefCntAutoPtr<Diligent::IShader> shader;
    const std::optional<std::string> bytes = cache::readEntry(cache::entryPath(shaderCacheDirectory(), key), key);
    if (!bytes) return shader;
    Diligent::ShaderCreateInfo cached;
    cached.Desc = createInfo.Desc;
    cached.ByteCode = bytes->data();
    cached.ByteCodeSize = bytes->size();
    device->CreateShader(cached, &shader);
    if (!shader) {
        std::error_code ignored;
        std::filesystem::remove(cache::entryPath(shaderCacheDirectory(), key), ignored);
    }
    return shader;
}

inline void storeCachedShader(Diligent::IShader* shader, uint64_t key) {
    const void* bytecode = nullptr;
    Diligent::Uint64 size = 0;
    shader->GetBytecode(&bytecode, size);
    cache::writeEntry(shaderCacheDirectory(), key, bytecode, static_cast<size_t>(size));
}

inline RefCntAutoPtr<Diligent::IShader> createShader(Diligent::IRenderDevice* device, const Diligent::ShaderCreateInfo& createInfo) {
    RefCntAutoPtr<Diligent::IShader> shader;
    lastCompilerOutput().clear();
    auto compile = [&] {
        RefCntAutoPtr<Diligent::IDataBlob> output;
        device->CreateShader(createInfo, &shader, &output);
        if (!shader && output && output->GetSize() > 0) {
            lastCompilerOutput().assign(static_cast<const char*>(output->GetConstDataPtr()), output->GetSize());
            lastCompilerOutput().erase(lastCompilerOutput().find_last_not_of('\0') + 1);
        }
    };
    if (!createInfo.Source || createInfo.ByteCode || std::getenv("LIT_NO_SHADER_CACHE")) {
        compile();
        return shader;
    }
    const uint64_t key = shaderCacheKey(createInfo, macrosOf(createInfo));
    shader = loadCachedShader(device, createInfo, key);
    if (shader) return shader;
    compile();
    if (shader) storeCachedShader(shader, key);
    return shader;
}

inline std::string stripVersion(std::string source) {
    const size_t versionPosition = source.find("#version");
    if (versionPosition == std::string::npos) return source;
    const size_t lineEnd = source.find('\n', versionPosition);
    if (lineEnd != std::string::npos) source.erase(versionPosition, lineEnd - versionPosition);
    return source;
}

inline RefCntAutoPtr<Diligent::IShader> compileSource(Diligent::IRenderDevice* device, const std::string& name, const std::string& source, Diligent::SHADER_TYPE type, const ShaderMacros& macros = {}, const std::string& userPath = {}) {
    const std::string body = "#line 1 0\n" + stripVersion(source);
    std::vector<Diligent::ShaderMacro> macroList;
    macroList.reserve(macros.size());
    for (const auto& [key, value] : macros) macroList.push_back({key.c_str(), value.c_str()});
    Diligent::ShaderCreateInfo createInfo;
    createInfo.Source = body.c_str();
    createInfo.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    createInfo.pShaderSourceStreamFactory = includeFactory();
    createInfo.Desc.UseCombinedTextureSamplers = true;
    createInfo.Desc.ShaderType = type;
    createInfo.Desc.Name = name.c_str();
    createInfo.Macros = Diligent::ShaderMacroArray{macroList.data(), static_cast<Diligent::Uint32>(macroList.size())};
    RefCntAutoPtr<Diligent::IShader> shader = createShader(device, createInfo);
    if (shader) return shader;
    std::string errors = compilerErrors(lastCompilerOutput(), name, userPath);
    if (errors.empty()) errors = "failed to compile (see the log)";
    lastCompileErrors() = errors;
    Lit::Log::Error("Failed to compile shader '{}':\n{}", name, errors);
    reportShaderError(userPath.empty() ? name : userPath, std::move(errors));
    return shader;
}

inline RefCntAutoPtr<Diligent::IShader> compileFile(Diligent::IRenderDevice* device, const std::string& path, Diligent::SHADER_TYPE type, const ShaderMacros& macros = {}) {
    const std::string source = readText(path);
    if (source.empty()) {
        Lit::Log::Error("Failed to open shader file: {}", path);
        return {};
    }
    return compileSource(device, path, source, type, macros);
}

struct ShaderJob {
    std::string path;
    Diligent::SHADER_TYPE type = Diligent::SHADER_TYPE_UNKNOWN;
    ShaderMacros macros;
};

inline bool jobIsCached(const ShaderJob& job) {
    const std::string source = readText(job.path);
    if (source.empty()) return false;
    const std::string body = "#line 1 0\n" + stripVersion(source);
    const uint64_t key = sourceCacheKey(body, job.type, Diligent::SHADER_SOURCE_LANGUAGE_GLSL, true, job.macros);
    std::error_code error;
    return std::filesystem::exists(cache::entryPath(shaderCacheDirectory(), key), error);
}

inline bool compileJobs(Diligent::IRenderDevice* device, const std::vector<ShaderJob>& jobs, bool skipCached) {
    if (skipCached && std::getenv("LIT_NO_SHADER_CACHE")) return true;
    std::vector<const ShaderJob*> pending;
    for (const ShaderJob& job : jobs) {
        if (!skipCached || !jobIsCached(job)) pending.push_back(&job);
    }
    if (pending.empty()) return true;
    std::atomic<size_t> nextJob{0};
    std::atomic<bool> allCompiled{true};
    auto worker = [&] {
        for (size_t index = nextJob.fetch_add(1); index < pending.size(); index = nextJob.fetch_add(1)) {
            const ShaderJob& job = *pending[index];
            if (!compileFile(device, job.path, job.type, job.macros)) allCompiled = false;
        }
    };
    const size_t workerCount = std::min<size_t>(pending.size(), std::max(1u, std::thread::hardware_concurrency()));
    std::vector<std::thread> threads;
    for (size_t index = 1; index < workerCount; ++index) threads.emplace_back(worker);
    worker();
    for (std::thread& thread : threads) thread.join();
    return allCompiled;
}

}
