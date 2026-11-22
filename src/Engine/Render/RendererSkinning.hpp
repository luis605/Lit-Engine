#pragma once

// GPU state of dynamic meshes (skinning, morph targets, procedural deform). Included only by RendererSkinning.cpp.

struct DynamicSourceGpu {
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;
    uint32_t morphCount = 0;
    uint32_t jointCount = 0;
    bool skinned = false;
    glm::vec4 restBounds{0.0f};
    float packedShape = 1.0f;
    std::vector<glm::vec4> normalSamples;
    SkinBounds bounds;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> rest;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> skin;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> morph;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> adjacencyOffsets;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> adjacencyTriangles;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> indices;
};

struct DynamicInstanceGpu {
    uint32_t source = 0;
    uint32_t meshId = 0;
    uint32_t baseVertex = 0;
    uint32_t version = 0;  // bumped whenever the vertices are rewritten (virtual shadow cache invalidation)
    Entity entity = INVALID_ENTITY;
    bool dirty = true;
    bool boundsDirty = true;
    glm::vec4 bounds{0.0f};
    std::vector<glm::mat4> palette;
    std::vector<float> morph;
    DeformSettings deform;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> output;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> paletteBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> morphWeights;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> skinUniforms;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> normalUniforms;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> userUniforms;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> skinSrb;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> normalSrb;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> userSrb;
    std::string userPath;
};

struct SkinningGpu {
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> skinPso;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> normalsPso;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> boundsPso;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> boundsSrb;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> boundsUniforms;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> boundsData;
    uint32_t boundsCapacity = 0;
    size_t stampCapacity = 0;
    std::unordered_map<std::string, Diligent::RefCntAutoPtr<Diligent::IPipelineState>> userPipelines;
    std::vector<DynamicSourceGpu> sources;
    std::vector<DynamicInstanceGpu> instances;
    DynamicMeshStats stats;
    bool failed = false;
};
