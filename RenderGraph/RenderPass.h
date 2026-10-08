#pragma once

// RenderPass: a purely declarative description of one step of rendering.
//
// A pass says WHAT it needs, never HOW it is executed:
//   - named fields (inputs / outputs / in-place outputs) with access and usage
//   - descriptions of the resources it creates
//   - the shaders and samplers it uses
//   - which kind of work it is (graphics / compute / copy)
//   - the minimal fixed-function state of a graphics pipeline, its vertex layout and the
//     size of its per-draw push constants
//   - what it draws, as a function recording draws into a DrawContext (see Draw)
//
// A pass does not know about the RenderGraph, the Renderer, NVRHI or any graphics API.
// Concrete passes usually derive from RenderPass and declare everything in their
// constructor, but a plain RenderPass can be filled in directly as well.

#include <array>
#include <cstddef>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "RenderGraph/DrawContext.h"
#include "RenderGraph/Types.h"
#include "Scene/MeshData.h" // VertexAttribute

namespace LynxEngine
{
    enum class PassType : uint8_t
    {
        Graphics,
        Compute,
        Copy
    };

    enum class FieldAccess : uint8_t
    {
        Read,      // input: needs an incoming edge
        Write,     // output: the pass creates a new resource from the field's description
        ReadWrite  // in-place output: modifies the resource of its incoming edge
    };

    struct PassField
    {
        std::string Name;
        FieldAccess Access = FieldAccess::Read;
        ResourceUsage Usage = ResourceUsage::ShaderResource;
        ResourceKind Kind = ResourceKind::Texture;
        TextureDesc Texture; // used by Write fields of Kind == Texture
        BufferDesc Buffer;   // used by Write fields of Kind == Buffer
        bool Clear = false;  // clear the resource before the pass runs
    };

    // One vertex buffer slot of a graphics pipeline.
    struct VertexStreamLayout
    {
        std::vector<VertexAttribute> Attributes;
        uint32_t Stride = 0;
    };

    struct VertexInputDesc
    {
        bool SceneLayout = false;                // the vertex layout of the scene meshes (Renderer::SetScene)
        std::vector<VertexStreamLayout> Streams; // otherwise one entry per stream; empty = no vertex buffer
    };

    using DrawFunction = std::function<void(DrawContext&)>;

    class RenderPass
    {
    public:
        explicit RenderPass(PassType type = PassType::Graphics);
        virtual ~RenderPass() = default;

        RenderPass(const RenderPass&) = delete;
        RenderPass& operator=(const RenderPass&) = delete;

        // ---- Declaration -------------------------------------------------------------
        // Changing declarations requires the graph to be compiled again.
        // The returned reference is only valid until the next field is declared.

        PassField& Input(std::string name, ResourceUsage usage = ResourceUsage::ShaderResource);
        PassField& InputBuffer(std::string name, ResourceUsage usage = ResourceUsage::ShaderResource);
        PassField& Output(std::string name, const TextureDesc& desc, ResourceUsage usage = ResourceUsage::RenderTarget);
        PassField& OutputBuffer(std::string name, const BufferDesc& desc, ResourceUsage usage = ResourceUsage::UnorderedAccess);
        PassField& InOut(std::string name, ResourceUsage usage = ResourceUsage::RenderTarget);

        void AddShader(ShaderType type, std::string file, std::string entryPoint);
        void AddSampler(const SamplerDesc& desc);

        void SetUsesMaterials(bool uses) { m_UsesMaterials = uses; }
        void SetUsesDrawTexture(bool uses) { m_UsesDrawTexture = uses; } // one SRV picked per draw (DrawContext::SetTexture)
        void SetVertexLayout(std::vector<VertexStreamLayout> streams) { m_VertexInput = { false, std::move(streams) }; }
        void UseSceneVertexLayout() { m_VertexInput = { true, {} }; }
        void SetPushConstantsSize(uint32_t size) { m_PushConstantsSize = size; }
        template <typename T>
        void SetPushConstants() { SetPushConstantsSize(uint32_t(sizeof(T))); }
        void SetDispatchSize(uint32_t x, uint32_t y = 1, uint32_t z = 1) { m_DispatchSize = { x, y, z }; }
        PipelineState& State() { return m_State; }

        // Per-frame parameters (bound as constant buffer b0). Changing them does not
        // require a graph recompile as long as the size stays the same.
        template <typename T>
        void SetConstants(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            m_Constants.resize(sizeof(T));
            std::memcpy(m_Constants.data(), &value, sizeof(T));
        }

        // ---- Drawing ---------------------------------------------------------------------
        // A graphics pass with a vertex shader calls Draw once per frame and records any number
        // of draws. Derived passes override it, plain passes set a function. A graphics pass
        // without shaders draws nothing (e.g. it only clears its targets).
        // Changing the draw function does not require a graph recompile.

        void SetDraw(DrawFunction draw) { m_Draw = std::move(draw); }
        virtual void Draw(DrawContext& context) const
        {
            if (m_Draw)
                m_Draw(context);
        }

        // ---- Description (read by RenderGraph and Renderer) ----------------------------

        PassType GetType() const { return m_Type; }
        bool UsesMaterials() const { return m_UsesMaterials; }
        bool UsesDrawTexture() const { return m_UsesDrawTexture; }
        const VertexInputDesc& GetVertexInput() const { return m_VertexInput; }
        uint32_t GetPushConstantsSize() const { return m_PushConstantsSize; }
        std::array<uint32_t, 3> GetDispatchSize() const { return m_DispatchSize; }
        const PipelineState& GetState() const { return m_State; }

        std::span<const PassField> GetFields() const { return m_Fields; }
        const PassField* FindField(std::string_view name) const;
        PassField* FindField(std::string_view name);
        std::span<const ShaderDesc> GetShaders() const { return m_Shaders; }
        const ShaderDesc* FindShader(ShaderType type) const;
        std::span<const SamplerDesc> GetSamplers() const { return m_Samplers; }
        std::span<const std::byte> GetConstants() const { return m_Constants; }

    private:
        PassField& AddField(PassField field);

        PassType m_Type;
        bool m_UsesMaterials = false;
        bool m_UsesDrawTexture = false;
        VertexInputDesc m_VertexInput;
        uint32_t m_PushConstantsSize = 0;
        DrawFunction m_Draw;
        std::array<uint32_t, 3> m_DispatchSize{ 1, 1, 1 };
        PipelineState m_State;

        std::vector<PassField> m_Fields;
        std::vector<ShaderDesc> m_Shaders;
        std::vector<SamplerDesc> m_Samplers;
        std::vector<std::byte> m_Constants;
    };
}
