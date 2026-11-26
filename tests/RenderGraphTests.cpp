#include "TestSupport.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import Engine.Render.graph;

namespace {

struct FakeBackend final : rg::Backend {
    uintptr_t next = 0x1000;
    int created = 0;
    int destroyed = 0;
    std::vector<std::string> events;
    std::vector<rg::Barrier> barriers;

    void* createTexture(const rg::TextureDesc&, std::string_view name) override {
        ++created;
        events.push_back("create " + std::string(name));
        next += 0x10;
        return reinterpret_cast<void*>(next);
    }
    void* createBuffer(const rg::BufferDesc&, std::string_view name) override {
        ++created;
        events.push_back("create " + std::string(name));
        next += 0x10;
        return reinterpret_cast<void*>(next);
    }
    void destroy(void*, rg::ResourceType) override { ++destroyed; }
    void transition(std::span<const rg::Barrier> list) override { barriers.insert(barriers.end(), list.begin(), list.end()); }
    void beginTiming(int scope) override { events.push_back("begin " + std::to_string(scope)); }
    void endTiming(int scope) override { events.push_back("end " + std::to_string(scope)); }
    void beginPass(std::string_view name) override { events.push_back("pass " + std::string(name)); }
};

rg::TextureDesc colorDesc(uint32_t width = 64, uint32_t height = 32, uint32_t format = 7) {
    rg::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.bindFlags = 0x28;
    return desc;
}

int g_backbuffer = 0;
int g_depth = 0;

void testOrderAndCulling() {
    rg::RenderGraph graph;
    const rg::Handle backbuffer = graph.importTexture("backbuffer", &g_backbuffer);
    rg::Handle unused;
    rg::Handle used;
    std::vector<std::string> ran;
    graph.addPass("feeds-unused", [&](rg::PassBuilder& builder) { unused = builder.write(builder.createTexture("unused", colorDesc()), rg::Usage::RenderTarget); }, [&](rg::PassContext&) { ran.push_back("feeds-unused"); });
    graph.addPass("reads-unused", [&](rg::PassBuilder& builder) {
        builder.read(unused, rg::Usage::ShaderResource);
        builder.write(builder.createTexture("also-unused", colorDesc()), rg::Usage::UnorderedAccess);
    }, [&](rg::PassContext&) { ran.push_back("reads-unused"); });
    graph.addPass("produce", [&](rg::PassBuilder& builder) { used = builder.write(builder.createTexture("used", colorDesc()), rg::Usage::RenderTarget); }, [&](rg::PassContext&) { ran.push_back("produce"); });
    graph.addPass("present", [&](rg::PassBuilder& builder) {
        builder.read(used, rg::Usage::ShaderResource);
        builder.write(backbuffer, rg::Usage::RenderTarget);
    }, [&](rg::PassContext&) { ran.push_back("present"); });
    graph.addPass("marker", [&](rg::PassBuilder& builder) { builder.sideEffect(); }, [&](rg::PassContext&) { ran.push_back("marker"); });
    graph.compile();
    CHECK(graph.culled(0));
    CHECK(graph.culled(1));
    CHECK(!graph.culled(2));
    CHECK(!graph.culled(3));
    CHECK(!graph.culled(4));
    CHECK(graph.stats().culledPasses == 2);
    CHECK(graph.order().size() == 3);
    CHECK(graph.order()[0] == 2 && graph.order()[1] == 3 && graph.order()[2] == 4);

    FakeBackend backend;
    rg::TransientPool pool;
    graph.execute(backend, pool);
    CHECK(ran.size() == 3);
    CHECK(ran[0] == "produce" && ran[1] == "present" && ran[2] == "marker");
    CHECK(backend.created == 1);
    CHECK(graph.native(unused) == nullptr);
    CHECK(graph.native(used) != nullptr);
}

void testDependenciesFollowDeclarationOrder() {
    rg::RenderGraph graph;
    const rg::Handle depth = graph.importTexture("depth", &g_depth);
    graph.addPass("draw", [&](rg::PassBuilder& builder) { builder.write(depth, rg::Usage::DepthWrite); }, {});
    graph.addPass("sample", [&](rg::PassBuilder& builder) {
        builder.read(depth, rg::Usage::ShaderResource);
        builder.sideEffect();
    }, {});
    graph.addPass("clear", [&](rg::PassBuilder& builder) { builder.write(depth, rg::Usage::DepthWrite); }, {});
    graph.compile();
    CHECK(graph.order().size() == 3);
    CHECK(graph.order()[0] == 0 && graph.order()[1] == 1 && graph.order()[2] == 2);
    const std::string text = graph.dumpText();
    CHECK(text.find("read  depth (ShaderResource) <- draw") != std::string::npos);
}

void testTransientReuse() {
    rg::RenderGraph graph;
    const rg::Handle out = graph.importTexture("out", &g_backbuffer);
    rg::Handle first;
    rg::Handle second;
    rg::Handle overlapping;
    rg::Handle otherFormat;
    graph.addPass("a", [&](rg::PassBuilder& builder) { first = builder.write(builder.createTexture("first", colorDesc()), rg::Usage::RenderTarget); }, {});
    graph.addPass("b", [&](rg::PassBuilder& builder) {
        builder.read(first, rg::Usage::ShaderResource);
        builder.write(out, rg::Usage::RenderTarget);
    }, {});
    graph.addPass("c", [&](rg::PassBuilder& builder) {
        second = builder.write(builder.createTexture("second", colorDesc()), rg::Usage::RenderTarget);
        overlapping = builder.write(builder.createTexture("overlapping", colorDesc()), rg::Usage::UnorderedAccess);
        otherFormat = builder.write(builder.createTexture("other-format", colorDesc(64, 32, 9)), rg::Usage::UnorderedAccess);
    }, {});
    graph.addPass("d", [&](rg::PassBuilder& builder) {
        builder.read(second, rg::Usage::ShaderResource);
        builder.read(overlapping, rg::Usage::ShaderResource);
        builder.read(otherFormat, rg::Usage::ShaderResource);
        builder.write(out, rg::Usage::RenderTarget);
    }, {});
    FakeBackend backend;
    rg::TransientPool pool(2);
    graph.execute(backend, pool);
    CHECK(graph.physicalOf(first) >= 0);
    CHECK(graph.physicalOf(first) == graph.physicalOf(second));
    CHECK(graph.physicalOf(second) != graph.physicalOf(overlapping));
    CHECK(graph.physicalOf(otherFormat) != graph.physicalOf(first));
    CHECK(graph.physicalOf(otherFormat) != graph.physicalOf(overlapping));
    CHECK(backend.created == 3);
    CHECK(graph.stats().allocatedTransients == 4);
    CHECK(graph.stats().physicalTransients == 3);
    CHECK(graph.stats().physicalBytes < graph.stats().transientBytes);
    const int32_t firstSlot = graph.physicalOf(first);
    pool.endFrame(backend);

    graph.compile();
    graph.execute(backend, pool);
    CHECK(backend.created == 3);
    CHECK(graph.physicalOf(first) == firstSlot);
    pool.endFrame(backend);

    pool.endFrame(backend);
    pool.endFrame(backend);
    CHECK(backend.destroyed == 3);
    pool.clear(backend);
}

void testBarriersAndTiming() {
    rg::RenderGraph graph;
    const rg::Handle depth = graph.importTexture("depth", &g_depth);
    const rg::Handle out = graph.importTexture("out", &g_backbuffer);
    rg::Handle scratch;
    graph.addPass("gbuffer", [&](rg::PassBuilder& builder) {
        builder.write(depth, rg::Usage::DepthWrite);
        builder.timing(6);
    }, {});
    graph.addPass("gbuffer-meshlets", [&](rg::PassBuilder& builder) {
        builder.write(depth, rg::Usage::DepthWrite);
        builder.timing(6);
    }, {});
    graph.addPass("hiz", [&](rg::PassBuilder& builder) {
        builder.read(depth, rg::Usage::ShaderResource);
        scratch = builder.write(builder.createTexture("scratch", colorDesc()), rg::Usage::UnorderedAccess);
        builder.timing(7);
    }, {});
    graph.addPass("untimed", [&](rg::PassBuilder& builder) {
        builder.read(scratch, rg::Usage::ShaderResource);
        builder.write(scratch, rg::Usage::UnorderedAccess);
        builder.write(out, rg::Usage::RenderTarget);
    }, {});
    FakeBackend backend;
    rg::TransientPool pool;
    graph.execute(backend, pool);
    const std::vector<std::string> expected = {"begin 6", "pass gbuffer", "pass gbuffer-meshlets", "create scratch", "end 6", "begin 7", "pass hiz", "end 7", "pass untimed"};
    CHECK(backend.events == expected);
    CHECK(backend.barriers.size() == 6);
    CHECK(backend.barriers[0].usage == rg::Usage::DepthWrite);
    CHECK(backend.barriers[2].usage == rg::Usage::ShaderResource);
    CHECK(backend.barriers[3].usage == rg::Usage::UnorderedAccess && backend.barriers[3].discard);
    CHECK(backend.barriers[4].usage == rg::Usage::UnorderedAccess && !backend.barriers[4].discard);
    CHECK(backend.barriers[5].usage == rg::Usage::RenderTarget);
    pool.clear(backend);
}

void testExternalAccessAndViews() {
    rg::RenderGraph graph;
    int lightingTexture = 0;
    const rg::ExternalAccess accesses[] = {
        {"lighting/ao", &lightingTexture, rg::ResourceType::Texture, rg::Usage::UnorderedAccess, true},
        {"lighting/ao", &lightingTexture, rg::ResourceType::Texture, rg::Usage::ShaderResource, false},
    };
    graph.beginView("portal A");
    const std::string colorName = rg::viewResourceName(0xabcu, "color");
    CHECK(colorName == "viewabc/color");
    rg::Handle color;
    graph.addPass("view color", [&](rg::PassBuilder& builder) {
        builder.access(accesses);
        color = builder.write(builder.createTexture(colorName, colorDesc()), rg::Usage::CopyDest);
    }, {});
    graph.endView();
    graph.addPass("composite", [&](rg::PassBuilder& builder) {
        builder.read(color, rg::Usage::ShaderResource);
        builder.sideEffect();
    }, {});
    graph.compile();
    CHECK(graph.stats().resources == 2);
    CHECK(graph.stats().importedResources == 1);
    CHECK(graph.order().size() == 2);
    const std::string dot = graph.dumpDot();
    CHECK(dot.rfind("digraph RenderGraph", 0) == 0);
    CHECK(dot.find("label=\"view portal A\"") != std::string::npos);
    CHECK(dot.find("viewabc/color") != std::string::npos);
    const std::string text = graph.dumpText();
    CHECK(text.find("[view portal A]") != std::string::npos);
}

}

int main() {
    testOrderAndCulling();
    testDependenciesFollowDeclarationOrder();
    testTransientReuse();
    testBarriersAndTiming();
    testExternalAccessAndViews();
    return testsupport::finish();
}
