module;

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Engine.Render.graph;

export namespace rg {

enum class Usage : uint8_t { None, ShaderResource, UnorderedAccess, RenderTarget, DepthWrite, DepthRead, CopySource, CopyDest, IndirectArgument, VertexBuffer, IndexBuffer, ConstantBuffer };

enum class ResourceType : uint8_t { Texture, Buffer };

[[nodiscard]] const char* usageName(Usage usage);
[[nodiscard]] bool usageWrites(Usage usage);

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
    bool discard = false;
    std::string_view name;
};

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
    [[nodiscard]] virtual uint64_t bytes(const TextureDesc& desc) const { return static_cast<uint64_t>(desc.width) * desc.height * desc.arraySize * 4u; }
};

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
    uint32_t allocatedTransients = 0;
    uint32_t physicalTransients = 0;
    uint32_t barriers = 0;
    uint64_t transientBytes = 0;
    uint64_t physicalBytes = 0;
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
    Handle importTexture(std::string_view name, std::function<void*()> resolve);
    Handle importBuffer(std::string_view name, std::function<void*()> resolve);
    void setExternalResolver(std::function<void*(std::string_view)> resolve) { m_externalResolver = std::move(resolve); }
    void setTimingNames(std::vector<std::string> names) { m_timingNames = std::move(names); }
    [[nodiscard]] Handle find(std::string_view name) const;
    Handle createTexture(std::string_view name, const TextureDesc& desc);
    Handle createBuffer(std::string_view name, const BufferDesc& desc);
    void beginView(std::string label);
    void endView();
    uint32_t addPass(std::string name, const SetupFn& setup, ExecuteFn execute);
    void compile();
    void execute(Backend& backend, TransientPool& pool);

    [[nodiscard]] const GraphStats& stats() const { return m_stats; }
    [[nodiscard]] const std::vector<uint32_t>& order() const { return m_order; }
    [[nodiscard]] bool culled(uint32_t pass) const;
    [[nodiscard]] uint32_t passCount() const { return static_cast<uint32_t>(m_passes.size()); }
    [[nodiscard]] const std::string& passName(uint32_t pass) const { return m_passes[pass].name; }
    [[nodiscard]] int32_t physicalOf(Handle handle) const;
    [[nodiscard]] void* native(Handle handle) const;
    [[nodiscard]] std::string dumpText() const;
    [[nodiscard]] std::string dumpDot() const;

  private:
    friend class PassBuilder;
    struct Access {
        uint32_t resource = 0;
        Usage usage = Usage::None;
        bool write = false;
        uint32_t producer = Handle::kInvalid;
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
        uint32_t first = Handle::kInvalid;
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
