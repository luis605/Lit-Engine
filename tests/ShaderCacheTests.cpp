#include "TestSupport.hpp"
#include "Engine/Render/ShaderCacheCore.hpp"

#include <map>
#include <thread>

namespace {

namespace fs = std::filesystem;
using namespace litgpu::cache;

constexpr int kThreadCount = 8;
constexpr size_t kPayloadWords = 64;
constexpr size_t kCorruptOffset = 40;

struct Sandbox {
    fs::path root;
    Sandbox() {
        root = testsupport::tempFilePath("litshadercache_", "dir");
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Sandbox() { fs::remove_all(root); }
};

std::string makePayload(uint32_t seed) {
    std::string payload(kPayloadWords * kWordSize, '\0');
    for (size_t index = 0; index < payload.size(); ++index) payload[index] = static_cast<char>((index * 31 + seed) & 0xFF);
    return payload;
}

uint64_t keyFor(const std::string& source, const MacroList& macros, const std::map<std::string, std::string>& includes, std::string_view tag = "tag-a", int type = 1) {
    KeyInputs inputs;
    inputs.shaderType = type;
    inputs.source = source;
    inputs.macros = &macros;
    inputs.compilerTag = tag;
    return computeKey(inputs, [&](const std::string& name) {
        const auto found = includes.find(name);
        return found == includes.end() ? std::string() : found->second;
    });
}

void testKeyInvalidation() {
    const std::string source = "#include \"common.glsl\"\nvoid main() {}\n";
    const std::map<std::string, std::string> includes = {{"common.glsl", "#include \"nested.glsl\"\nfloat a;\n"}, {"nested.glsl", "float b;\n"}};
    const uint64_t base = keyFor(source, {}, includes);
    CHECK(base == keyFor(source, {}, includes));
    CHECK(base != keyFor(source + " ", {}, includes));
    CHECK(base != keyFor(source, {{"FLAG", "1"}}, includes));
    CHECK(keyFor(source, {{"FLAG", "1"}}, includes) != keyFor(source, {{"FLAG", "2"}}, includes));
    CHECK(base != keyFor(source, {}, includes, "tag-b"));
    CHECK(base != keyFor(source, {}, includes, "tag-a", 2));
    auto changedInclude = includes;
    changedInclude["common.glsl"] += "float c;\n";
    CHECK(base != keyFor(source, {}, changedInclude));
    auto changedNested = includes;
    changedNested["nested.glsl"] = "float d;\n";
    CHECK(base != keyFor(source, {}, changedNested));
    const std::map<std::string, std::string> cyclic = {{"common.glsl", "#include \"common.glsl\"\n"}};
    CHECK(keyFor(source, {}, cyclic) == keyFor(source, {}, cyclic));
}

void testRoundTrip() {
    Sandbox sandbox;
    const std::string payload = makePayload(1);
    CHECK(writeEntry(sandbox.root, 0x1234, payload.data(), payload.size()));
    const auto loaded = readEntry(entryPath(sandbox.root, 0x1234), 0x1234);
    CHECK(loaded.has_value());
    CHECK(loaded && *loaded == payload);
    CHECK(!readEntry(entryPath(sandbox.root, 0x9999), 0x9999).has_value());
    CHECK(!writeEntry(sandbox.root, 0x5, payload.data(), 3));
    CHECK(!writeEntry(sandbox.root, 0x5, nullptr, 0));
    const std::string replacement = makePayload(2);
    CHECK(writeEntry(sandbox.root, 0x1234, replacement.data(), replacement.size()));
    CHECK(readEntry(entryPath(sandbox.root, 0x1234), 0x1234) == replacement);
}

void rewriteFile(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string slurp(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

void testCorruptionIsRejectedAndRemoved() {
    Sandbox sandbox;
    const std::string payload = makePayload(3);
    const uint64_t key = 0xABCD;
    const fs::path path = entryPath(sandbox.root, key);
    writeEntry(sandbox.root, key, payload.data(), payload.size());
    const std::string good = slurp(path);

    std::string flipped = good;
    flipped[kCorruptOffset] ^= 0x5A;
    rewriteFile(path, flipped);
    CHECK(!readEntry(path, key).has_value());
    CHECK(!fs::exists(path));

    rewriteFile(path, good.substr(0, good.size() - 5));
    CHECK(!readEntry(path, key).has_value());

    rewriteFile(path, good.substr(0, 7));
    CHECK(!readEntry(path, key).has_value());

    rewriteFile(path, std::string());
    CHECK(!readEntry(path, key).has_value());

    rewriteFile(path, good);
    CHECK(!readEntry(path, key + 1).has_value());

    std::string oldVersion = good;
    oldVersion[sizeof(uint32_t)] ^= 1;
    rewriteFile(path, oldVersion);
    CHECK(!readEntry(path, key).has_value());

    writeEntry(sandbox.root, key, payload.data(), payload.size());
    CHECK(readEntry(path, key) == payload);
}

void testConcurrentWritesAreAtomic() {
    Sandbox sandbox;
    const std::string payload = makePayload(4);
    const uint64_t key = 0x77;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int thread = 0; thread < kThreadCount; ++thread) {
        threads.emplace_back([&] {
            for (int round = 0; round < 50; ++round) {
                writeEntry(sandbox.root, key, payload.data(), payload.size());
                const auto loaded = readEntry(entryPath(sandbox.root, key), key);
                if (loaded && *loaded != payload) ++failures;
            }
        });
    }
    for (auto& thread : threads) thread.join();
    CHECK(failures == 0);
    CHECK(readEntry(entryPath(sandbox.root, key), key) == payload);
    int leftover = 0;
    for (const auto& entry : fs::directory_iterator(sandbox.root)) leftover += entry.path().string().ends_with(kTemporaryTag) ? 1 : 0;
    CHECK(leftover == 0);
}

}

int main() {
    testKeyInvalidation();
    testRoundTrip();
    testCorruptionIsRejectedAndRemoved();
    testConcurrentWritesAreAtomic();
    return testsupport::finish();
}
