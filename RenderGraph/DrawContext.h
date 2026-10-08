#pragma once

// DrawContext: what a graphics pass records its draws with (see RenderPass::Draw).
//
// The pass decides WHAT is drawn (a fullscreen triangle, the scene, ImGui, a debug line list,
// procedural geometry, ...). The renderer implements this interface and decides HOW it reaches
// the GPU. No graphics API type appears here.
//
// Everything set on the context (geometry, push constants, texture, scissor) stays set until it
// is changed again or the pass ends; each pass starts with a fresh context.

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "Scene/MeshData.h" // IndexFormat

namespace LynxEngine
{
    struct Scene;

    // Opaque texture handle given out by the renderer (e.g. the ImGui font atlas TexID).
    using TextureId = uint64_t;

    struct DrawArgs
    {
        uint32_t VertexCount = 0;
        uint32_t InstanceCount = 1;
        uint32_t FirstVertex = 0;
        uint32_t FirstInstance = 0;
    };

    struct DrawIndexedArgs
    {
        uint32_t IndexCount = 0;
        uint32_t InstanceCount = 1;
        uint32_t FirstIndex = 0;
        int32_t BaseVertex = 0;
        uint32_t FirstInstance = 0;
    };

    // In pixels of the pass's render targets. Only applied when State().Raster.ScissorEnable is set.
    struct ScissorRect
    {
        int32_t MinX = 0;
        int32_t MinY = 0;
        int32_t MaxX = 0;
        int32_t MaxY = 0;
    };

    class DrawContext
    {
    public:
        // ---- Frame information ------------------------------------------------------------

        virtual const Scene& GetScene() const = 0;
        virtual uint32_t GetTargetWidth() const = 0;
        virtual uint32_t GetTargetHeight() const = 0;

        // ---- Geometry ---------------------------------------------------------------------
        // Vertex streams must match the pass's vertex layout (RenderPass::SetVertexLayout or
        // UseSceneVertexLayout). Drawing without any vertex stream is valid (vertex pulling,
        // SV_VertexID based geometry such as the fullscreen triangle).

        // Binds the vertex and index buffers of Scene::Meshes[mesh] (uploaded by Renderer::SetScene).
        virtual void BindMesh(uint32_t mesh) = 0;

        // Copies CPU data into transient GPU buffers that live for this frame.
        virtual void SetVertexData(uint32_t stream, std::span<const std::byte> data) = 0;
        virtual void SetIndexData(std::span<const std::byte> data, IndexFormat format) = 0;

        // ---- State ------------------------------------------------------------------------

        // Per-draw constants (b2). The pass declares their size with RenderPass::SetPushConstants<T>().
        virtual void SetPushConstants(std::span<const std::byte> data) = 0;

        // The per-draw texture of a pass that declared SetUsesDrawTexture(true).
        virtual void SetTexture(TextureId texture) = 0;

        virtual void SetScissor(const ScissorRect& rect) = 0;
        virtual void ResetScissor() = 0; // scissor = the whole render target

        // ---- Draws ------------------------------------------------------------------------

        virtual void Draw(const DrawArgs& args) = 0;
        virtual void DrawIndexed(const DrawIndexedArgs& args) = 0;

        // ---- Helpers ----------------------------------------------------------------------

        template <typename T>
        void SetPushConstants(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            SetPushConstants(std::as_bytes(std::span(&value, 1)));
        }

        // 3 vertices, no vertex buffer (see Shaders/Fullscreen.slang).
        void DrawFullscreenTriangle() { Draw({ .VertexCount = 3 }); }

    protected:
        ~DrawContext() = default;
    };
}
