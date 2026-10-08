#pragma once

// Renderer: the execution layer. It turns a CompiledGraph into NVRHI objects and runs it.
//
//   RenderPass  -> what a step needs          (declarative)
//   RenderGraph -> which steps, in what order (logical)
//   Renderer    -> how each step is executed  (NVRHI, through a DeviceBackend)
//
// Binding convention used for every pass (register space 0):
//   b0  pass constants (RenderPass::SetConstants), if any
//   b1  frame constants (Scene::MainCamera matrices, time, resolution; see Shaders/Common.slang)
//   b2  per-draw push constants (RenderPass::SetPushConstants<T>, DrawContext::SetPushConstants)
//   t#  ShaderResource fields in declaration order, then the draw texture (SetUsesDrawTexture)
//   u#  UnorderedAccess fields in declaration order
//   s#  samplers in declaration order
// Passes with UsesMaterials() also get the scene material table:
//   space1 t0  StructuredBuffer<MaterialConstants> (PBR, see Shaders/Scene.slang)
//   space2 t0+ Texture2D[] (bindless, slots handed out by DescriptorTableManager; materials
//              store these indices instead of binding textures)
//
// Graphics passes record their draws through a DrawContext implemented here (RenderPass::Draw).
// This header does not expose NVRHI or any graphics API type.

#include <filesystem>
#include <memory>
#include <string>

#include "RenderGraph/RenderGraph.h"
#include "Scene/Scene.h"

namespace LynxEngine
{
    struct RendererDesc
    {
        void* WindowHandle = nullptr;
        uint32_t Width = 0;
        uint32_t Height = 0;
        GraphicsAPI Api = GraphicsAPI::D3D12;
        bool Debug = false;
        bool VSync = true;
        std::filesystem::path ShaderDirectory;
    };

    struct FrameInfo
    {
        float Time = 0.0f;
        float DeltaTime = 0.0f;
        uint32_t FrameIndex = 0;
    };

    class Renderer
    {
    public:
        Renderer();
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        // ImGui must have a context before Init (the font atlas is uploaded here).
        bool Init(const RendererDesc& desc);
        void Shutdown();

        // Releases the graph's GPU objects; compile the graph again and call SetGraph afterwards.
        void Resize(uint32_t width, uint32_t height);

        uint32_t GetWidth() const;
        uint32_t GetHeight() const;
        TextureFormat GetBackBufferFormat() const;
        const char* GetBackendName() const;

        // Uploads meshes, textures and materials. Call before SetGraph.
        bool SetScene(const Scene& scene);

        // Creates resources, shaders, pipelines and bindings for every compiled pass.
        // The graph is copied; the RenderPass objects it points to must stay alive.
        bool SetGraph(const CompiledGraph& graph);

        // Executes the compiled passes in order and presents the graph output.
        void Render(const Scene& scene, const FrameInfo& frame);

        const std::string& GetLastError() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
