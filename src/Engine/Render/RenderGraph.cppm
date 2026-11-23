module;

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Render graph (docs/RenderGraph.md): passes declare the textures and buffers they read and write, the graph derives
// the dependency DAG from the declaration order, culls passes whose results nobody consumes, places transient resources
// in a pool that reuses physical resources of identical description across non-overlapping lifetimes (and across frames),
// moves every declared resource into the state its pass declared before the pass runs, and brackets passes with the
// timing scopes of the frame profiler. The module does not know Diligent: a Backend creates resources, issues barriers
// and records timestamps, so the graph logic is testable without a GPU (tests/RenderGraphTests.cpp).
export module Engine.Render.graph;

export namespace rg {

// Declared use of a resource by a pass. The backend maps it to a resource state (Diligent RESOURCE_STATE_*).
// None declares a dependency only: the pass manages the state itself (for example a resource used in several states).
enum class Usage : uint8_t { None, ShaderResource, UnorderedAccess, RenderTarget, DepthWrite, DepthRead, CopySource, CopyDest, IndirectArgument, VertexBuffer, IndexBuffer, ConstantBuffer };

enum class ResourceType : uint8_t { Texture, Buffer };

[[nodiscard]] const char* usageName(Usage usage);
[[nodiscard]] bool usageWrites(Usage usage);

// Backend values (Diligent TEXTURE_FORMAT, BIND_FLAGS, RESOURCE_DIMENSION, ...) are stored as integers; two transient
// resources share a physical resource only when their descriptions compare equal.
struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t arraySize = 1;
    uint32_t mipLevels = 1;
    uint32_t sampleCount = 1;
    uint32_t format = 0;
    uint32_t bindFlags = 0;
    uint32_t dimension = 0;
    bool operator==(const TextureDesc&) const = default;
};

struct BufferDesc {
    uint64_t size = 0;
    uint32_t bindFlags = 0;
    uint32_t mode = 0;
    uint32_t stride = 0;
    bool operator==(const BufferDesc&) const = default;
};

struct Handle {
    static constexpr uint32_t kInvalid = 0xFFFFFFFFu;
    uint32_t id = kInvalid;
    [[nodiscard]] bool valid() const { return id != kInvalid; }
    bool operator==(const Handle&) const = default;
};

struct Barrier {
    void* native = nullptr;
    ResourceType type = ResourceType::Texture;
    Usage usage = Usage::None;
    bool discard = false;  // first use of a transient: previous contents are not needed
    std::string_view name;
};

// Resource of another module (for example Lighting's internal textures) declared by name: imported on first use.
struct ExternalAccess {
    std::string_view name;
    void* native = nullptr;
    ResourceType type = ResourceType::Texture;
    Usage usage = Usage::None;
    bool write = false;
};

class Backend {
  public:
    virtual ~Backend() = default;
    virtual void* createTexture(const TextureDesc& desc, std::string_view name) = 0;
    virtual void* createBuffer(const BufferDesc& desc, std::string_view name) = 0;
    virtual void destroy(void* native, ResourceType type) = 0;
    virtual void transition(std::span<const Barrier> barriers) = 0;
    virtual void beginTiming(int scope) = 0;
    virtual void endTiming(int scope) = 0;
    virtual void beginPass(std::string_view) {}
    virtual void endPass() {}
    // Estimated size for the statistics (the graph does not know backend formats).
    [[nodiscard]] virtual uint64_t bytes(const TextureDesc& desc) const { return static_cast<uint64_t>(desc.width) * desc.height * desc.arraySize * 4u; }
};

// Physical transient resources, kept across frames. A graph acquires one per transient resource at its first use and
// releases it after its last use; later resources of the same description reuse it within the frame. Resources nobody
// acquired for `retireFrames` frames are destroyed in endFrame.
class TransientPool {
  public:
    explicit TransientPool(uint32_t retireFrames = 8) : m_retireFrames(retireFrames) {}
    TransientPool(const TransientPool&) = delete;
    TransientPool& operator=(const TransientPool&) = delete;

    void endFrame(Backend& backend);
    void clear(Backend& backend);
    [[nodiscard]] uint32_t size() const { return static_cast<uint32_t>(m_entries.size()); }
    [[nodiscard]] uint64_t frame() const { return m_frame; }

    struct Entry {
        ResourceType type = ResourceType::Texture;
        TextureDesc texture;
        BufferDesc buffer;
        void* native = nullptr;
        std::string name;
        uint64_t lastFrame = 0;
        bool inUse = false;
    };
    [[nodiscard]] const std::vector<Entry>& entries() const { return m_entries; }

    uint32_t acquire(Backend& backend, ResourceType type, const TextureDesc& texture, const BufferDesc& buffer, std::string_view name);
    void release(uint32_t entry);

  private:
    std::vector<Entry> m_entries;
    uint64_t m_frame = 1;
    uint32_t m_retireFrames = 8;
};

class RenderGraph;
class PassBuilder;

class PassContext {
  public:
    [[nodiscard]] void* native(Handle handle) const;
    template <typename T>
    [[nodiscard]] T* as(Handle handle) const {
        return static_cast<T*>(native(handle));
    }
    [[nodiscard]] uint32_t pass() const { return m_pass; }

  private:
    friend class RenderGraph;
    PassContext(const RenderGraph& graph, uint32_t pass) : m_graph(graph), m_pass(pass) {}
    const RenderGraph& m_graph;
    uint32_t m_pass;
};

using SetupFn = std::function<void(PassBuilder&)>;
using ExecuteFn = std::function<void(PassContext&)>;

struct GraphStats {
    uint32_t passes = 0;
    uint32_t culledPasses = 0;
    uint32_t resources = 0;
    uint32_t importedResources = 0;
    uint32_t transientResources = 0;
    uint32_t allocatedTransients = 0;  // transients used by a live pass
    uint32_t physicalTransients = 0;   // distinct physical resources they were placed in
    uint32_t barriers = 0;
    uint64_t transientBytes = 0;  // estimated: sum over virtual transients (what separate allocations would hold)
    uint64_t physicalBytes = 0;   // estimated: sum over the distinct physical resources used
};

[[nodiscard]] std::string viewResourceName(uint64_t viewKey, std::string_view resource);

class RenderGraph {
  public:
    RenderGraph() = default;
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    void reset();
    Handle importTexture(std::string_view name, void* native, Usage initial = Usage::None);
    Handle importBuffer(std::string_view name, void* native, Usage initial = Usage::None);
    // Imports whose native object is looked up when a pass runs (resources another system may recreate while the
    // frame executes, for example a shadow map resized by the lighting setup pass).
    Handle importTexture(std::string_view name, std::function<void*()> resolve);
    Handle importBuffer(std::string_view name, std::function<void*()> resolve);
    // Resolver for ExternalAccess imports (by name); without it the pointer given at declaration time is used.
    void setExternalResolver(std::function<void*(std::string_view)> resolve) { m_externalResolver = std::move(resolve); }
    // Names of the timing scopes for the dump (index = scope id).
    void setTimingNames(std::vector<std::string> names) { m_timingNames = std::move(names); }
    [[nodiscard]] Handle find(std::string_view name) const;
    // Transient resources declared ahead of the passes that use them (handles can then be captured by execute
    // callbacks). Placement happens at the first live use; a transient nobody uses is never allocated.
    Handle createTexture(std::string_view name, const TextureDesc& desc);
    Handle createBuffer(std::string_view name, const BufferDesc& desc);
    // Passes added between beginView and endView belong to that view (dump grouping, view-keyed resource names).
    void beginView(std::string label);
    void endView();
    uint32_t addPass(std::string name, const SetupFn& setup, ExecuteFn execute);
    // Builds the DAG, culls dead passes, orders the live ones and computes transient lifetimes.
    void compile();
    // Runs the live passes in order: transient placement, declared state transitions, timing scopes, execution.
    void execute(Backend& backend, TransientPool& pool);

    [[nodiscard]] const GraphStats& stats() const { return m_stats; }
    [[nodiscard]] const std::vector<uint32_t>& order() const { return m_order; }
    [[nodiscard]] bool culled(uint32_t pass) const;
    [[nodiscard]] uint32_t passCount() const { return static_cast<uint32_t>(m_passes.size()); }
    [[nodiscard]] const std::string& passName(uint32_t pass) const { return m_passes[pass].name; }
    [[nodiscard]] int32_t physicalOf(Handle handle) const;  // pool entry of a transient after execute (-1 otherwise)
    [[nodiscard]] void* native(Handle handle) const;
    [[nodiscard]] std::string dumpText() const;
    [[nodiscard]] std::string dumpDot() const;

  private:
    friend class PassBuilder;
    struct Access {
        uint32_t resource = 0;
        Usage usage = Usage::None;
        bool write = false;
        uint32_t producer = Handle::kInvalid;  // pass that wrote the version this access reads (or overwrites)
    };
    struct Pass {
        std::string name;
        std::string view;
        ExecuteFn execute;
        std::vector<Access> accesses;
        std::vector<uint32_t> dependencies;
        int timing = -1;
        bool sideEffect = false;
        bool culled = false;
    };
    struct Resource {
        std::string name;
        ResourceType type = ResourceType::Texture;
        bool imported = false;
        Usage initial = Usage::None;
        TextureDesc texture;
        BufferDesc buffer;
        void* native = nullptr;
        std::function<void*()> resolve;
        uint32_t creator = Handle::kInvalid;
        uint32_t lastWriter = Handle::kInvalid;
        std::vector<uint32_t> readersSinceWrite;
        uint32_t first = Handle::kInvalid;  // positions in m_order
        uint32_t last = Handle::kInvalid;
        int32_t physical = -1;
    };

    Handle addResource(Resource resource);
    void declare(uint32_t pass, Handle handle, Usage usage, bool write);

    std::vector<Pass> m_passes;
    std::vector<Resource> m_resources;
    std::vector<uint32_t> m_order;
    std::string m_view;
    std::function<void*(std::string_view)> m_externalResolver;
    std::vector<std::string> m_timingNames;
    GraphStats m_stats;
    bool m_compiled = false;
};

class PassBuilder {
  public:
    Handle read(Handle handle, Usage usage);
    Handle write(Handle handle, Usage usage);
    Handle readWrite(Handle handle, Usage usage) { return write(handle, usage); }
    Handle access(const ExternalAccess& external);
    void access(std::span<const ExternalAccess> externals);
    Handle createTexture(std::string_view name, const TextureDesc& desc);
    Handle createBuffer(std::string_view name, const BufferDesc& desc);
    void sideEffect();
    void timing(int scope);
    [[nodiscard]] RenderGraph& graph() { return m_graph; }

  private:
    friend class RenderGraph;
    PassBuilder(RenderGraph& graph, uint32_t pass) : m_graph(graph), m_pass(pass) {}
    RenderGraph& m_graph;
    uint32_t m_pass;
};

}
