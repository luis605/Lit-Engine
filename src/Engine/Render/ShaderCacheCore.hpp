#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace litgpu::cache {

inline constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ull;
inline constexpr uint64_t kFnvPrime = 1099511628211ull;
inline constexpr size_t kIncludeKeywordLength = 8;
inline constexpr size_t kMaxIncludeDepth = 32;
inline constexpr uint32_t kEntryMagic = 0x5653504Cu;
inline constexpr uint32_t kEntryFormatVersion = 2;
inline constexpr size_t kWordSize = 4;
inline constexpr const char* kShaderCacheDirectory = ".litcache/shaders";
inline constexpr const char* kEntryExtension = ".spv";
inline constexpr const char* kTemporaryTag = ".tmp";

using MacroList = std::vector<std::pair<std::string, std::string>>;
using IncludeReader = std::function<std::string(const std::string&)>;

struct KeyInputs {
    int shaderType = 0;
    int sourceLanguage = 0;
    bool combinedSamplers = false;
    std::string_view source;
    const MacroList* macros = nullptr;
    std::string_view compilerTag;
};

inline uint64_t fnv1a(std::string_view data, uint64_t hash = kFnvOffsetBasis) {
    for (unsigned char byte : data) hash = (hash ^ byte) * kFnvPrime;
    return hash;
}

inline void hashWithIncludes(std::string_view source, uint64_t& hash, std::vector<std::string>& seen, const IncludeReader& reader, size_t depth = 0) {
    hash = fnv1a(source, hash);
    if (depth >= kMaxIncludeDepth) return;
    size_t position = 0;
    while ((position = source.find("#include", position)) != std::string_view::npos) {
        position += kIncludeKeywordLength;
        const size_t open = source.find_first_of("\"<", position);
        const size_t close = open == std::string_view::npos ? open : source.find_first_of("\">", open + 1);
        if (close == std::string_view::npos) break;
        std::string includeName(source.substr(open + 1, close - open - 1));
        if (std::find(seen.begin(), seen.end(), includeName) != seen.end()) continue;
        seen.push_back(includeName);
        const std::string text = reader(includeName);
        hash = fnv1a(includeName, hash);
        if (!text.empty()) hashWithIncludes(text, hash, seen, reader, depth + 1);
    }
}

inline uint64_t computeKey(const KeyInputs& inputs, const IncludeReader& reader) {
    uint64_t hash = fnv1a(std::format("lit-spirv-{}|{}|{}|{}|{}|", kEntryFormatVersion, inputs.compilerTag, inputs.shaderType, inputs.sourceLanguage, inputs.combinedSamplers ? 1 : 0));
    if (inputs.macros) {
        for (const auto& [name, definition] : *inputs.macros) hash = fnv1a(std::format("{}={};", name, definition), hash);
    }
    std::vector<std::string> seen;
    hashWithIncludes(inputs.source, hash, seen, reader);
    return hash;
}

inline std::filesystem::path entryPath(const std::filesystem::path& directory, uint64_t key) {
    return directory / (std::format("{:016x}", key) + kEntryExtension);
}

inline std::string encodeEntry(uint64_t key, const void* payload, size_t size) {
    std::string bytes;
    bytes.resize(sizeof(uint32_t) * 2 + sizeof(uint64_t) * 3 + size);
    char* cursor = bytes.data();
    auto put = [&](const auto& value) {
        std::memcpy(cursor, &value, sizeof(value));
        cursor += sizeof(value);
    };
    put(kEntryMagic);
    put(kEntryFormatVersion);
    put(key);
    put(static_cast<uint64_t>(size));
    put(fnv1a(std::string_view(static_cast<const char*>(payload), size)));
    if (size) std::memcpy(cursor, payload, size);
    return bytes;
}

inline std::optional<std::string> decodeEntry(std::string_view bytes, uint64_t key) {
    constexpr size_t headerSize = sizeof(uint32_t) * 2 + sizeof(uint64_t) * 3;
    if (bytes.size() < headerSize) return std::nullopt;
    const char* cursor = bytes.data();
    auto take = [&](auto& value) {
        std::memcpy(&value, cursor, sizeof(value));
        cursor += sizeof(value);
    };
    uint32_t magic = 0, version = 0;
    uint64_t storedKey = 0, size = 0, checksum = 0;
    take(magic);
    take(version);
    take(storedKey);
    take(size);
    take(checksum);
    if (magic != kEntryMagic || version != kEntryFormatVersion || storedKey != key) return std::nullopt;
    if (size == 0 || size % kWordSize != 0 || size != bytes.size() - headerSize) return std::nullopt;
    const std::string_view payload(cursor, static_cast<size_t>(size));
    if (fnv1a(payload) != checksum) return std::nullopt;
    return std::string(payload);
}

inline std::optional<std::string> readEntry(const std::filesystem::path& path, uint64_t key) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::optional<std::string> payload = decodeEntry(bytes, key);
    if (!payload) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    return payload;
}

inline std::filesystem::path uniqueTemporaryPath(const std::filesystem::path& finalPath) {
    static std::atomic<uint64_t> counter{0};
    const size_t threadHash = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return finalPath.string() + std::format(".{:x}.{}{}", threadHash, counter.fetch_add(1), kTemporaryTag);
}

inline bool writeEntry(const std::filesystem::path& directory, uint64_t key, const void* payload, size_t size) {
    if (!payload || size == 0 || size % kWordSize != 0) return false;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path finalPath = entryPath(directory, key);
    const std::filesystem::path temporary = uniqueTemporaryPath(finalPath);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        const std::string bytes = encodeEntry(key, payload, size);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(temporary, finalPath, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

}
