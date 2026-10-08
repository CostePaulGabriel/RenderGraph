#pragma once

// RenderGraph: the logical description of a frame.
//
// It owns the passes, connects pass fields with edges ("Pass.field" -> "Pass.field"),
// and compiles everything into a CompiledGraph that answers:
//   - which passes run, and in which order
//   - which resources exist and who reads / writes them
//   - which passes depend on each other
//   - which shaders and samplers are needed
//
// The graph never creates GPU objects. The Renderer consumes the CompiledGraph.

#include <concepts>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "RenderGraph/RenderPass.h"

namespace LynxEngine
{
    struct GraphEdge
    {
        std::string SrcPass;
        std::string SrcField;
        std::string DstPass;
        std::string DstField;
    };

    struct GraphCompileDesc
    {
        uint32_t BackBufferWidth = 0;
        uint32_t BackBufferHeight = 0;
        TextureFormat BackBufferFormat = TextureFormat::RGBA8_UNORM;
    };

    struct CompiledResource
    {
        RhiHandle Handle = kInvalidHandle;
        std::string Name;        // "Producer.field"
        ResourceKind Kind = ResourceKind::Texture;
        TextureDesc Texture;     // Width/Height resolved
        BufferDesc Buffer;
        uint32_t UsageMask = 0;  // bit (1 << ResourceUsage) for every usage in the graph
        bool IsGraphOutput = false; // presented: the renderer maps it onto the back buffer
        uint32_t FirstPass = 0;  // lifetime, as indices into CompiledGraph::Passes
        uint32_t LastPass = 0;

        bool HasUsage(ResourceUsage usage) const { return (UsageMask & (1u << uint32_t(usage))) != 0; }
    };

    struct CompiledBinding
    {
        std::string Field;
        RhiHandle Resource = kInvalidHandle;
        FieldAccess Access = FieldAccess::Read;
        ResourceUsage Usage = ResourceUsage::ShaderResource;
        bool Clear = false;
    };

    struct CompiledPass
    {
        std::string Name;
        const RenderPass* Pass = nullptr;      // owned by the RenderGraph
        std::vector<CompiledBinding> Bindings; // same order as the pass fields
        std::vector<RhiHandle> Shaders;        // indices into CompiledGraph::Shaders
        std::vector<RhiHandle> Samplers;       // indices into CompiledGraph::Samplers
        std::vector<uint32_t> DependsOn;       // indices into CompiledGraph::Passes (always earlier)
    };

    struct CompiledGraph
    {
        std::vector<CompiledPass> Passes;         // execution order
        std::vector<CompiledResource> Resources;  // index == RhiHandle
        std::vector<ShaderDesc> Shaders;          // index == RhiHandle, deduplicated
        std::vector<SamplerDesc> Samplers;        // index == RhiHandle, deduplicated
        RhiHandle Output = kInvalidHandle;
        uint32_t Width = 0;
        uint32_t Height = 0;
        std::vector<std::string> CulledPasses;    // passes that do not contribute to the output
        std::vector<std::string> Warnings;
    };

    class RenderGraph
    {
    public:
        RenderGraph() = default;
        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;

        RenderPass& AddPass(std::string name, std::unique_ptr<RenderPass> pass);

        template <std::derived_from<RenderPass> T, typename... Args>
        T& AddPass(std::string name, Args&&... args)
        {
            return static_cast<T&>(AddPass(std::move(name), std::make_unique<T>(std::forward<Args>(args)...)));
        }

        bool RemovePass(std::string_view name); // also removes its edges
        RenderPass* GetPass(std::string_view name) const;

        template <std::derived_from<RenderPass> T>
        T* GetPass(std::string_view name) const { return dynamic_cast<T*>(GetPass(name)); }

        // Connect an output field to an input field: AddEdge("GBuffer.albedo", "Lighting.albedo").
        void AddEdge(std::string_view src, std::string_view dst);
        bool RemoveEdge(std::string_view dst);

        // The field whose resource ends up on the back buffer.
        void MarkOutput(std::string_view field);

        std::expected<CompiledGraph, std::string> Compile(const GraphCompileDesc& desc) const;

        std::span<const GraphEdge> GetEdges() const { return m_Edges; }
        std::vector<std::string> GetPassNames() const;

    private:
        struct PassEntry
        {
            std::string Name;
            std::unique_ptr<RenderPass> Pass;
        };

        int FindPassIndex(std::string_view name) const;

        std::vector<PassEntry> m_Passes;
        std::vector<GraphEdge> m_Edges;
        std::string m_OutputPass;
        std::string m_OutputField;
    };

    // Human readable summary (execution order, bindings, resources, culled passes).
    std::string DescribeGraph(const CompiledGraph& graph);
}
