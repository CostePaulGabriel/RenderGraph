#pragma once

// Backend-independent types shared by RenderPass, RenderGraph and the Renderer.
// Nothing in here knows about NVRHI, D3D12 or Vulkan. Enums and descriptions follow
// Idea/Types.h, trimmed to what a pass actually needs to describe itself.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "Scene/TextureData.h" // TextureFormat, TextureDimension

namespace LynxEngine
{
    // Logical handle of a graph object (resource, shader, sampler). It is an index into the
    // corresponding table of the CompiledGraph. It never refers to a GPU object directly.
    using RhiHandle = uint32_t;
    inline constexpr RhiHandle kInvalidHandle = ~0u;

    enum class GraphicsAPI : uint8_t
    {
        D3D11,
        D3D12,
        Vulkan
    };

    // ------------------------------------------------------------------------------------
    // Resources
    // ------------------------------------------------------------------------------------

    enum class ResourceKind : uint8_t
    {
        Texture,
        Buffer
    };

    // How a pass uses a resource. This decides the binding slot / attachment and the
    // resource state the renderer transitions to.
    enum class ResourceUsage : uint8_t
    {
        ShaderResource,  // SRV, bound to t0, t1, ... in declaration order
        UnorderedAccess, // UAV, bound to u0, u1, ... in declaration order
        RenderTarget,    // color attachment, SV_Target0, 1, ... in declaration order
        DepthStencil,    // depth attachment, writable
        DepthRead,       // depth attachment, read-only (depth test without writes)
        CopySource,
        CopyDest,
    };

    enum class TextureSizeMode : uint8_t
    {
        BackBufferRelative, // Width/Height = back buffer size * Scale
        Fixed               // Width/Height used as given
    };

    struct TextureDesc
    {
        TextureFormat Format = TextureFormat::RGBA8_UNORM;
        TextureSizeMode SizeMode = TextureSizeMode::BackBufferRelative;
        float Scale = 1.0f;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t MipLevels = 1;
        std::array<float, 4> ClearValue{ 0.0f, 0.0f, 0.0f, 0.0f }; // depth uses ClearValue[0]

        bool operator==(const TextureDesc&) const = default;
    };

    // Structured buffer when Format == Unknown, typed buffer otherwise.
    struct BufferDesc
    {
        uint32_t ElementCount = 0;
        uint32_t ElementSize = 0;
        TextureFormat Format = TextureFormat::Unknown;

        uint64_t ByteSize() const { return uint64_t(ElementCount) * ElementSize; }
        bool operator==(const BufferDesc&) const = default;
    };

    // ------------------------------------------------------------------------------------
    // Shaders and samplers
    // ------------------------------------------------------------------------------------

    enum class ShaderType : uint8_t
    {
        Unknown = 0,
        Vertex,
        Pixel,
        Hull,
        Domain,
        Geometry,
        Amplification,
        Mesh,
        Compute,
        RayGeneration,
        AnyHit,
        ClosestHit,
        Miss,
        Intersection,
        Callable
    };

    struct ShaderDesc
    {
        ShaderType Type = ShaderType::Unknown;
        std::string File;       // relative to the renderer's shader directory, e.g. "Lighting.slang"
        std::string EntryPoint;
        std::string Profile;    // optional, empty = renderer default
        std::string DebugName;

        bool operator==(const ShaderDesc& o) const
        {
            return Type == o.Type && File == o.File && EntryPoint == o.EntryPoint && Profile == o.Profile;
        }
    };

    enum class FilterMode : uint8_t
    {
        Nearest,
        Linear,
        Anisotropic
    };

    enum class AddressMode : uint8_t
    {
        Wrap,
        Mirror,
        Clamp,
        Border,
        MirrorOnce
    };

    struct SamplerDesc
    {
        FilterMode MinFilter = FilterMode::Linear;
        FilterMode MagFilter = FilterMode::Linear;
        FilterMode MipFilter = FilterMode::Linear;
        AddressMode AddressU = AddressMode::Wrap;
        AddressMode AddressV = AddressMode::Wrap;
        AddressMode AddressW = AddressMode::Wrap;

        bool operator==(const SamplerDesc&) const = default;
    };

    // ------------------------------------------------------------------------------------
    // Minimal fixed-function state of a graphics pass
    // ------------------------------------------------------------------------------------

    enum class BlendOp : uint8_t
    {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max
    };

    enum class BlendFactor : uint8_t
    {
        Zero = 1,
        One,
        SrcColor,
        InvSrcColor,
        SrcAlpha,
        InvSrcAlpha,
        DstAlpha,
        InvDstAlpha,
        DstColor,
        InvDstColor,
    };

    enum class RasterCullMode : uint8_t
    {
        Back,
        Front,
        None
    };

    enum class ComparisonFunc : uint8_t
    {
        Never = 1,
        Less = 2,
        Equal = 3,
        LessOrEqual = 4,
        Greater = 5,
        NotEqual = 6,
        GreaterOrEqual = 7,
        Always = 8
    };

    enum class PrimitiveType : uint8_t
    {
        PointList,
        LineList,
        TriangleList,
        TriangleStrip
    };

    // Applied to every render target of the pass.
    struct BlendDesc
    {
        bool Enable = false;
        BlendFactor SrcColor = BlendFactor::One;
        BlendFactor DstColor = BlendFactor::Zero;
        BlendOp ColorOp = BlendOp::Add;
        BlendFactor SrcAlpha = BlendFactor::One;
        BlendFactor DstAlpha = BlendFactor::Zero;
        BlendOp AlphaOp = BlendOp::Add;
    };

    struct DepthDesc
    {
        bool TestEnable = false;
        bool WriteEnable = false;
        ComparisonFunc Func = ComparisonFunc::LessOrEqual;
    };

    struct RasterDesc
    {
        RasterCullMode CullMode = RasterCullMode::None;
        bool FrontCounterClockwise = false;
        bool ScissorEnable = false;
    };

    struct PipelineState
    {
        BlendDesc Blend;
        DepthDesc Depth;
        RasterDesc Raster;
        PrimitiveType Primitive = PrimitiveType::TriangleList;
    };

    // ------------------------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------------------------

    bool IsDepthFormat(TextureFormat format);
    std::string_view ToString(TextureFormat format);
    std::string_view ToString(ResourceUsage usage);
    std::string_view ToString(ShaderType type);
}
