#include "Renderer/Renderer.h"

#include "Backends/Backend.h"
#include "Renderer/DescriptorTableManager.h"
#include "Renderer/ShaderCompiler.h"

#include <imgui.h>
#include <nvrhi/nvrhi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <functional>
#include <map>
#include <span>

namespace LynxEngine
{
    namespace
    {
        // ---- GPU-side structures (must match Shaders/Common.slang) ------------------------

        struct FrameConstants
        {
            float4x4 View;
            float4x4 Projection;
            float4x4 ViewProjection;
            float4x4 InverseViewProjection;
            float3 CameraPosition;
            float Time = 0.0f;
            float2 Resolution;
            float2 InvResolution;
            float DeltaTime = 0.0f;
            uint32_t FrameIndex = 0;
            float Padding[2] = {};
        };
        static_assert(sizeof(FrameConstants) % 16 == 0);

        // MaterialData as the shaders see it (MaterialConstants in Shaders/Scene.slang). Texture
        // fields are bindless descriptor indices (DescriptorTableManager), kNoTexture when unused.
        struct GpuMaterial
        {
            float BaseColor[4];
            float Emissive[3];
            float Metallic;
            float Roughness;
            float NormalScale;
            float OcclusionStrength;
            uint32_t BaseColorTexture;
            uint32_t MetallicRoughnessTexture;
            uint32_t NormalTexture;
            uint32_t OcclusionTexture;
            uint32_t EmissiveTexture;
        };
        static_assert(sizeof(GpuMaterial) == 64);

        constexpr uint32_t kPassConstantsSlot = 0;
        constexpr uint32_t kFrameConstantsSlot = 1;
        constexpr uint32_t kPushConstantsSlot = 2;
        constexpr uint32_t kMaterialSpace = 1;
        constexpr uint32_t kBindlessSpace = 2;
        constexpr uint32_t kMaxBindlessTextures = 1024;

        // ---- Engine -> NVRHI conversions -----------------------------------------------------
        // TextureFormat mirrors nvrhi::Format one to one (see Idea/TextureData.h).

        static_assert(uint8_t(TextureFormat::RGBA8_UNORM) == uint8_t(nvrhi::Format::RGBA8_UNORM));
        static_assert(uint8_t(TextureFormat::RGBA16_FLOAT) == uint8_t(nvrhi::Format::RGBA16_FLOAT));
        static_assert(uint8_t(TextureFormat::D32) == uint8_t(nvrhi::Format::D32));
        static_assert(uint8_t(TextureFormat::BC7_UNORM_SRGB) == uint8_t(nvrhi::Format::BC7_UNORM_SRGB));

        nvrhi::Format ToNvrhi(TextureFormat format) { return static_cast<nvrhi::Format>(format); }
        TextureFormat FromNvrhi(nvrhi::Format format) { return static_cast<TextureFormat>(format); }

        nvrhi::ShaderType ToNvrhi(ShaderType type)
        {
            switch (type)
            {
            case ShaderType::Vertex:        return nvrhi::ShaderType::Vertex;
            case ShaderType::Pixel:         return nvrhi::ShaderType::Pixel;
            case ShaderType::Hull:          return nvrhi::ShaderType::Hull;
            case ShaderType::Domain:        return nvrhi::ShaderType::Domain;
            case ShaderType::Geometry:      return nvrhi::ShaderType::Geometry;
            case ShaderType::Amplification: return nvrhi::ShaderType::Amplification;
            case ShaderType::Mesh:          return nvrhi::ShaderType::Mesh;
            case ShaderType::Compute:       return nvrhi::ShaderType::Compute;
            default:                        return nvrhi::ShaderType::None;
            }
        }

        static_assert(uint8_t(BlendFactor::InvDstColor) == uint8_t(nvrhi::BlendFactor::InvDstColor));
        nvrhi::BlendFactor ToNvrhi(BlendFactor factor) { return static_cast<nvrhi::BlendFactor>(factor); }
        nvrhi::BlendOp ToNvrhi(BlendOp op) { return static_cast<nvrhi::BlendOp>(uint8_t(op) + 1); }
        nvrhi::ComparisonFunc ToNvrhi(ComparisonFunc func) { return static_cast<nvrhi::ComparisonFunc>(func); }
        nvrhi::RasterCullMode ToNvrhi(RasterCullMode mode) { return static_cast<nvrhi::RasterCullMode>(mode); }

        nvrhi::PrimitiveType ToNvrhi(PrimitiveType type)
        {
            switch (type)
            {
            case PrimitiveType::PointList:     return nvrhi::PrimitiveType::PointList;
            case PrimitiveType::LineList:      return nvrhi::PrimitiveType::LineList;
            case PrimitiveType::TriangleStrip: return nvrhi::PrimitiveType::TriangleStrip;
            default:                           return nvrhi::PrimitiveType::TriangleList;
            }
        }

        nvrhi::SamplerAddressMode ToNvrhi(AddressMode mode)
        {
            switch (mode)
            {
            case AddressMode::Mirror:     return nvrhi::SamplerAddressMode::Mirror;
            case AddressMode::Clamp:      return nvrhi::SamplerAddressMode::Clamp;
            case AddressMode::Border:     return nvrhi::SamplerAddressMode::Border;
            case AddressMode::MirrorOnce: return nvrhi::SamplerAddressMode::MirrorOnce;
            default:                      return nvrhi::SamplerAddressMode::Wrap;
            }
        }

        nvrhi::SamplerDesc ToNvrhi(const SamplerDesc& desc)
        {
            nvrhi::SamplerDesc result;
            result.minFilter = desc.MinFilter != FilterMode::Nearest;
            result.magFilter = desc.MagFilter != FilterMode::Nearest;
            result.mipFilter = desc.MipFilter != FilterMode::Nearest;
            result.maxAnisotropy = (desc.MinFilter == FilterMode::Anisotropic || desc.MagFilter == FilterMode::Anisotropic) ? 16.0f : 1.0f;
            result.addressU = ToNvrhi(desc.AddressU);
            result.addressV = ToNvrhi(desc.AddressV);
            result.addressW = ToNvrhi(desc.AddressW);
            return result;
        }

        nvrhi::Format ToNvrhi(VertexFormat format)
        {
            switch (format)
            {
            case VertexFormat::Float32:           return nvrhi::Format::R32_FLOAT;
            case VertexFormat::Float32x2:         return nvrhi::Format::RG32_FLOAT;
            case VertexFormat::Float32x3:         return nvrhi::Format::RGB32_FLOAT;
            case VertexFormat::Float32x4:         return nvrhi::Format::RGBA32_FLOAT;
            case VertexFormat::Float16:           return nvrhi::Format::R16_FLOAT;
            case VertexFormat::Float16x2:         return nvrhi::Format::RG16_FLOAT;
            case VertexFormat::Float16x4:         return nvrhi::Format::RGBA16_FLOAT;
            case VertexFormat::Int32:             return nvrhi::Format::R32_SINT;
            case VertexFormat::Int32x2:           return nvrhi::Format::RG32_SINT;
            case VertexFormat::Int32x3:           return nvrhi::Format::RGB32_SINT;
            case VertexFormat::Int32x4:           return nvrhi::Format::RGBA32_SINT;
            case VertexFormat::UInt32:            return nvrhi::Format::R32_UINT;
            case VertexFormat::UInt32x2:          return nvrhi::Format::RG32_UINT;
            case VertexFormat::UInt32x3:          return nvrhi::Format::RGB32_UINT;
            case VertexFormat::UInt32x4:          return nvrhi::Format::RGBA32_UINT;
            case VertexFormat::UInt8x4:           return nvrhi::Format::RGBA8_UINT;
            case VertexFormat::UInt8x4Normalized: return nvrhi::Format::RGBA8_UNORM;
            case VertexFormat::Int8x4:            return nvrhi::Format::RGBA8_SINT;
            case VertexFormat::Int8x4Normalized:  return nvrhi::Format::RGBA8_SNORM;
            default:                              return nvrhi::Format::UNKNOWN;
            }
        }

        const char* SemanticName(VertexSemantic semantic)
        {
            switch (semantic)
            {
            case VertexSemantic::Position:    return "POSITION";
            case VertexSemantic::Normal:      return "NORMAL";
            case VertexSemantic::Tangent:     return "TANGENT";
            case VertexSemantic::Bitangent:   return "BINORMAL";
            case VertexSemantic::TexCoord:    return "TEXCOORD";
            case VertexSemantic::TexIndex:    return "TEXINDEX";
            case VertexSemantic::Color:       return "COLOR";
            case VertexSemantic::BlendIndice: return "BLENDINDICES";
            case VertexSemantic::BlendWeight: return "BLENDWEIGHT";
            default:                          return "UNKNOWN";
            }
        }

        bool IsAttachment(ResourceUsage usage)
        {
            return usage == ResourceUsage::RenderTarget || usage == ResourceUsage::DepthStencil || usage == ResourceUsage::DepthRead;
        }

        class ConsoleMessageCallback final : public nvrhi::IMessageCallback
        {
        public:
            void message(nvrhi::MessageSeverity severity, const char* text) override
            {
                const char* label = severity == nvrhi::MessageSeverity::Info ? "info"
                    : severity == nvrhi::MessageSeverity::Warning ? "warning" : "error";
                std::fprintf(severity == nvrhi::MessageSeverity::Info ? stdout : stderr, "[NVRHI %s] %s\n", label, text);
            }
        };
    }

    // ============================================================================================

    struct Renderer::Impl
    {
        struct GpuMesh
        {
            std::vector<nvrhi::BufferHandle> VertexBuffers; // one per VertexStream
            nvrhi::BufferHandle IndexBuffer;
            nvrhi::Format IndexFormat = nvrhi::Format::R32_UINT;
            std::vector<SubMesh> SubMeshes;
        };

        struct GpuResource
        {
            nvrhi::TextureHandle Texture;
            nvrhi::BufferHandle Buffer;
        };

        // Grows as needed; data written during a frame is appended, the offset resets every frame.
        struct TransientBuffer
        {
            nvrhi::BufferHandle Buffer;
            uint64_t Capacity = 0;
            uint64_t Offset = 0;
        };

        struct GpuPass
        {
            nvrhi::BindingLayoutHandle Layout;
            std::vector<nvrhi::BindingSetHandle> BindingSets;   // one, or one per back buffer (none with a draw texture)
            std::vector<nvrhi::FramebufferHandle> Framebuffers; // one, or one per back buffer
            std::map<std::pair<nvrhi::ITexture*, uint32_t>, nvrhi::BindingSetHandle> DrawTextureSets; // (texture, back buffer variant)
            bool PerBackBufferBindings = false;
            TransientBuffer TransientVertices;
            TransientBuffer TransientIndices;
            nvrhi::GraphicsPipelineHandle GraphicsPipeline;
            nvrhi::ComputePipelineHandle ComputePipeline;
            nvrhi::BufferHandle Constants;
            uint32_t ConstantsSize = 0;
        };

        RendererDesc Desc;
        ConsoleMessageCallback Messages;
        std::unique_ptr<DeviceBackend> Backend;
        nvrhi::DeviceHandle Device;
        nvrhi::CommandListHandle CommandList;
        std::unique_ptr<ShaderCompiler> Compiler;
        std::string LastError;

        // Shared by all passes.
        nvrhi::BufferHandle FrameConstantBuffer;
        nvrhi::BindingLayoutHandle MaterialLayout;
        nvrhi::BindingLayoutHandle BindlessLayout;
        std::shared_ptr<DescriptorTableManager> Bindless; // space2: every scene texture

        // Scene.
        std::vector<GpuMesh> Meshes;
        std::vector<nvrhi::TextureHandle> SceneTextures;
        std::vector<DescriptorHandle> SceneTextureDescriptors; // index == Scene::Textures index
        nvrhi::BufferHandle MaterialBuffer;
        nvrhi::BindingSetHandle MaterialSet;
        std::vector<nvrhi::VertexAttributeDesc> SceneVertexLayout;
        bool HasScene = false;

        // ImGui font atlas (its TexID is the nvrhi::ITexture*, see DrawContext::SetTexture).
        nvrhi::TextureHandle FontTexture;

        // Compiled graph and its GPU objects.
        CompiledGraph Graph;
        std::vector<GpuResource> Resources;     // index == RhiHandle
        std::vector<nvrhi::ShaderHandle> Shaders;
        std::vector<nvrhi::SamplerHandle> Samplers;
        std::vector<GpuPass> Passes;
        std::map<std::string, nvrhi::ShaderHandle> ShaderCache; // survives graph rebuilds
        bool GraphReady = false;

        // ------------------------------------------------------------------------------------

        bool Fail(std::string message)
        {
            std::fprintf(stderr, "[Renderer] %s\n", message.c_str());
            LastError = std::move(message);
            return false;
        }

        uint32_t BackBufferCount() const { return Backend->GetBackBufferCount(); }

        nvrhi::ITexture* ResolveTexture(RhiHandle handle, uint32_t backBufferIndex) const
        {
            if (Graph.Resources[handle].IsGraphOutput)
                return Backend->GetBackBuffer(backBufferIndex);
            return Resources[handle].Texture;
        }

        bool UsesOutput(const CompiledPass& pass, bool attachments) const
        {
            for (const CompiledBinding& binding : pass.Bindings)
            {
                if (Graph.Resources[binding.Resource].IsGraphOutput && IsAttachment(binding.Usage) == attachments)
                    return true;
            }
            return false;
        }

        nvrhi::IShader* FindShader(const CompiledPass& pass, ShaderType type) const
        {
            for (const RhiHandle handle : pass.Shaders)
            {
                if (Graph.Shaders[handle].Type == type)
                    return Shaders[handle];
            }
            return nullptr;
        }

        void Submit(const std::function<void(nvrhi::ICommandList*)>& record)
        {
            CommandList->open();
            record(CommandList);
            CommandList->close();
            Device->executeCommandList(CommandList);
            Device->waitForIdle();
        }

        // ---- Init / shutdown ---------------------------------------------------------------

        bool Init(const RendererDesc& desc)
        {
            Desc = desc;

            BackendDesc backendDesc;
            backendDesc.WindowHandle = desc.WindowHandle;
            backendDesc.Width = desc.Width;
            backendDesc.Height = desc.Height;
            backendDesc.Debug = desc.Debug;
            backendDesc.VSync = desc.VSync;
            backendDesc.MessageCallback = &Messages;

            switch (desc.Api)
            {
            case GraphicsAPI::D3D12:  Backend = CreateD3D12Backend(backendDesc); break;
            case GraphicsAPI::Vulkan: Backend = CreateVulkanBackend(backendDesc); break;
            default: break;
            }
            if (!Backend)
                return Fail("Could not create the graphics backend");

            Device = Backend->GetDevice();
            CommandList = Device->createCommandList();

            Compiler = std::make_unique<ShaderCompiler>(desc.Api);
            if (!Compiler->IsValid())
                return Fail("Could not initialize the Slang shader compiler");

            nvrhi::BufferDesc frameDesc;
            frameDesc.byteSize = sizeof(FrameConstants);
            frameDesc.isConstantBuffer = true;
            frameDesc.isVolatile = true;
            frameDesc.maxVersions = 64;
            frameDesc.debugName = "FrameConstants";
            FrameConstantBuffer = Device->createBuffer(frameDesc);

            nvrhi::BindingLayoutDesc materialLayout;
            materialLayout.visibility = nvrhi::ShaderType::AllGraphics;
            materialLayout.registerSpace = kMaterialSpace;
            materialLayout.bindings = { nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0) };
            MaterialLayout = Device->createBindingLayout(materialLayout);

            nvrhi::BindlessLayoutDesc bindlessLayout;
            bindlessLayout.visibility = nvrhi::ShaderType::AllGraphics;
            bindlessLayout.firstSlot = 0;
            bindlessLayout.maxCapacity = kMaxBindlessTextures;
            bindlessLayout.registerSpaces = { nvrhi::BindingLayoutItem::Texture_SRV(kBindlessSpace) };
            BindlessLayout = Device->createBindlessLayout(bindlessLayout);
            Bindless = std::make_shared<DescriptorTableManager>(Device, BindlessLayout);

            CreateFontTexture();

            std::printf("[Renderer] %s backend, %ux%u\n", Backend->GetName(), desc.Width, desc.Height);
            return true;
        }

        void CreateFontTexture()
        {
            if (!ImGui::GetCurrentContext())
                return;

            ImGuiIO& io = ImGui::GetIO();
            io.BackendRendererName = "LynxEngine RenderGraph (NVRHI)";
            io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

            unsigned char* pixels = nullptr;
            int width = 0, height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

            nvrhi::TextureDesc desc;
            desc.width = uint32_t(width);
            desc.height = uint32_t(height);
            desc.format = nvrhi::Format::RGBA8_UNORM;
            desc.debugName = "ImGuiFont";
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.keepInitialState = true;
            FontTexture = Device->createTexture(desc);

            Submit([&](nvrhi::ICommandList* cmd) { cmd->writeTexture(FontTexture, 0, 0, pixels, size_t(width) * 4); });
            io.Fonts->SetTexID(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(FontTexture.Get())));
        }

        void Shutdown()
        {
            if (Device)
                Device->waitForIdle();

            ReleaseGraph();
            ShaderCache.clear();
            Meshes.clear();
            SceneTextureDescriptors.clear(); // before the manager they point into
            SceneTextures.clear();
            MaterialBuffer = nullptr;
            MaterialSet = nullptr;
            FontTexture = nullptr;
            Bindless.reset();
            BindlessLayout = nullptr;
            MaterialLayout = nullptr;
            FrameConstantBuffer = nullptr;
            CommandList = nullptr;
            Device = nullptr;
            Backend.reset();

            if (ImGui::GetCurrentContext())
            {
                ImGui::GetIO().Fonts->SetTexID(ImTextureID{});
                ImGui::GetIO().BackendRendererName = nullptr;
            }
        }

        void Resize(uint32_t width, uint32_t height)
        {
            if (!Backend || width == 0 || height == 0)
                return;
            Device->waitForIdle();
            ReleaseGraph(); // framebuffers reference the old back buffers
            // ResizeBuffers fails while anything still references a back buffer: NVRHI keeps the
            // last frame's resources alive until garbage collection runs after the GPU is done.
            Device->runGarbageCollection();
            Backend->Resize(width, height);
            Desc.Width = width;
            Desc.Height = height;
        }

        // ---- Scene ------------------------------------------------------------------------

        // Scene texture index -> bindless descriptor index.
        uint32_t ToDescriptor(uint32_t sceneTexture) const
        {
            if (sceneTexture == kNoTexture)
                return kNoTexture;
            if (sceneTexture >= SceneTextureDescriptors.size())
            {
                std::fprintf(stderr, "[Renderer] Material references texture %u, the scene has %zu; ignored\n", sceneTexture, SceneTextureDescriptors.size());
                return kNoTexture;
            }
            return SceneTextureDescriptors[sceneTexture].Get();
        }

        GpuMaterial ToGpuMaterial(const MaterialData& material) const
        {
            GpuMaterial gpu{};
            std::copy(std::begin(material.BaseColor), std::end(material.BaseColor), gpu.BaseColor);
            std::copy(std::begin(material.Emissive), std::end(material.Emissive), gpu.Emissive);
            gpu.Metallic = material.Metallic;
            gpu.Roughness = material.Roughness;
            gpu.NormalScale = material.NormalScale;
            gpu.OcclusionStrength = material.OcclusionStrength;
            gpu.BaseColorTexture = ToDescriptor(material.BaseColorTexture);
            gpu.MetallicRoughnessTexture = ToDescriptor(material.MetallicRoughnessTexture);
            gpu.NormalTexture = ToDescriptor(material.NormalTexture);
            gpu.OcclusionTexture = ToDescriptor(material.OcclusionTexture);
            gpu.EmissiveTexture = ToDescriptor(material.EmissiveTexture);
            return gpu;
        }

        bool SetScene(const Scene& scene)
        {
            Device->waitForIdle();
            Meshes.clear();
            SceneTextureDescriptors.clear(); // frees the bindless slots of the previous scene
            SceneTextures.clear();
            SceneVertexLayout.clear();
            HasScene = false;

            if (scene.Meshes.empty())
                return Fail("The scene has no meshes");
            if (scene.Textures.size() > kMaxBindlessTextures)
                return Fail("Too many scene textures");

            // Vertex layout of the scene: taken from the first mesh, all meshes must match.
            const MeshData& first = scene.Meshes.front();
            for (uint32_t s = 0; s < uint32_t(first.VertexStreams.size()); ++s)
            {
                const VertexStream& stream = first.VertexStreams[s];
                for (const VertexAttribute& attribute : stream.Attributes)
                {
                    nvrhi::VertexAttributeDesc desc;
                    desc.name = SemanticName(attribute.Semantic);
                    desc.format = ToNvrhi(attribute.Format);
                    desc.bufferIndex = s;
                    desc.offset = attribute.Offset;
                    desc.elementStride = stream.Stride;
                    if (attribute.Index != 0)
                        std::fprintf(stderr, "[Renderer] Semantic index %u of %s is not supported, skipped\n", attribute.Index, desc.name.c_str());
                    else
                        SceneVertexLayout.push_back(desc);
                }
            }

            for (const MeshData& mesh : scene.Meshes)
            {
                if (!mesh.IsValid() || mesh.VertexStreams.size() != first.VertexStreams.size())
                    return Fail("Invalid mesh, or a mesh whose vertex streams differ from the first mesh");
                for (size_t s = 0; s < mesh.VertexStreams.size(); ++s)
                {
                    if (mesh.VertexStreams[s].Stride != first.VertexStreams[s].Stride)
                        return Fail("All meshes must share the same vertex layout in this prototype");
                }
            }

            Submit([&](nvrhi::ICommandList* cmd)
            {
                for (const MeshData& mesh : scene.Meshes)
                {
                    GpuMesh gpu;
                    for (const VertexStream& stream : mesh.VertexStreams)
                    {
                        nvrhi::BufferDesc desc;
                        desc.byteSize = std::max<size_t>(stream.Data.size(), 16);
                        desc.isVertexBuffer = true;
                        desc.debugName = "SceneVertexBuffer";
                        desc.initialState = nvrhi::ResourceStates::VertexBuffer;
                        desc.keepInitialState = true;
                        nvrhi::BufferHandle buffer = Device->createBuffer(desc);
                        cmd->writeBuffer(buffer, stream.Data.data(), stream.Data.size());
                        gpu.VertexBuffers.push_back(buffer);
                    }

                    nvrhi::BufferDesc indexDesc;
                    indexDesc.byteSize = std::max<size_t>(mesh.IndexBuffer.size(), 16);
                    indexDesc.isIndexBuffer = true;
                    indexDesc.debugName = "SceneIndexBuffer";
                    indexDesc.initialState = nvrhi::ResourceStates::IndexBuffer;
                    indexDesc.keepInitialState = true;
                    gpu.IndexBuffer = Device->createBuffer(indexDesc);
                    cmd->writeBuffer(gpu.IndexBuffer, mesh.IndexBuffer.data(), mesh.IndexBuffer.size());
                    gpu.IndexFormat = mesh.IndexFormat == IndexFormat::UInt16 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT;
                    gpu.SubMeshes = mesh.SubMeshes;
                    Meshes.push_back(std::move(gpu));
                }

                for (const TextureData& texture : scene.Textures)
                {
                    nvrhi::TextureDesc desc;
                    desc.width = texture.Width;
                    desc.height = texture.Height;
                    desc.arraySize = texture.ArraySize;
                    desc.mipLevels = texture.MipLevels;
                    desc.format = ToNvrhi(texture.Format);
                    desc.dimension = texture.ArraySize > 1 ? nvrhi::TextureDimension::Texture2DArray : nvrhi::TextureDimension::Texture2D;
                    desc.debugName = "SceneTexture";
                    desc.initialState = nvrhi::ResourceStates::ShaderResource;
                    desc.keepInitialState = true;
                    nvrhi::TextureHandle handle = Device->createTexture(desc);
                    for (const TextureSubresource& sub : texture.Subresources)
                        cmd->writeTexture(handle, sub.ArraySlice, sub.MipLevel, sub.Pixels.data(), sub.RowPitch, sub.SlicePitch);
                    SceneTextures.push_back(handle);
                    SceneTextureDescriptors.push_back(Bindless->CreateDescriptorHandle(
                        nvrhi::BindingSetItem::Texture_SRV(0, handle).setDimension(desc.dimension)));
                }

                std::vector<GpuMaterial> materials;
                for (const MaterialData& material : scene.Materials)
                    materials.push_back(ToGpuMaterial(material));
                if (materials.empty())
                    materials.push_back(ToGpuMaterial({}));

                nvrhi::BufferDesc materialDesc;
                materialDesc.byteSize = materials.size() * sizeof(GpuMaterial);
                materialDesc.structStride = sizeof(GpuMaterial);
                materialDesc.debugName = "Materials";
                materialDesc.initialState = nvrhi::ResourceStates::ShaderResource;
                materialDesc.keepInitialState = true;
                MaterialBuffer = Device->createBuffer(materialDesc);
                cmd->writeBuffer(MaterialBuffer, materials.data(), materialDesc.byteSize);
            });
            std::printf("[Renderer] %u bindless descriptors in use (capacity %u)\n", Bindless->GetAllocatedCount(), Bindless->GetCapacity());

            nvrhi::BindingSetDesc materialSet;
            materialSet.bindings = { nvrhi::BindingSetItem::StructuredBuffer_SRV(0, MaterialBuffer) };
            MaterialSet = Device->createBindingSet(materialSet, MaterialLayout);

            HasScene = true;

            // Pipelines depend on the scene vertex layout.
            if (GraphReady)
            {
                CompiledGraph graph = Graph;
                return SetGraph(graph);
            }
            return true;
        }

        // ---- Graph ------------------------------------------------------------------------

        void ReleaseGraph()
        {
            Passes.clear();
            Resources.clear();
            Shaders.clear();
            Samplers.clear();
            GraphReady = false;
        }

        bool SetGraph(const CompiledGraph& graph)
        {
            Device->waitForIdle();
            ReleaseGraph();
            Graph = graph;

            if (!HasScene)
                return Fail("SetScene must be called before SetGraph");
            if (Graph.Width != Desc.Width || Graph.Height != Desc.Height)
                return Fail(std::format("The graph was compiled for {}x{}, the back buffer is {}x{}", Graph.Width, Graph.Height, Desc.Width, Desc.Height));

            // Resources. The graph output is not created: it is the swap chain image.
            for (const CompiledResource& resource : Graph.Resources)
            {
                GpuResource gpu;
                if (resource.IsGraphOutput)
                {
                    if (ToNvrhi(resource.Texture.Format) != Backend->GetBackBufferFormat())
                        return Fail("The graph output format does not match the back buffer");
                }
                else if (resource.Kind == ResourceKind::Texture)
                {
                    const TextureDesc& t = resource.Texture;
                    const bool isDepth = IsDepthFormat(t.Format);

                    nvrhi::TextureDesc desc;
                    desc.width = t.Width;
                    desc.height = t.Height;
                    desc.mipLevels = t.MipLevels;
                    desc.format = ToNvrhi(t.Format);
                    desc.debugName = resource.Name;
                    desc.isShaderResource = true;
                    desc.isTypeless = isDepth; // depth that is also sampled needs a typeless resource
                    desc.isRenderTarget = resource.HasUsage(ResourceUsage::RenderTarget) || resource.HasUsage(ResourceUsage::DepthStencil) || resource.HasUsage(ResourceUsage::DepthRead);
                    desc.isUAV = resource.HasUsage(ResourceUsage::UnorderedAccess);
                    if (desc.isRenderTarget)
                        desc.setClearValue(isDepth ? nvrhi::Color(t.ClearValue[0], 0.0f, 0.0f, 0.0f) : nvrhi::Color(t.ClearValue[0], t.ClearValue[1], t.ClearValue[2], t.ClearValue[3]));

                    desc.initialState = isDepth ? nvrhi::ResourceStates::DepthWrite
                        : desc.isRenderTarget ? nvrhi::ResourceStates::RenderTarget
                        : desc.isUAV ? nvrhi::ResourceStates::UnorderedAccess
                        : nvrhi::ResourceStates::ShaderResource;
                    desc.keepInitialState = true;
                    gpu.Texture = Device->createTexture(desc);
                }
                else
                {
                    const BufferDesc& b = resource.Buffer;
                    nvrhi::BufferDesc desc;
                    desc.byteSize = b.ByteSize();
                    desc.debugName = resource.Name;
                    if (b.Format == TextureFormat::Unknown)
                        desc.structStride = b.ElementSize;
                    else
                    {
                        desc.format = ToNvrhi(b.Format);
                        desc.canHaveTypedViews = true;
                    }
                    desc.canHaveUAVs = resource.HasUsage(ResourceUsage::UnorderedAccess);
                    desc.initialState = desc.canHaveUAVs ? nvrhi::ResourceStates::UnorderedAccess : nvrhi::ResourceStates::ShaderResource;
                    desc.keepInitialState = true;
                    gpu.Buffer = Device->createBuffer(desc);
                }
                Resources.push_back(std::move(gpu));
            }

            // Shaders (cached by file / entry / stage across rebuilds).
            for (const ShaderDesc& shader : Graph.Shaders)
            {
                const std::string key = std::format("{}|{}|{}", shader.File, shader.EntryPoint, int(shader.Type));
                nvrhi::ShaderHandle& cached = ShaderCache[key];
                if (!cached)
                {
                    ShaderCompileRequest request;
                    request.Source = Desc.ShaderDirectory / shader.File;
                    request.EntryPoint = shader.EntryPoint;
                    request.IncludeDirectories = { Desc.ShaderDirectory };

                    auto blob = Compiler->Compile(request);
                    if (!blob)
                    {
                        ShaderCache.erase(key);
                        return Fail(blob.error());
                    }

                    nvrhi::ShaderDesc desc;
                    desc.shaderType = ToNvrhi(shader.Type);
                    desc.debugName = shader.DebugName;
                    desc.entryName = shader.EntryPoint;
                    cached = Device->createShader(desc, blob->Bytecode.data(), blob->Bytecode.size());
                    if (!cached)
                    {
                        ShaderCache.erase(key);
                        return Fail("createShader failed for " + shader.DebugName);
                    }
                    std::printf("[Renderer] Compiled %s (%zu bytes)\n", shader.DebugName.c_str(), blob->Bytecode.size());
                }
                Shaders.push_back(cached);
            }

            for (const SamplerDesc& sampler : Graph.Samplers)
                Samplers.push_back(Device->createSampler(ToNvrhi(sampler)));

            Passes.resize(Graph.Passes.size());
            for (size_t i = 0; i < Graph.Passes.size(); ++i)
            {
                if (!BuildPass(Graph.Passes[i], Passes[i]))
                {
                    ReleaseGraph();
                    return false;
                }
            }

            GraphReady = true;
            return true;
        }

        nvrhi::BindingLayoutHandle CreatePassLayout(const CompiledPass& pass, const GpuPass& gpu)
        {
            const RenderPass& rp = *pass.Pass;

            nvrhi::BindingLayoutDesc desc;
            desc.visibility = rp.GetType() == PassType::Compute ? nvrhi::ShaderType::Compute : nvrhi::ShaderType::AllGraphics;
            desc.registerSpace = 0;

            if (gpu.Constants)
                desc.bindings.push_back(nvrhi::BindingLayoutItem::VolatileConstantBuffer(kPassConstantsSlot));
            desc.bindings.push_back(nvrhi::BindingLayoutItem::VolatileConstantBuffer(kFrameConstantsSlot));

            uint32_t t = 0, u = 0;
            for (const CompiledBinding& binding : pass.Bindings)
            {
                const CompiledResource& resource = Graph.Resources[binding.Resource];
                const bool texture = resource.Kind == ResourceKind::Texture;
                const bool typed = resource.Buffer.Format != TextureFormat::Unknown;

                if (binding.Usage == ResourceUsage::ShaderResource)
                {
                    desc.bindings.push_back(texture ? nvrhi::BindingLayoutItem::Texture_SRV(t)
                        : typed ? nvrhi::BindingLayoutItem::TypedBuffer_SRV(t) : nvrhi::BindingLayoutItem::StructuredBuffer_SRV(t));
                    ++t;
                }
                else if (binding.Usage == ResourceUsage::UnorderedAccess)
                {
                    desc.bindings.push_back(texture ? nvrhi::BindingLayoutItem::Texture_UAV(u)
                        : typed ? nvrhi::BindingLayoutItem::TypedBuffer_UAV(u) : nvrhi::BindingLayoutItem::StructuredBuffer_UAV(u));
                    ++u;
                }
            }

            if (rp.UsesDrawTexture())
                desc.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(t++));

            for (uint32_t s = 0; s < uint32_t(pass.Samplers.size()); ++s)
                desc.bindings.push_back(nvrhi::BindingLayoutItem::Sampler(s));

            if (const uint32_t size = PushConstantsSize(rp))
                desc.bindings.push_back(nvrhi::BindingLayoutItem::PushConstants(kPushConstantsSlot, size));

            return Device->createBindingLayout(desc);
        }

        static uint32_t PushConstantsSize(const RenderPass& rp)
        {
            return (rp.GetPushConstantsSize() + 3) & ~3u; // whole 32-bit values
        }

        nvrhi::BindingSetHandle CreatePassBindingSet(const CompiledPass& pass, const GpuPass& gpu, uint32_t backBufferIndex, nvrhi::ITexture* drawTexture)
        {
            const RenderPass& rp = *pass.Pass;

            nvrhi::BindingSetDesc desc;
            if (gpu.Constants)
                desc.bindings.push_back(nvrhi::BindingSetItem::ConstantBuffer(kPassConstantsSlot, gpu.Constants));
            desc.bindings.push_back(nvrhi::BindingSetItem::ConstantBuffer(kFrameConstantsSlot, FrameConstantBuffer));

            uint32_t t = 0, u = 0;
            for (const CompiledBinding& binding : pass.Bindings)
            {
                const CompiledResource& resource = Graph.Resources[binding.Resource];
                const bool texture = resource.Kind == ResourceKind::Texture;
                const bool typed = resource.Buffer.Format != TextureFormat::Unknown;
                nvrhi::IBuffer* buffer = Resources[binding.Resource].Buffer;

                if (binding.Usage == ResourceUsage::ShaderResource)
                {
                    desc.bindings.push_back(texture ? nvrhi::BindingSetItem::Texture_SRV(t, ResolveTexture(binding.Resource, backBufferIndex))
                        : typed ? nvrhi::BindingSetItem::TypedBuffer_SRV(t, buffer, ToNvrhi(resource.Buffer.Format))
                        : nvrhi::BindingSetItem::StructuredBuffer_SRV(t, buffer));
                    ++t;
                }
                else if (binding.Usage == ResourceUsage::UnorderedAccess)
                {
                    desc.bindings.push_back(texture ? nvrhi::BindingSetItem::Texture_UAV(u, ResolveTexture(binding.Resource, backBufferIndex))
                        : typed ? nvrhi::BindingSetItem::TypedBuffer_UAV(u, buffer, ToNvrhi(resource.Buffer.Format))
                        : nvrhi::BindingSetItem::StructuredBuffer_UAV(u, buffer));
                    ++u;
                }
            }

            if (rp.UsesDrawTexture())
                desc.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(t++, drawTexture));

            for (uint32_t s = 0; s < uint32_t(pass.Samplers.size()); ++s)
                desc.bindings.push_back(nvrhi::BindingSetItem::Sampler(s, Samplers[pass.Samplers[s]]));

            if (const uint32_t size = PushConstantsSize(rp))
                desc.bindings.push_back(nvrhi::BindingSetItem::PushConstants(kPushConstantsSlot, size));

            return Device->createBindingSet(desc, gpu.Layout);
        }

        bool BuildPass(const CompiledPass& pass, GpuPass& gpu)
        {
            const RenderPass& rp = *pass.Pass;
            if (rp.GetType() == PassType::Copy)
                return true; // copies need no pipeline

            if (!rp.GetConstants().empty())
            {
                gpu.ConstantsSize = uint32_t((rp.GetConstants().size() + 15) & ~size_t(15));
                nvrhi::BufferDesc desc;
                desc.byteSize = gpu.ConstantsSize;
                desc.isConstantBuffer = true;
                desc.isVolatile = true;
                desc.maxVersions = 64;
                desc.debugName = pass.Name + ".Constants";
                gpu.Constants = Device->createBuffer(desc);
            }

            gpu.Layout = CreatePassLayout(pass, gpu);
            if (!gpu.Layout)
                return Fail("Could not create the binding layout of " + pass.Name);

            // Binding sets. They only vary per back buffer if a shader reads the graph output.
            // With a draw texture they are created on first use, per texture (DrawTextureSets).
            gpu.PerBackBufferBindings = UsesOutput(pass, false);
            if (!rp.UsesDrawTexture())
            {
                const uint32_t variants = gpu.PerBackBufferBindings ? BackBufferCount() : 1;
                for (uint32_t i = 0; i < variants; ++i)
                    gpu.BindingSets.push_back(CreatePassBindingSet(pass, gpu, i, nullptr));
            }

            if (rp.GetType() == PassType::Compute)
            {
                nvrhi::ComputePipelineDesc desc;
                desc.CS = FindShader(pass, ShaderType::Compute);
                desc.bindingLayouts = { gpu.Layout };
                gpu.ComputePipeline = Device->createComputePipeline(desc);
                return gpu.ComputePipeline ? true : Fail("Could not create the compute pipeline of " + pass.Name);
            }

            // Framebuffers: one per back buffer if the pass renders into the graph output.
            uint32_t colorCount = 0;
            const uint32_t fbVariants = UsesOutput(pass, true) ? BackBufferCount() : 1;
            for (uint32_t i = 0; i < fbVariants; ++i)
            {
                nvrhi::FramebufferDesc desc;
                colorCount = 0;
                for (const CompiledBinding& binding : pass.Bindings)
                {
                    if (binding.Usage == ResourceUsage::RenderTarget)
                    {
                        desc.addColorAttachment(ResolveTexture(binding.Resource, i));
                        ++colorCount;
                    }
                    else if (binding.Usage == ResourceUsage::DepthStencil || binding.Usage == ResourceUsage::DepthRead)
                    {
                        desc.setDepthAttachment(nvrhi::FramebufferAttachment()
                            .setTexture(ResolveTexture(binding.Resource, i))
                            .setReadOnly(binding.Usage == ResourceUsage::DepthRead));
                    }
                }
                gpu.Framebuffers.push_back(Device->createFramebuffer(desc));
            }

            if (!FindShader(pass, ShaderType::Vertex))
                return true; // nothing is drawn, the pass only clears

            nvrhi::GraphicsPipelineDesc desc;
            desc.primType = ToNvrhi(rp.GetState().Primitive);
            desc.VS = FindShader(pass, ShaderType::Vertex);
            desc.PS = FindShader(pass, ShaderType::Pixel);
            desc.GS = FindShader(pass, ShaderType::Geometry);

            const VertexInputDesc& input = rp.GetVertexInput();
            if (input.SceneLayout)
            {
                desc.inputLayout = Device->createInputLayout(SceneVertexLayout.data(), uint32_t(SceneVertexLayout.size()), desc.VS);
            }
            else if (!input.Streams.empty())
            {
                std::vector<nvrhi::VertexAttributeDesc> attributes;
                for (uint32_t s = 0; s < uint32_t(input.Streams.size()); ++s)
                {
                    for (const VertexAttribute& attribute : input.Streams[s].Attributes)
                    {
                        nvrhi::VertexAttributeDesc& a = attributes.emplace_back();
                        a.name = SemanticName(attribute.Semantic);
                        a.format = ToNvrhi(attribute.Format);
                        a.bufferIndex = s;
                        a.offset = attribute.Offset;
                        a.elementStride = input.Streams[s].Stride;
                    }
                }
                desc.inputLayout = Device->createInputLayout(attributes.data(), uint32_t(attributes.size()), desc.VS);
            }

            const PipelineState& state = rp.GetState();
            for (uint32_t i = 0; i < colorCount; ++i)
            {
                nvrhi::BlendState::RenderTarget& target = desc.renderState.blendState.targets[i];
                target.blendEnable = state.Blend.Enable;
                target.srcBlend = ToNvrhi(state.Blend.SrcColor);
                target.destBlend = ToNvrhi(state.Blend.DstColor);
                target.blendOp = ToNvrhi(state.Blend.ColorOp);
                target.srcBlendAlpha = ToNvrhi(state.Blend.SrcAlpha);
                target.destBlendAlpha = ToNvrhi(state.Blend.DstAlpha);
                target.blendOpAlpha = ToNvrhi(state.Blend.AlphaOp);
            }

            nvrhi::DepthStencilState& depth = desc.renderState.depthStencilState;
            depth.depthTestEnable = state.Depth.TestEnable;
            depth.depthWriteEnable = state.Depth.WriteEnable;
            depth.depthFunc = ToNvrhi(state.Depth.Func);
            depth.stencilEnable = false;

            nvrhi::RasterState& raster = desc.renderState.rasterState;
            raster.cullMode = ToNvrhi(state.Raster.CullMode);
            raster.frontCounterClockwise = state.Raster.FrontCounterClockwise;
            raster.scissorEnable = state.Raster.ScissorEnable;
            raster.depthClipEnable = true;

            desc.bindingLayouts = { gpu.Layout };
            if (rp.UsesMaterials())
            {
                desc.bindingLayouts.push_back(MaterialLayout);
                desc.bindingLayouts.push_back(BindlessLayout);
            }

            gpu.GraphicsPipeline = Device->createGraphicsPipeline(desc, gpu.Framebuffers[0]->getFramebufferInfo());
            return gpu.GraphicsPipeline ? true : Fail("Could not create the graphics pipeline of " + pass.Name);
        }

        // ---- Execution --------------------------------------------------------------------

        void Render(const Scene& scene, const FrameInfo& frame)
        {
            if (!GraphReady)
                return;

            Backend->BeginFrame();
            const uint32_t backBuffer = Backend->GetCurrentBackBufferIndex();

            CommandList->open();

            const PerspectiveCamera& camera = scene.MainCamera;
            FrameConstants constants;
            constants.View = camera.GetView();
            constants.Projection = camera.GetProjection();
            constants.ViewProjection = camera.GetViewProjection();
            constants.InverseViewProjection = camera.GetInverseViewProjection();
            constants.CameraPosition = camera.GetPosition();
            constants.Time = frame.Time;
            constants.Resolution = { float(Desc.Width), float(Desc.Height) };
            constants.InvResolution = { 1.0f / float(Desc.Width), 1.0f / float(Desc.Height) };
            constants.DeltaTime = frame.DeltaTime;
            constants.FrameIndex = frame.FrameIndex;
            CommandList->writeBuffer(FrameConstantBuffer, &constants, sizeof(constants));

            for (size_t i = 0; i < Graph.Passes.size(); ++i)
                ExecutePass(Graph.Passes[i], Passes[i], scene, backBuffer);

            CommandList->clearState(); // drop the bound framebuffer so no back buffer reference outlives the frame
            CommandList->close();
            Device->executeCommandList(CommandList);

            Backend->Present();
            Device->runGarbageCollection();
        }

        void ExecutePass(const CompiledPass& pass, GpuPass& gpu, const Scene& scene, uint32_t backBuffer)
        {
            const RenderPass& rp = *pass.Pass;
            nvrhi::ICommandList* cmd = CommandList;
            cmd->beginMarker(pass.Name.c_str());

            for (const CompiledBinding& binding : pass.Bindings)
            {
                if (!binding.Clear)
                    continue;
                const CompiledResource& resource = Graph.Resources[binding.Resource];
                if (resource.Kind == ResourceKind::Buffer)
                {
                    cmd->clearBufferUInt(Resources[binding.Resource].Buffer, 0);
                    continue;
                }
                const auto& c = resource.Texture.ClearValue;
                nvrhi::ITexture* texture = ResolveTexture(binding.Resource, backBuffer);
                if (IsDepthFormat(resource.Texture.Format))
                    cmd->clearDepthStencilTexture(texture, nvrhi::AllSubresources, true, c[0], false, 0);
                else
                    cmd->clearTextureFloat(texture, nvrhi::AllSubresources, nvrhi::Color(c[0], c[1], c[2], c[3]));
            }

            if (gpu.Constants)
            {
                std::vector<std::byte> data(gpu.ConstantsSize);
                const auto constants = rp.GetConstants();
                std::memcpy(data.data(), constants.data(), std::min<size_t>(constants.size(), data.size()));
                cmd->writeBuffer(gpu.Constants, data.data(), data.size());
            }

            switch (rp.GetType())
            {
            case PassType::Copy:     ExecuteCopy(pass, backBuffer); break;
            case PassType::Compute:  ExecuteCompute(pass, gpu, backBuffer); break;
            case PassType::Graphics: ExecuteGraphics(pass, gpu, scene, backBuffer); break;
            }

            cmd->endMarker();
        }

        void ExecuteCopy(const CompiledPass& pass, uint32_t backBuffer)
        {
            RhiHandle src = kInvalidHandle, dst = kInvalidHandle;
            for (const CompiledBinding& binding : pass.Bindings)
            {
                if (binding.Usage == ResourceUsage::CopySource) src = binding.Resource;
                if (binding.Usage == ResourceUsage::CopyDest) dst = binding.Resource;
            }
            if (Graph.Resources[src].Kind == ResourceKind::Texture)
            {
                CommandList->copyTexture(ResolveTexture(dst, backBuffer), nvrhi::TextureSlice(), ResolveTexture(src, backBuffer), nvrhi::TextureSlice());
            }
            else
            {
                const uint64_t size = std::min(Graph.Resources[src].Buffer.ByteSize(), Graph.Resources[dst].Buffer.ByteSize());
                CommandList->copyBuffer(Resources[dst].Buffer, 0, Resources[src].Buffer, 0, size);
            }
        }

        void ExecuteCompute(const CompiledPass& pass, GpuPass& gpu, uint32_t backBuffer)
        {
            nvrhi::ComputeState state;
            state.pipeline = gpu.ComputePipeline;
            state.bindings = { gpu.BindingSets[gpu.BindingSets.size() > 1 ? backBuffer : 0] };
            CommandList->setComputeState(state);

            const auto groups = pass.Pass->GetDispatchSize();
            CommandList->dispatch(groups[0], groups[1], groups[2]);
        }

        // Copies data into a transient buffer and returns the offset it was written at.
        uint64_t UploadTransient(TransientBuffer& transient, std::span<const std::byte> data, bool index, const std::string& passName)
        {
            const uint64_t offset = (transient.Offset + 15) & ~uint64_t(15);
            if (!transient.Buffer || offset + data.size() > transient.Capacity)
            {
                // Earlier draws of this frame keep the old buffer alive through the command list.
                transient.Capacity = std::max<uint64_t>(64 * 1024, (offset + data.size()) * 2);
                nvrhi::BufferDesc desc;
                desc.byteSize = transient.Capacity;
                desc.isVertexBuffer = !index;
                desc.isIndexBuffer = index;
                desc.debugName = passName + (index ? ".TransientIndices" : ".TransientVertices");
                desc.initialState = index ? nvrhi::ResourceStates::IndexBuffer : nvrhi::ResourceStates::VertexBuffer;
                desc.keepInitialState = true;
                transient.Buffer = Device->createBuffer(desc);
                transient.Offset = 0;
                return UploadTransient(transient, data, index, passName);
            }
            CommandList->writeBuffer(transient.Buffer, data.data(), data.size(), offset);
            transient.Offset = offset + data.size();
            return offset;
        }

        // DrawContext of one graphics pass. Changes are collected in an nvrhi::GraphicsState
        // and only submitted (setGraphicsState + push constants) when a draw needs them.
        class PassDrawContext final : public DrawContext
        {
        public:
            PassDrawContext(Impl& renderer, const CompiledPass& pass, GpuPass& gpu, const Scene& scene, uint32_t backBuffer)
                : m_Renderer(renderer), m_Pass(pass), m_Gpu(gpu), m_Scene(scene)
            {
                m_Variant = gpu.PerBackBufferBindings ? backBuffer : 0;
                nvrhi::IFramebuffer* framebuffer = gpu.Framebuffers[gpu.Framebuffers.size() > 1 ? backBuffer : 0];
                m_Viewport = framebuffer->getFramebufferInfo().getViewport();

                m_State.pipeline = gpu.GraphicsPipeline;
                m_State.framebuffer = framebuffer;
                m_State.viewport.addViewportAndScissorRect(m_Viewport);
                if (!gpu.BindingSets.empty())
                    m_PassSet = gpu.BindingSets[gpu.BindingSets.size() > 1 ? m_Variant : 0];
                m_PushConstants.resize(PushConstantsSize(*pass.Pass));

                gpu.TransientVertices.Offset = 0;
                gpu.TransientIndices.Offset = 0;
            }

            const Scene& GetScene() const override { return m_Scene; }
            uint32_t GetTargetWidth() const override { return uint32_t(m_Viewport.maxX - m_Viewport.minX); }
            uint32_t GetTargetHeight() const override { return uint32_t(m_Viewport.maxY - m_Viewport.minY); }

            void BindMesh(uint32_t index) override
            {
                if (index >= m_Renderer.Meshes.size())
                {
                    Warn(std::format("BindMesh: mesh {} does not exist", index));
                    return;
                }
                const GpuMesh& mesh = m_Renderer.Meshes[index];
                m_State.vertexBuffers.resize(0);
                for (uint32_t s = 0; s < uint32_t(mesh.VertexBuffers.size()); ++s)
                    m_State.vertexBuffers.push_back({ mesh.VertexBuffers[s], s, 0 });
                m_State.indexBuffer = { mesh.IndexBuffer, mesh.IndexFormat, 0 };
                m_StateDirty = true;
            }

            void SetVertexData(uint32_t stream, std::span<const std::byte> data) override
            {
                if (data.empty())
                    return;
                const uint64_t offset = m_Renderer.UploadTransient(m_Gpu.TransientVertices, data, false, m_Pass.Name);
                const nvrhi::VertexBufferBinding binding{ m_Gpu.TransientVertices.Buffer, stream, offset };
                for (nvrhi::VertexBufferBinding& existing : m_State.vertexBuffers)
                {
                    if (existing.slot == stream)
                    {
                        existing = binding;
                        m_StateDirty = true;
                        return;
                    }
                }
                m_State.vertexBuffers.push_back(binding);
                m_StateDirty = true;
            }

            void SetIndexData(std::span<const std::byte> data, IndexFormat format) override
            {
                if (data.empty())
                    return;
                const uint64_t offset = m_Renderer.UploadTransient(m_Gpu.TransientIndices, data, true, m_Pass.Name);
                const nvrhi::Format nvrhiFormat = format == IndexFormat::UInt16 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT;
                m_State.indexBuffer = { m_Gpu.TransientIndices.Buffer, nvrhiFormat, uint32_t(offset) };
                m_StateDirty = true;
            }

            void SetPushConstants(std::span<const std::byte> data) override
            {
                if (m_PushConstants.empty())
                {
                    Warn("SetPushConstants: the pass declares no push constants (RenderPass::SetPushConstants<T>)");
                    return;
                }
                std::memcpy(m_PushConstants.data(), data.data(), std::min(data.size(), m_PushConstants.size()));
                m_PushDirty = true;
            }

            void SetTexture(TextureId id) override
            {
                if (!m_Pass.Pass->UsesDrawTexture())
                {
                    Warn("SetTexture: the pass does not declare a draw texture (RenderPass::SetUsesDrawTexture)");
                    return;
                }
                auto* texture = reinterpret_cast<nvrhi::ITexture*>(static_cast<uintptr_t>(id));
                nvrhi::BindingSetHandle& set = m_Gpu.DrawTextureSets[{ texture, m_Variant }];
                if (!set)
                    set = m_Renderer.CreatePassBindingSet(m_Pass, m_Gpu, m_Variant, texture);
                if (set.Get() != m_PassSet)
                {
                    m_PassSet = set;
                    m_StateDirty = true;
                }
            }

            void SetScissor(const ScissorRect& rect) override
            {
                m_State.viewport = nvrhi::ViewportState().addViewport(m_Viewport).addScissorRect(nvrhi::Rect(rect.MinX, rect.MaxX, rect.MinY, rect.MaxY));
                m_StateDirty = true;
            }

            void ResetScissor() override
            {
                m_State.viewport = nvrhi::ViewportState().addViewportAndScissorRect(m_Viewport);
                m_StateDirty = true;
            }

            void Draw(const DrawArgs& args) override
            {
                if (!Flush())
                    return;
                nvrhi::DrawArguments draw;
                draw.vertexCount = args.VertexCount;
                draw.instanceCount = args.InstanceCount;
                draw.startVertexLocation = args.FirstVertex;
                draw.startInstanceLocation = args.FirstInstance;
                m_Renderer.CommandList->draw(draw);
            }

            void DrawIndexed(const DrawIndexedArgs& args) override
            {
                if (!m_State.indexBuffer.buffer)
                {
                    Warn("DrawIndexed: no index buffer (BindMesh or SetIndexData)");
                    return;
                }
                if (!Flush())
                    return;
                nvrhi::DrawArguments draw;
                draw.vertexCount = args.IndexCount;
                draw.instanceCount = args.InstanceCount;
                draw.startIndexLocation = args.FirstIndex;
                draw.startVertexLocation = uint32_t(args.BaseVertex);
                draw.startInstanceLocation = args.FirstInstance;
                m_Renderer.CommandList->drawIndexed(draw);
            }

        private:
            bool Flush()
            {
                if (!m_PassSet)
                {
                    Warn("Draw skipped: the pass uses a draw texture and none was set (DrawContext::SetTexture)");
                    return false;
                }
                if (m_StateDirty)
                {
                    m_State.bindings = { m_PassSet };
                    if (m_Pass.Pass->UsesMaterials())
                    {
                        m_State.bindings.push_back(m_Renderer.MaterialSet);
                        m_State.bindings.push_back(m_Renderer.Bindless->GetDescriptorTable());
                    }
                    m_Renderer.CommandList->setGraphicsState(m_State);
                    m_StateDirty = false;
                    m_PushDirty = !m_PushConstants.empty(); // push constants do not survive a state change
                }
                if (m_PushDirty)
                {
                    m_Renderer.CommandList->setPushConstants(m_PushConstants.data(), m_PushConstants.size());
                    m_PushDirty = false;
                }
                return true;
            }

            void Warn(const std::string& message)
            {
                if (m_Warned)
                    return; // once per pass and frame
                m_Warned = true;
                std::fprintf(stderr, "[Renderer] Pass '%s': %s\n", m_Pass.Name.c_str(), message.c_str());
            }

            Impl& m_Renderer;
            const CompiledPass& m_Pass;
            GpuPass& m_Gpu;
            const Scene& m_Scene;
            uint32_t m_Variant = 0;
            nvrhi::Viewport m_Viewport;
            nvrhi::GraphicsState m_State;
            nvrhi::IBindingSet* m_PassSet = nullptr;
            std::vector<std::byte> m_PushConstants;
            bool m_StateDirty = true;
            bool m_PushDirty = false;
            bool m_Warned = false;
        };

        void ExecuteGraphics(const CompiledPass& pass, GpuPass& gpu, const Scene& scene, uint32_t backBuffer)
        {
            if (!gpu.GraphicsPipeline)
                return; // no vertex shader: the pass only clears

            PassDrawContext context(*this, pass, gpu, scene, backBuffer);
            pass.Pass->Draw(context);
        }
    };

    // ============================================================================================

    Renderer::Renderer() : m_Impl(std::make_unique<Impl>()) {}
    Renderer::~Renderer() { Shutdown(); }

    bool Renderer::Init(const RendererDesc& desc) { return m_Impl->Init(desc); }
    void Renderer::Shutdown() { m_Impl->Shutdown(); }
    void Renderer::Resize(uint32_t width, uint32_t height) { m_Impl->Resize(width, height); }

    uint32_t Renderer::GetWidth() const { return m_Impl->Desc.Width; }
    uint32_t Renderer::GetHeight() const { return m_Impl->Desc.Height; }

    TextureFormat Renderer::GetBackBufferFormat() const
    {
        return m_Impl->Backend ? FromNvrhi(m_Impl->Backend->GetBackBufferFormat()) : TextureFormat::Unknown;
    }

    const char* Renderer::GetBackendName() const
    {
        return m_Impl->Backend ? m_Impl->Backend->GetName() : "none";
    }

    bool Renderer::SetScene(const Scene& scene) { return m_Impl->SetScene(scene); }
    bool Renderer::SetGraph(const CompiledGraph& graph) { return m_Impl->SetGraph(graph); }
    void Renderer::Render(const Scene& scene, const FrameInfo& frame) { m_Impl->Render(scene, frame); }
    const std::string& Renderer::GetLastError() const { return m_Impl->LastError; }
}
