#pragma once

#include "RendererTypes.hpp"
#include "RendererState.hpp"

struct SurfaceSource {
    std::string shared;
    std::string vertexShared;
    std::string vertexCode;
    std::string fragmentCode;
    bool hasVertex = false;
    bool hasSurface = false;
    float bounds = 0.0f;
    bool twoSided = false;
};

struct NamedBuffer {
    std::string_view name;
    Diligent::IBuffer* buffer;
};

SurfaceSource ParseSurface(const std::string& text);
bool BuildSurface(DiligentData& gpu, DiligentData::SurfaceProgram& program);

Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateStructuredBuffer(Diligent::IRenderDevice* device, const char* name, Diligent::Uint32 elementSize, Diligent::Uint32 elementCount, void* initialData = nullptr, Diligent::BIND_FLAGS extraFlags = Diligent::BIND_NONE);
Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateDispatchArgsBuffer(Diligent::IRenderDevice* device, const char* name, uint32_t count = 1);
Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateIndirectBuffer(Diligent::IRenderDevice* device, const char* name, size_t size);
Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateUniformBuffer(Diligent::IRenderDevice* device, const char* name, Diligent::Uint64 size, Diligent::USAGE usage = Diligent::USAGE_DYNAMIC);
Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateVertexBuffer(Diligent::IRenderDevice* device, size_t size);
Diligent::RefCntAutoPtr<Diligent::IBuffer> CreateIndexBuffer(Diligent::IRenderDevice* device, size_t size);
Diligent::RefCntAutoPtr<Diligent::IShader> CompileShader(Diligent::IRenderDevice* device, const std::string& path, Diligent::SHADER_TYPE type, const Diligent::ShaderMacroArray& macros = {});
Diligent::RefCntAutoPtr<Diligent::IPipelineState> CreateComputePipeline(Diligent::IRenderDevice* device, const char* name, const std::string& path, std::initializer_list<std::string_view> dynamicVariables = {}, const Diligent::ShaderMacroArray& macros = {});
std::vector<Diligent::ShaderResourceVariableDesc> DescribeVariables(std::initializer_list<Diligent::IShader*> shaders, std::initializer_list<std::string_view> dynamicVariables = {});
void UseVariables(Diligent::PipelineResourceLayoutDesc& layout, const std::vector<Diligent::ShaderResourceVariableDesc>& variables);
void CreateBindings(Diligent::IPipelineState* pipeline, std::span<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>> bindings);
void CreateBoundSRB(Diligent::IPipelineState* pipeline, Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& binding, std::initializer_list<NamedBuffer> buffers);
void SetDynamicVariable(Diligent::IShaderResourceBinding* binding, const char* name, Diligent::IDeviceObject* object, Diligent::SHADER_TYPE stage = Diligent::SHADER_TYPE_COMPUTE);
bool AllShadersCompile(Diligent::IRenderDevice* device);
void PrefetchShaders(Diligent::IRenderDevice* device);
Diligent::Uint32 SelectFastestAdapter(Diligent::IEngineFactory* factory, Diligent::Version minVersion);
uint8_t PackAlpha(float alpha);
uint32_t PackRenderable(const RenderableComponent& renderable);
void ClearCounter(Diligent::IDeviceContext* context, Diligent::IBuffer* buffer);
void UnbindRenderTargets(Diligent::IDeviceContext* context);
void SetSwapChainViewport(DiligentData& gpu);
void SetSceneViewport(DiligentData& gpu);
void DispatchSingleGroup(Diligent::IDeviceContext* context, Diligent::IPipelineState* pipeline, Diligent::IShaderResourceBinding* binding);
void DrawFullscreenTriangle(Diligent::IDeviceContext* context);
bool CreateHiZSet(DiligentData& gpu, HiZSet& set, int width, int height, int mips);
void BuildHiZ(DiligentData& gpu, Diligent::ITexture* hiz, Diligent::RefCntAutoPtr<Diligent::ITextureView>* source, Diligent::RefCntAutoPtr<Diligent::ITextureView>* dest, int width, int height, int mips);
Diligent::IPipelineState* SurfacePipeline(const DiligentData::SurfaceProgram& surface, int phaseIndex, bool points);
std::vector<Diligent::IPipelineState*> SurfacePipelines(const DiligentData& gpu, int phaseIndex, bool points);
void ReadGpuTimings(DiligentData& gpu, int frame);
void LogGpuTimings(const DiligentData& gpu);
