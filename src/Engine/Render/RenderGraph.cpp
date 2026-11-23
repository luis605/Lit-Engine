module;

#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module Engine.Render.graph;

namespace rg {

namespace {

constexpr const char* kUsageNames[] = {"None", "ShaderResource", "UnorderedAccess", "RenderTarget", "DepthWrite", "DepthRead", "CopySource", "CopyDest", "IndirectArgument", "VertexBuffer", "IndexBuffer", "ConstantBuffer"};

std::string dotEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

}

const char* usageName(Usage usage) { return kUsageNames[static_cast<size_t>(usage)]; }

bool usageWrites(Usage usage) { return usage == Usage::UnorderedAccess || usage == Usage::RenderTarget || usage == Usage::DepthWrite || usage == Usage::CopyDest; }

std::string viewResourceName(uint64_t viewKey, std::string_view resource) { return std::format("view{:x}/{}", viewKey, resource); }

uint32_t TransientPool::acquire(Backend& backend, ResourceType type, const TextureDesc& texture, const BufferDesc& buffer, std::string_view name) {
    // Lowest free index with an identical description: the same frame structure maps every virtual resource to the same
    // physical one each frame, so bindings cached by the passes stay valid.
    for (uint32_t index = 0; index < m_entries.size(); ++index) {
        Entry& entry = m_entries[index];
        if (entry.inUse || entry.type != type || !entry.native) continue;
        if (type == ResourceType::Texture ? !(entry.texture == texture) : !(entry.buffer == buffer)) continue;
        entry.inUse = true;
        entry.lastFrame = m_frame;
        return index;
    }
    Entry entry;
    entry.type = type;
    entry.texture = texture;
    entry.buffer = buffer;
    entry.name = std::string(name);
    entry.native = type == ResourceType::Texture ? backend.createTexture(texture, name) : backend.createBuffer(buffer, name);
    entry.inUse = true;
    entry.lastFrame = m_frame;
    for (uint32_t index = 0; index < m_entries.size(); ++index) {
        if (!m_entries[index].native && !m_entries[index].inUse) {
            m_entries[index] = std::move(entry);
            return index;
        }
    }
    m_entries.push_back(std::move(entry));
    return static_cast<uint32_t>(m_entries.size() - 1);
}

void TransientPool::release(uint32_t entry) {
    if (entry < m_entries.size()) m_entries[entry].inUse = false;
}

void TransientPool::endFrame(Backend& backend) {
    for (Entry& entry : m_entries) {
        entry.inUse = false;
        if (entry.native && m_frame - entry.lastFrame >= m_retireFrames) {
            backend.destroy(entry.native, entry.type);
            entry.native = nullptr;
        }
    }
    ++m_frame;
}

void TransientPool::clear(Backend& backend) {
    for (Entry& entry : m_entries) {
        if (entry.native) backend.destroy(entry.native, entry.type);
    }
    m_entries.clear();
}

void* PassContext::native(Handle handle) const { return m_graph.native(handle); }

void RenderGraph::reset() {
    m_externalResolver = {};
    m_passes.clear();
    m_resources.clear();
    m_order.clear();
    m_view.clear();
    m_stats = {};
    m_compiled = false;
}

Handle RenderGraph::addResource(Resource resource) {
    m_resources.push_back(std::move(resource));
    return Handle{static_cast<uint32_t>(m_resources.size() - 1)};
}

Handle RenderGraph::importTexture(std::string_view name, void* native, Usage initial) {
    if (const Handle existing = find(name); existing.valid()) return existing;
    Resource resource;
    resource.name = std::string(name);
    resource.type = ResourceType::Texture;
    resource.imported = true;
    resource.native = native;
    resource.initial = initial;
    return addResource(std::move(resource));
}

Handle RenderGraph::importBuffer(std::string_view name, void* native, Usage initial) {
    if (const Handle existing = find(name); existing.valid()) return existing;
    Resource resource;
    resource.name = std::string(name);
    resource.type = ResourceType::Buffer;
    resource.imported = true;
    resource.native = native;
    resource.initial = initial;
    return addResource(std::move(resource));
}

Handle RenderGraph::importTexture(std::string_view name, std::function<void*()> resolve) {
    if (const Handle existing = find(name); existing.valid()) return existing;
    Resource resource;
    resource.name = std::string(name);
    resource.type = ResourceType::Texture;
    resource.imported = true;
    resource.native = resolve ? resolve() : nullptr;
    resource.resolve = std::move(resolve);
    return addResource(std::move(resource));
}

Handle RenderGraph::importBuffer(std::string_view name, std::function<void*()> resolve) {
    if (const Handle existing = find(name); existing.valid()) return existing;
    Resource resource;
    resource.name = std::string(name);
    resource.type = ResourceType::Buffer;
    resource.imported = true;
    resource.native = resolve ? resolve() : nullptr;
    resource.resolve = std::move(resolve);
    return addResource(std::move(resource));
}

Handle RenderGraph::find(std::string_view name) const {
    for (uint32_t index = 0; index < m_resources.size(); ++index) {
        if (m_resources[index].name == name) return Handle{index};
    }
    return {};
}

Handle RenderGraph::createTexture(std::string_view name, const TextureDesc& desc) {
    Resource resource;
    resource.name = std::string(name);
    resource.type = ResourceType::Texture;
    resource.texture = desc;
    return addResource(std::move(resource));
}

Handle RenderGraph::createBuffer(std::string_view name, const BufferDesc& desc) {
    Resource resource;
    resource.name = std::string(name);
    resource.type = ResourceType::Buffer;
    resource.buffer = desc;
    return addResource(std::move(resource));
}

void RenderGraph::beginView(std::string label) { m_view = std::move(label); }

void RenderGraph::endView() { m_view.clear(); }

uint32_t RenderGraph::addPass(std::string name, const SetupFn& setup, ExecuteFn execute) {
    m_compiled = false;
    Pass pass;
    pass.name = std::move(name);
    pass.view = m_view;
    pass.execute = std::move(execute);
    m_passes.push_back(std::move(pass));
    const auto index = static_cast<uint32_t>(m_passes.size() - 1);
    PassBuilder builder(*this, index);
    if (setup) setup(builder);
    return index;
}

void RenderGraph::declare(uint32_t passIndex, Handle handle, Usage usage, bool write) {
    if (!handle.valid() || handle.id >= m_resources.size()) return;
    Pass& pass = m_passes[passIndex];
    Resource& resource = m_resources[handle.id];
    Access access;
    access.resource = handle.id;
    access.usage = usage;
    access.write = write;
    access.producer = resource.lastWriter;
    auto depend = [&](uint32_t other) {
        if (other == Handle::kInvalid || other == passIndex) return;
        if (std::ranges::find(pass.dependencies, other) == pass.dependencies.end()) pass.dependencies.push_back(other);
    };
    depend(resource.lastWriter);  // read after write, write after write
    if (write) {
        for (const uint32_t reader : resource.readersSinceWrite) depend(reader);  // write after read
        resource.readersSinceWrite.clear();
        resource.lastWriter = passIndex;
    } else if (std::ranges::find(resource.readersSinceWrite, passIndex) == resource.readersSinceWrite.end()) {
        resource.readersSinceWrite.push_back(passIndex);
    }
    pass.accesses.push_back(access);
}

bool RenderGraph::culled(uint32_t pass) const { return pass < m_passes.size() && m_passes[pass].culled; }

void RenderGraph::compile() {
    const auto passCount = static_cast<uint32_t>(m_passes.size());
    // Liveness: passes with side effects or writes to imported resources are roots; every pass that produced a version a
    // live pass reads (or partially overwrites) is live. Everything else only feeds unread transients and is culled.
    std::vector<uint8_t> live(passCount, 0);
    std::vector<uint32_t> work;
    for (uint32_t index = 0; index < passCount; ++index) {
        const Pass& pass = m_passes[index];
        bool root = pass.sideEffect;
        for (const Access& access : pass.accesses) root = root || (access.write && m_resources[access.resource].imported);
        if (root) {
            live[index] = 1;
            work.push_back(index);
        }
    }
    while (!work.empty()) {
        const uint32_t index = work.back();
        work.pop_back();
        for (const Access& access : m_passes[index].accesses) {
            if (access.producer == Handle::kInvalid || live[access.producer]) continue;
            live[access.producer] = 1;
            work.push_back(access.producer);
        }
    }
    for (uint32_t index = 0; index < passCount; ++index) m_passes[index].culled = live[index] == 0;

    // Topological order of the live passes (Kahn); ties go to the earlier declaration, so a valid declaration order is kept.
    std::vector<uint32_t> pending(passCount, 0);
    std::vector<std::vector<uint32_t>> dependents(passCount);
    for (uint32_t index = 0; index < passCount; ++index) {
        if (!live[index]) continue;
        for (const uint32_t dependency : m_passes[index].dependencies) {
            if (!live[dependency]) continue;
            ++pending[index];
            dependents[dependency].push_back(index);
        }
    }
    std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<>> ready;
    for (uint32_t index = 0; index < passCount; ++index) {
        if (live[index] && pending[index] == 0) ready.push(index);
    }
    m_order.clear();
    while (!ready.empty()) {
        const uint32_t index = ready.top();
        ready.pop();
        m_order.push_back(index);
        for (const uint32_t dependent : dependents[index]) {
            if (--pending[dependent] == 0) ready.push(dependent);
        }
    }

    for (Resource& resource : m_resources) {
        resource.first = Handle::kInvalid;
        resource.last = Handle::kInvalid;
        resource.physical = -1;
    }
    for (uint32_t position = 0; position < m_order.size(); ++position) {
        for (const Access& access : m_passes[m_order[position]].accesses) {
            Resource& resource = m_resources[access.resource];
            if (resource.first == Handle::kInvalid) resource.first = position;
            resource.last = position;
        }
    }

    m_stats = {};
    m_stats.passes = passCount;
    m_stats.culledPasses = passCount - static_cast<uint32_t>(m_order.size());
    m_stats.resources = static_cast<uint32_t>(m_resources.size());
    for (const Resource& resource : m_resources) {
        if (resource.imported) ++m_stats.importedResources;
        else ++m_stats.transientResources;
    }
    m_compiled = true;
}

void RenderGraph::execute(Backend& backend, TransientPool& pool) {
    if (!m_compiled) compile();
    std::vector<std::vector<uint32_t>> acquireAt(m_order.size());
    std::vector<std::vector<uint32_t>> releaseAt(m_order.size());
    for (uint32_t index = 0; index < m_resources.size(); ++index) {
        const Resource& resource = m_resources[index];
        if (resource.imported || resource.first == Handle::kInvalid) continue;
        acquireAt[resource.first].push_back(index);
        releaseAt[resource.last].push_back(index);
    }

    std::vector<uint8_t> physicalSeen(pool.size() + m_resources.size(), 0);
    std::vector<Barrier> barriers;
    std::vector<uint32_t> barrierResources;
    int openTiming = -1;
    for (uint32_t position = 0; position < m_order.size(); ++position) {
        const uint32_t passIndex = m_order[position];
        Pass& pass = m_passes[passIndex];
        for (const uint32_t index : acquireAt[position]) {
            Resource& resource = m_resources[index];
            const uint32_t entry = pool.acquire(backend, resource.type, resource.texture, resource.buffer, resource.name);
            resource.physical = static_cast<int32_t>(entry);
            resource.native = pool.entries()[entry].native;
            ++m_stats.allocatedTransients;
            const uint64_t size = resource.type == ResourceType::Texture ? backend.bytes(resource.texture) : resource.buffer.size;
            m_stats.transientBytes += size;
            if (entry >= physicalSeen.size()) physicalSeen.resize(entry + 1, 0);
            if (!physicalSeen[entry]) {
                physicalSeen[entry] = 1;
                ++m_stats.physicalTransients;
                m_stats.physicalBytes += size;
            }
        }

        for (const Access& access : pass.accesses) {
            Resource& resource = m_resources[access.resource];
            if (resource.resolve) resource.native = resource.resolve();
        }
        // One state per resource and pass: a write usage wins over a read usage of the same resource.
        barriers.clear();
        barrierResources.clear();
        for (const Access& access : pass.accesses) {
            if (access.usage == Usage::None) continue;
            const Resource& resource = m_resources[access.resource];
            if (!resource.native) continue;
            const auto found = std::ranges::find(barrierResources, access.resource);
            if (found != barrierResources.end()) {
                Barrier& existing = barriers[static_cast<size_t>(found - barrierResources.begin())];
                if (access.write) existing.usage = access.usage;
                continue;
            }
            barrierResources.push_back(access.resource);
            barriers.push_back(Barrier{resource.native, resource.type, access.usage, !resource.imported && resource.first == position, resource.name});
        }

        if (pass.timing != openTiming) {
            if (openTiming >= 0) backend.endTiming(openTiming);
            if (pass.timing >= 0) backend.beginTiming(pass.timing);
            openTiming = pass.timing;
        }
        if (!barriers.empty()) {
            backend.transition(barriers);
            m_stats.barriers += static_cast<uint32_t>(barriers.size());
        }
        backend.beginPass(pass.name);
        if (pass.execute) {
            PassContext context(*this, passIndex);
            pass.execute(context);
        }
        backend.endPass();
        for (const uint32_t index : releaseAt[position]) pool.release(static_cast<uint32_t>(m_resources[index].physical));
    }
    if (openTiming >= 0) backend.endTiming(openTiming);
}

int32_t RenderGraph::physicalOf(Handle handle) const { return handle.valid() && handle.id < m_resources.size() ? m_resources[handle.id].physical : -1; }

void* RenderGraph::native(Handle handle) const { return handle.valid() && handle.id < m_resources.size() ? m_resources[handle.id].native : nullptr; }

std::string RenderGraph::dumpText() const {
    std::string out = std::format("RenderGraph: {} passes ({} culled), {} resources ({} imported, {} transient), {} transients placed in {} physical resources (~{:.1f} MB virtual, ~{:.1f} MB physical), {} barriers\n",
                                  m_stats.passes, m_stats.culledPasses, m_stats.resources, m_stats.importedResources, m_stats.transientResources, m_stats.allocatedTransients, m_stats.physicalTransients,
                                  static_cast<double>(m_stats.transientBytes) / (1024.0 * 1024.0), static_cast<double>(m_stats.physicalBytes) / (1024.0 * 1024.0), m_stats.barriers);
    std::vector<int32_t> position(m_passes.size(), -1);
    for (uint32_t index = 0; index < m_order.size(); ++index) position[m_order[index]] = static_cast<int32_t>(index);
    out += "\nPasses (declaration order; #position in execution order):\n";
    for (uint32_t index = 0; index < m_passes.size(); ++index) {
        const Pass& pass = m_passes[index];
        out += position[index] >= 0 ? std::format("  #{:03} {}", position[index], pass.name) : std::format("  CULLED {}", pass.name);
        if (!pass.view.empty()) out += std::format("  [view {}]", pass.view);
        if (pass.timing >= 0) out += static_cast<size_t>(pass.timing) < m_timingNames.size() ? std::format("  [timing {}]", m_timingNames[static_cast<size_t>(pass.timing)]) : std::format("  [timing {}]", pass.timing);
        if (pass.sideEffect) out += "  [side effect]";
        out += '\n';
        for (const Access& access : pass.accesses) {
            const Resource& resource = m_resources[access.resource];
            out += std::format("        {} {} ({}){}\n", access.write ? "write" : "read ", resource.name, usageName(access.usage), access.producer != Handle::kInvalid ? std::format(" <- {}", m_passes[access.producer].name) : "");
        }
    }
    out += "\nResources:\n";
    for (const Resource& resource : m_resources) {
        out += std::format("  {} {} {}", resource.imported ? "imported " : "transient", resource.type == ResourceType::Texture ? "texture" : "buffer ", resource.name);
        if (!resource.imported) {
            if (resource.type == ResourceType::Texture) out += std::format(" {}x{}x{} mips {} format {} bind 0x{:x}", resource.texture.width, resource.texture.height, resource.texture.arraySize, resource.texture.mipLevels, resource.texture.format, resource.texture.bindFlags);
            else out += std::format(" {} bytes bind 0x{:x}", resource.buffer.size, resource.buffer.bindFlags);
            if (resource.first != Handle::kInvalid) out += std::format(" lifetime #{:03}..#{:03} -> physical {}", resource.first, resource.last, resource.physical);
            else out += " unused";
        }
        out += '\n';
    }
    return out;
}

std::string RenderGraph::dumpDot() const {
    std::string out = "digraph RenderGraph {\n  rankdir=LR;\n  node [fontname=\"Helvetica\", fontsize=10];\n";
    std::vector<std::string> views;
    for (const Pass& pass : m_passes) {
        if (std::ranges::find(views, pass.view) == views.end()) views.push_back(pass.view);
    }
    for (size_t viewIndex = 0; viewIndex < views.size(); ++viewIndex) {
        const bool cluster = !views[viewIndex].empty();
        if (cluster) out += std::format("  subgraph cluster_{} {{\n    label=\"view {}\";\n", viewIndex, dotEscape(views[viewIndex]));
        for (uint32_t index = 0; index < m_passes.size(); ++index) {
            const Pass& pass = m_passes[index];
            if (pass.view != views[viewIndex]) continue;
            out += std::format("    p{} [shape=box, style=\"filled{}\", fillcolor=\"{}\", label=\"{}\"];\n", index, pass.culled ? ",dashed" : "", pass.culled ? "#eeeeee" : "#cfe3ff", dotEscape(pass.name));
        }
        if (cluster) out += "  }\n";
    }
    for (uint32_t index = 0; index < m_resources.size(); ++index) {
        const Resource& resource = m_resources[index];
        out += std::format("  r{} [shape={}, style=filled, fillcolor=\"{}\", label=\"{}\"];\n", index, resource.type == ResourceType::Texture ? "ellipse" : "hexagon", resource.imported ? "#fff2c6" : "#d7f5d0", dotEscape(resource.name));
    }
    for (uint32_t index = 0; index < m_passes.size(); ++index) {
        for (const Access& access : m_passes[index].accesses) {
            if (access.write) out += std::format("  p{} -> r{} [color=\"#c0392b\", label=\"{}\"];\n", index, access.resource, usageName(access.usage));
            else out += std::format("  r{} -> p{} [color=\"#2c3e50\", label=\"{}\"];\n", access.resource, index, usageName(access.usage));
        }
    }
    out += "}\n";
    return out;
}

Handle PassBuilder::read(Handle handle, Usage usage) {
    m_graph.declare(m_pass, handle, usage, false);
    return handle;
}

Handle PassBuilder::write(Handle handle, Usage usage) {
    m_graph.declare(m_pass, handle, usage, true);
    return handle;
}

Handle PassBuilder::access(const ExternalAccess& external) {
    if (!external.native) return {};
    Handle handle = m_graph.find(external.name);
    if (!handle.valid()) {
        if (m_graph.m_externalResolver) {
            auto resolve = [resolver = m_graph.m_externalResolver, name = std::string(external.name)] { return resolver(name); };
            handle = external.type == ResourceType::Texture ? m_graph.importTexture(external.name, resolve) : m_graph.importBuffer(external.name, resolve);
        } else {
            handle = external.type == ResourceType::Texture ? m_graph.importTexture(external.name, external.native) : m_graph.importBuffer(external.name, external.native);
        }
    }
    return external.write ? write(handle, external.usage) : read(handle, external.usage);
}

void PassBuilder::access(std::span<const ExternalAccess> externals) {
    for (const ExternalAccess& external : externals) access(external);
}

Handle PassBuilder::createTexture(std::string_view name, const TextureDesc& desc) {
    RenderGraph::Resource resource;
    resource.name = std::string(name);
    resource.type = ResourceType::Texture;
    resource.texture = desc;
    resource.creator = m_pass;
    return m_graph.addResource(std::move(resource));
}

Handle PassBuilder::createBuffer(std::string_view name, const BufferDesc& desc) {
    RenderGraph::Resource resource;
    resource.name = std::string(name);
    resource.type = ResourceType::Buffer;
    resource.buffer = desc;
    resource.creator = m_pass;
    return m_graph.addResource(std::move(resource));
}

void PassBuilder::sideEffect() { m_graph.m_passes[m_pass].sideEffect = true; }

void PassBuilder::timing(int scope) { m_graph.m_passes[m_pass].timing = scope; }

}
