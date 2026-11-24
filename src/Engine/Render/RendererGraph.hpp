#pragma once

// Renderer side of the render graph (docs/RenderGraph.md). Included after RendererInternal.hpp by the implementation
// units that build or run the frame graph (RendererGraph.cpp, RendererPortalViews.cpp); needs Engine.Render.graph.

// Diligent backend: transient resources, declared-state barriers and the GpuPass timestamp scopes.
class DiligentGraphBackend final : public rg::Backend {
  public:
    DiligentGraphBackend(DiligentData& gpu, std::function<void(int)> beginTiming, std::function<void(int)> endTiming);
    void* createTexture(const rg::TextureDesc& desc, std::string_view name) override;
    void* createBuffer(const rg::BufferDesc& desc, std::string_view name) override;
    void destroy(void* native, rg::ResourceType type) override;
    void transition(std::span<const rg::Barrier> barriers) override;
    inline void beginTiming(int scope) override { m_beginTiming(scope); }
    inline void endTiming(int scope) override { m_endTiming(scope); }
    void beginPass(std::string_view name) override;
    void endPass() override;
    [[nodiscard]] uint64_t bytes(const rg::TextureDesc& desc) const override;
    [[nodiscard]] inline uint32_t skippedDeclarations() const { return m_skipped; }

  private:
    DiligentData& m_gpu;
    std::function<void(int)> m_beginTiming;
    std::function<void(int)> m_endTiming;
    std::vector<Diligent::StateTransitionDesc> m_descs;
    std::vector<std::string> m_warned;
    std::string m_label;
    uint32_t m_skipped = 0;
    bool m_labels = false;
};

struct RendererGraphState {
    rg::RenderGraph graph;
    rg::TransientPool pool{8};
    std::unique_ptr<DiligentGraphBackend> backend;
    std::string dumpPath;  // requested by requestGraphDump: written after the next graph frame
    std::string envDumpPath;
    uint64_t envDumpFrame = 0;
    uint64_t frames = 0;
    std::string lastDump;
    rg::GraphStats stats;
    ~RendererGraphState();
};

// Per-frame values the passes hand to each other (filled while the graph executes, in pass order).
struct FrameGraphData {
    const Camera* camera = nullptr;
    SceneDatabase* scene = nullptr;
    float time = 0.0f;
    float deltaTime = 0.0f;
    uint32_t objectCount = 0;
    bool portalsActive = false;
    SceneUniforms uniforms{};      // main view (Lighting::beginFrame)
    SceneUniforms rootUniforms{};  // main view with the camera cell's parameters (portal frames)
    FrameInput input;
    int hdrIndex = 0;
    rg::Handle depth;
    rg::Handle gbuffer[3];
    rg::Handle hdr;
    rg::Handle hdrOther;
    rg::Handle sceneUbo;
    rg::Handle visibilityBits;
    rg::Handle vertices;
    rg::Handle indices;
    rg::Handle objects;
    rg::Handle instances;  // instance pool (RendererInstancing.cpp)
    rg::Handle phases[kPhaseCount];
    rg::Handle meshlets[kPhaseCount];
    rg::Handle accumulation;
    rg::Handle revealage;
    rg::Handle shadowMap;
    rg::Handle localShadowMap;
    rg::Handle vsmAtlas;
    rg::Handle effectsDepth;
    struct ViewOutputs {
        bool rendered = false;
        rg::Handle color;
        rg::Handle depth;
        rg::Handle gbuffer[3];
    };
    ViewOutputs views[kMaxPortalViews];
};

rg::TextureDesc GraphTextureDesc(const Diligent::TextureDesc& desc);
