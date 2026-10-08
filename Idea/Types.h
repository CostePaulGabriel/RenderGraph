#pragma once

namespace
{
    using RhiHandle = uint32_t;

    inline constexpr uint32_t kTriangleIndices[] = { 0, 1, 2 };
    inline constexpr uint32_t kQuadIndices[] = { 0, 1, 2, 2, 1, 3 };

    struct  Mesh
    {
        // Just stores the handles
    };

    struct  Material
    {
        // TBD, it should be an materialX material
    };

    enum class GPUVendor : uint32_t
    {
        Unknown = 0,
        NVIDIA = 0x10DE,
        AMD = 0x1002,
        Intel = 0x8086
    };

	enum class GraphicsAPI : uint8_t
	{
		D3D11,
		D3D12,
		Vulkan
	};

    enum class ResourceType : uint8_t
    {
        None,
        Texture_SRV,
        Texture_UAV,
        TypedBuffer_SRV,
        TypedBuffer_UAV,
        StructuredBuffer_SRV,
        StructuredBuffer_UAV,
        RawBuffer_SRV,
        RawBuffer_UAV,
        ConstantBuffer,
        VolatileConstantBuffer,
        Sampler,
        RayTracingAccelStruct,
        PushConstants,
        SamplerFeedbackTexture_UAV
    };

    struct  GpuBufferDesc
    {
        uint64_t ByteSize = 0;
        uint32_t StructStride = 0;
        uint32_t MaxVersions = 0;
        const char* DebugName = nullptr;
        TextureFormat Format = TextureFormat::Unknown;
        bool CanHaveUAVs = false;
        bool CanHaveTypedViews = false;
        bool CanHaveRawViews = false;
        bool IsVertexBuffer = false;
        bool IsIndexBuffer = false;
        bool IsConstantBuffer = false;
        bool IsDrawIndirectArgs = false;
        bool IsShaderBindingTable = false;
        bool IsVolatile = false;
        bool IsVirtual = false;
        bool CanHaveReadCpuAccess = false;
        bool CanHaveWriteCpuAccess = false;
        bool IsSharedResource = false;
    };

    struct  TextureDesc
    {
        std::string DebugName;
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t Depth = 1;
        uint32_t ArraySize = 1;
        uint32_t MipLevels = 1;
        uint32_t SampleCount = 1;
        uint32_t SampleQuality = 0;
        TextureFormat Format = TextureFormat::Unknown;
        TextureDimension Dimension = TextureDimension::Texture2D;
        bool IsShaderResource = true;
        bool IsRenderTarget = false;
        bool IsDepthStencil = false;
        bool IsUAV = false;
        bool IsTypeless = false;
        bool IsShadingRateSurface = false;
        bool IsSharedResource = false;
        bool IsVirtual = false;
        bool IsTiled = false;
        Color ClearValue;
        bool UseClearValue = false;
    };

    struct  TextureSlice
    {
        uint32_t X = 0;
        uint32_t Y = 0;
        uint32_t Z = 0;
        // -1 means the entire dimension is part of the region
        uint32_t Width = uint32_t(-1);
        uint32_t Height = uint32_t(-1);
        uint32_t Depth = uint32_t(-1);
        uint32_t MipLevel = 0;
        uint32_t ArraySlice = 0;
    };

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

    struct  ShaderDesc
    {
        ShaderType Type;
        std::string EntryPoint;
        std::string Profile;
        std::string DebugName;
    };

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
        SrcAlphaSaturate,
        ConstantColor,
        InvConstantColor,
        Src1Color,
        InvSrc1Color,
        Src1Alpha,
        InvSrc1Alpha
    };

    enum class RasterFillMode : uint8_t
    {
        Solid,
        Wireframe
    };

    enum class RasterCullMode : uint8_t
    {
        Back,
        Front,
        None
    };

    enum class StencilOp : uint8_t
    {
        Keep = 1,
        Zero = 2,
        Replace = 3,
        IncrementAndClamp = 4,
        DecrementAndClamp = 5,
        Invert = 6,
        IncrementAndWrap = 7,
        DecrementAndWrap = 8
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
        LineStrip,
        TriangleList,
        TriangleStrip,
        TriangleFan,
        TriangleListWithAdjacency,
        TriangleStripWithAdjacency,
        PatchList
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

    struct  SamplerDesc
    {
        FilterMode MinFilter = FilterMode::Linear;
        FilterMode MagFilter = FilterMode::Linear;
        FilterMode MipFilter = FilterMode::Linear;
        AddressMode AddressU = AddressMode::Wrap;
        AddressMode AddressV = AddressMode::Wrap;
        AddressMode AddressW = AddressMode::Wrap;
    };

    struct  RenderTargetState
    {
        bool BlendEnable = false;
        BlendFactor SrcBlend = BlendFactor::One;
        BlendFactor DestBlend = BlendFactor::Zero;
        BlendOp ColorBlendOp = BlendOp::Add;
        BlendFactor SrcBlendAlpha = BlendFactor::One;
        BlendFactor DestBlendAlpha = BlendFactor::Zero;
        BlendOp BlendOpAlpha = BlendOp::Add;
        ColorMask ColorWriteMask = ColorMask::All;
    };

    struct  BlendState
    {
        std::vector<RenderTargetState> RenderTargets;
        uint32_t pRenderTargetCount = 0;
        bool AlphaToCoverageEnable = false;
    };

    struct  RasterState
    {
        RasterFillMode FillMode = RasterFillMode::Solid;
        RasterCullMode CullMode = RasterCullMode::Back;
        bool FrontCounterClockwise = false;
        bool DepthClipEnable = false;
        bool ScissorEnable = false;
        bool MultisampleEnable = false;
        bool AntialiasedLineEnable = false;
        int DepthBias = 0;
        float DepthBiasClamp = 0.f;
        float SlopeScaledDepthBias = 0.f;
        uint8_t ForcedSampleCount = 0;
        bool ProgrammableSamplePositionsEnable = false;
        bool ConservativeRasterEnable = false;
        bool QuadFillEnable = false;
        char SamplePositionsX[16]{};
        char SamplePositionsY[16]{};
    };

    struct  StencilOpDesc
    {
        StencilOp FailOp = StencilOp::Keep;
        StencilOp DepthFailOp = StencilOp::Keep;
        StencilOp PassOp = StencilOp::Keep;
        ComparisonFunc StencilFunc = ComparisonFunc::Always;
    };

    struct  DepthStencilState
    {
        bool DepthTestEnable = true;
        bool DepthWriteEnable = true;
        ComparisonFunc  DepthFunc = ComparisonFunc::Less;
        bool StencilEnable = false;
        uint8_t StencilReadMask = 0xff;
        uint8_t StencilWriteMask = 0xff;
        uint8_t StencilRefValue = 0;
        bool DynamicStencilRef = false;
        StencilOpDesc FrontFaceStencil;
        StencilOpDesc BackFaceStencil;
    };

    struct  RenderState
    {
        BlendState BlendState;
        DepthStencilState DepthStencilState;
        RasterState RasterState;
    };

    enum class PipelineType : uint8_t
    {
        Compute,
        Graphics
    };

    enum class ScalingMode : uint8_t
    {
        None,
        Stretch,
        MaintainAspectRatio
    };

    enum class SwapMode : uint8_t
    {
        Discard,
        FlipDiscard,
        Sequential,
        FlipSequential
    };

    struct  SwapchainDesc
    {
        std::string DebugName;
        void* pPlatformWindowHandle = nullptr;
        uint32_t Width = 640;
        uint32_t Height = 400;
        uint32_t RefreshRate = 60;
        uint32_t BufferCount = 2;
        TextureFormat Format = TextureFormat::BGRA8_UNORM;
        ScalingMode ScalingMode = ScalingMode::None;
        SwapMode SwapMode = SwapMode::FlipDiscard;
        bool Stereo = false;
        bool Vsync = true;
        bool Tearing = false;
        bool AllowAlpha = false;
        bool AllowResize = true;
    };

    struct  DrawArguments
    {
        uint32_t VertexCount = 0;
        uint32_t InstanceCount = 1;
        uint32_t StartIndexLocation = 0;
        uint32_t StartVertexLocation = 0;
        uint32_t StartInstanceLocation = 0;
    };
}