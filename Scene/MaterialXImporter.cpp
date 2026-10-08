#include "Scene/MaterialXImporter.h"

#include <MaterialXCore/Document.h>
#include <MaterialXCore/Node.h>
#include <MaterialXCore/Value.h>
#include <MaterialXFormat/XmlIo.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <type_traits>

namespace LynxEngine
{
    namespace
    {
        // ---- MaterialX reading (same logic as Idea's MaterialImporter.cpp) ------------------

        void ReadFloat(const MaterialX::NodePtr& node, const char* inputName, float& value)
        {
            const auto input = node->getInput(inputName);
            if (!input || node->getConnectedNode(inputName))
                return;

            const auto inputValue = input->getValue();
            if (inputValue && inputValue->isA<float>())
                value = inputValue->asA<float>();
        }

        void ReadFloat3(const MaterialX::NodePtr& node, const char* inputName, float3& value)
        {
            const auto input = node->getInput(inputName);
            if (!input || node->getConnectedNode(inputName))
                return;

            const auto inputValue = input->getValue();
            if (!inputValue)
                return;

            std::string serialized = inputValue->getValueString();
            std::ranges::replace(serialized, ',', ' ');
            std::ranges::replace(serialized, '(', ' ');
            std::ranges::replace(serialized, ')', ' ');
            std::istringstream stream(serialized);
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (stream >> x >> y >> z)
                value = float3{ x, y, z };
        }

        template <typename T>
        void ReadInput(const MaterialX::NodePtr& node, const char* name, T& value)
        {
            if constexpr (std::is_same_v<T, float>)
                ReadFloat(node, name, value);
            else
                ReadFloat3(node, name, value);
        }

        MaterialX::NodePtr FindSurfaceShader(const MaterialX::NodePtr& materialNode)
        {
            for (const char* inputName : { "surfaceshader", "shader" })
            {
                auto node = materialNode->getConnectedNode(inputName);
                if (node)
                    return node;
            }
            return nullptr;
        }

        void ReadStandardSurface(const MaterialX::NodePtr& node, MaterialXMaterial& data)
        {
#define RG_READ_MTLX_INPUT(input, field) ReadInput(node, input, data.field)
            RG_READ_MTLX_INPUT("base", Base);
            RG_READ_MTLX_INPUT("base_color", BaseColor);
            RG_READ_MTLX_INPUT("diffuse_roughness", DiffuseRoughness);
            RG_READ_MTLX_INPUT("metalness", Metalness);
            RG_READ_MTLX_INPUT("specular", Specular);
            RG_READ_MTLX_INPUT("specular_color", SpecularColor);
            RG_READ_MTLX_INPUT("specular_roughness", SpecularRoughness);
            RG_READ_MTLX_INPUT("specular_IOR", SpecularIOR);
            RG_READ_MTLX_INPUT("specular_anisotropy", SpecularAnisotropy);
            RG_READ_MTLX_INPUT("specular_rotation", SpecularRotation);
            RG_READ_MTLX_INPUT("transmission", Transmission);
            RG_READ_MTLX_INPUT("transmission_color", TransmissionColor);
            RG_READ_MTLX_INPUT("transmission_depth", TransmissionDepth);
            RG_READ_MTLX_INPUT("transmission_scatter", TransmissionScatter);
            RG_READ_MTLX_INPUT("transmission_scatter_anisotropy", TransmissionScatterAnisotropy);
            RG_READ_MTLX_INPUT("transmission_dispersion", TransmissionDispersion);
            RG_READ_MTLX_INPUT("transmission_extra_roughness", TransmissionExtraRoughness);
            RG_READ_MTLX_INPUT("subsurface", Subsurface);
            RG_READ_MTLX_INPUT("subsurface_color", SubsurfaceColor);
            RG_READ_MTLX_INPUT("subsurface_radius", SubsurfaceRadius);
            RG_READ_MTLX_INPUT("subsurface_scale", SubsurfaceScale);
            RG_READ_MTLX_INPUT("subsurface_anisotropy", SubsurfaceAnisotropy);
            RG_READ_MTLX_INPUT("sheen", Sheen);
            RG_READ_MTLX_INPUT("sheen_color", SheenColor);
            RG_READ_MTLX_INPUT("sheen_roughness", SheenRoughness);
            RG_READ_MTLX_INPUT("coat", Coat);
            RG_READ_MTLX_INPUT("coat_color", CoatColor);
            RG_READ_MTLX_INPUT("coat_roughness", CoatRoughness);
            RG_READ_MTLX_INPUT("coat_anisotropy", CoatAnisotropy);
            RG_READ_MTLX_INPUT("coat_rotation", CoatRotation);
            RG_READ_MTLX_INPUT("coat_IOR", CoatIOR);
            RG_READ_MTLX_INPUT("coat_affect_color", CoatAffectColor);
            RG_READ_MTLX_INPUT("coat_affect_roughness", CoatAffectRoughness);
            RG_READ_MTLX_INPUT("thin_film_thickness", ThinFilmThickness);
            RG_READ_MTLX_INPUT("thin_film_IOR", ThinFilmIOR);
            RG_READ_MTLX_INPUT("emission", Emission);
            RG_READ_MTLX_INPUT("emission_color", EmissionColor);
            RG_READ_MTLX_INPUT("opacity", Opacity);
#undef RG_READ_MTLX_INPUT

            if (const auto thinWalled = node->getInput("thin_walled"))
            {
                const auto value = thinWalled->getValue();
                if (value && value->isA<bool>())
                    data.ThinWalled = value->asA<bool>();
            }
        }

        // An input connected to an image node (directly or through a nodegraph output, which
        // getConnectedNode follows) becomes a texture reference; its factor is set to 1.
        void ReadStandardSurfaceTextures(const MaterialX::NodePtr& node, const std::filesystem::path& documentPath, MaterialXMaterial& data)
        {
            constexpr float3 kOne{ 1.0f, 1.0f, 1.0f };
            const std::pair<const char*, MaterialTextureSlot> textureInputs[] =
            {
                { "base_color", MaterialTextureSlot::BaseColor },
                { "metalness", MaterialTextureSlot::Metalness },
                { "specular", MaterialTextureSlot::Specular },
                { "specular_color", MaterialTextureSlot::SpecularColor },
                { "specular_roughness", MaterialTextureSlot::SpecularRoughness },
                { "normal", MaterialTextureSlot::Normal },
                { "transmission", MaterialTextureSlot::Transmission },
                { "transmission_color", MaterialTextureSlot::TransmissionColor },
                { "emission", MaterialTextureSlot::Emission },
                { "emission_color", MaterialTextureSlot::EmissionColor },
                { "opacity", MaterialTextureSlot::Opacity },
                { "coat", MaterialTextureSlot::Coat },
                { "coat_color", MaterialTextureSlot::CoatColor },
                { "coat_roughness", MaterialTextureSlot::CoatRoughness },
                { "sheen", MaterialTextureSlot::Sheen },
                { "sheen_color", MaterialTextureSlot::SheenColor },
                { "sheen_roughness", MaterialTextureSlot::SheenRoughness },
                { "subsurface", MaterialTextureSlot::Subsurface },
                { "subsurface_color", MaterialTextureSlot::SubsurfaceColor }
            };

            for (const auto& [inputName, slot] : textureInputs)
            {
                const auto imageNode = node->getConnectedNode(inputName);
                if (!imageNode || imageNode->getCategory() != "image")
                    continue;

                switch (slot)
                {
                case MaterialTextureSlot::BaseColor: data.BaseColor = kOne; break;
                case MaterialTextureSlot::Metalness: data.Metalness = 1.0f; break;
                case MaterialTextureSlot::Specular: data.Specular = 1.0f; break;
                case MaterialTextureSlot::SpecularColor: data.SpecularColor = kOne; break;
                case MaterialTextureSlot::SpecularRoughness: data.SpecularRoughness = 1.0f; break;
                case MaterialTextureSlot::Transmission: data.Transmission = 1.0f; break;
                case MaterialTextureSlot::TransmissionColor: data.TransmissionColor = kOne; break;
                case MaterialTextureSlot::Emission: data.Emission = 1.0f; break;
                case MaterialTextureSlot::EmissionColor: data.EmissionColor = kOne; break;
                case MaterialTextureSlot::Opacity: data.Opacity = kOne; break;
                case MaterialTextureSlot::Coat: data.Coat = 1.0f; break;
                case MaterialTextureSlot::CoatColor: data.CoatColor = kOne; break;
                case MaterialTextureSlot::CoatRoughness: data.CoatRoughness = 1.0f; break;
                case MaterialTextureSlot::Sheen: data.Sheen = 1.0f; break;
                case MaterialTextureSlot::SheenColor: data.SheenColor = kOne; break;
                case MaterialTextureSlot::SheenRoughness: data.SheenRoughness = 1.0f; break;
                case MaterialTextureSlot::Subsurface: data.Subsurface = 1.0f; break;
                case MaterialTextureSlot::SubsurfaceColor: data.SubsurfaceColor = kOne; break;
                case MaterialTextureSlot::Normal:
                case MaterialTextureSlot::Count:
                    break;
                }

                const auto fileInput = imageNode->getInput("file");
                if (!fileInput || !fileInput->getValue())
                    continue;

                std::filesystem::path texturePath(fileInput->getValue()->getValueString());
                if (texturePath.is_relative())
                    texturePath = documentPath.parent_path() / texturePath;
                data.Textures.push_back({ slot, texturePath.lexically_normal() });
            }
        }

        // ---- Textures -----------------------------------------------------------------------

        struct Image
        {
            uint32_t Width = 0;
            uint32_t Height = 0;
            std::vector<uint8_t> Pixels; // 'channels' bytes per pixel
        };

        std::optional<Image> LoadImageFile(const std::filesystem::path& path, int channels)
        {
            int width = 0, height = 0, fileChannels = 0;
            stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &fileChannels, channels);
            if (!pixels)
            {
                std::fprintf(stderr, "[MaterialX] Cannot load texture '%s': %s\n", path.string().c_str(), stbi_failure_reason());
                return std::nullopt;
            }
            Image image;
            image.Width = uint32_t(width);
            image.Height = uint32_t(height);
            image.Pixels.assign(pixels, pixels + size_t(width) * size_t(height) * size_t(channels));
            stbi_image_free(pixels);
            return image;
        }

        // RGBA8 texture with a full box-filtered mip chain (the material textures are 2k).
        TextureData MakeMippedTexture(uint32_t width, uint32_t height, std::vector<std::byte> pixels, TextureFormat format)
        {
            TextureData texture;
            texture.Width = width;
            texture.Height = height;
            texture.Format = format;

            TextureSubresource level;
            level.Width = width;
            level.Height = height;
            level.RowPitch = width * 4;
            level.SlicePitch = level.RowPitch * height;
            level.Pixels = std::move(pixels);
            texture.Subresources.push_back(std::move(level));

            while (texture.Subresources.back().Width > 1 || texture.Subresources.back().Height > 1)
            {
                const TextureSubresource& src = texture.Subresources.back();
                TextureSubresource dst;
                dst.MipLevel = src.MipLevel + 1;
                dst.Width = std::max(1u, src.Width / 2);
                dst.Height = std::max(1u, src.Height / 2);
                dst.RowPitch = dst.Width * 4;
                dst.SlicePitch = dst.RowPitch * dst.Height;
                dst.Pixels.resize(dst.SlicePitch);
                for (uint32_t y = 0; y < dst.Height; ++y)
                {
                    for (uint32_t x = 0; x < dst.Width; ++x)
                    {
                        const uint32_t x0 = std::min(x * 2, src.Width - 1), x1 = std::min(x * 2 + 1, src.Width - 1);
                        const uint32_t y0 = std::min(y * 2, src.Height - 1), y1 = std::min(y * 2 + 1, src.Height - 1);
                        for (uint32_t c = 0; c < 4; ++c)
                        {
                            const auto at = [&](uint32_t sx, uint32_t sy) { return uint32_t(src.Pixels[(size_t(sy) * src.Width + sx) * 4 + c]); };
                            dst.Pixels[(size_t(y) * dst.Width + x) * 4 + c] = std::byte(uint8_t((at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1) + 2) / 4));
                        }
                    }
                }
                texture.Subresources.push_back(std::move(dst));
            }
            texture.MipLevels = uint32_t(texture.Subresources.size());
            return texture;
        }

        uint32_t AddTexture(Scene& scene, TextureData texture)
        {
            scene.Textures.push_back(std::move(texture));
            return uint32_t(scene.Textures.size() - 1);
        }

        uint32_t AddColorTexture(Scene& scene, const std::filesystem::path& path, TextureFormat format)
        {
            std::optional<Image> image = LoadImageFile(path, 4);
            if (!image)
                return kNoTexture;
            std::vector<std::byte> pixels(image->Pixels.size());
            std::memcpy(pixels.data(), image->Pixels.data(), pixels.size());
            return AddTexture(scene, MakeMippedTexture(image->Width, image->Height, std::move(pixels), format));
        }

        // The renderer reads roughness from G and metallic from B (glTF packing). standard_surface
        // stores them as two scalar images; a missing one is 1 (its factor alone is then used).
        uint32_t AddMetallicRoughnessTexture(Scene& scene, const std::filesystem::path* roughnessPath, const std::filesystem::path* metalnessPath)
        {
            std::optional<Image> roughness = roughnessPath ? LoadImageFile(*roughnessPath, 1) : std::nullopt;
            std::optional<Image> metalness = metalnessPath ? LoadImageFile(*metalnessPath, 1) : std::nullopt;
            if (roughness && metalness && (roughness->Width != metalness->Width || roughness->Height != metalness->Height))
            {
                std::fprintf(stderr, "[MaterialX] Roughness and metalness textures differ in size; metalness texture ignored\n");
                metalness.reset();
            }
            const Image* size = roughness ? &*roughness : metalness ? &*metalness : nullptr;
            if (!size)
                return kNoTexture;

            const size_t count = size_t(size->Width) * size->Height;
            std::vector<std::byte> pixels(count * 4);
            for (size_t i = 0; i < count; ++i)
            {
                pixels[i * 4 + 0] = std::byte(0);
                pixels[i * 4 + 1] = std::byte(roughness ? roughness->Pixels[i] : 255);
                pixels[i * 4 + 2] = std::byte(metalness ? metalness->Pixels[i] : 255);
                pixels[i * 4 + 3] = std::byte(255);
            }
            return AddTexture(scene, MakeMippedTexture(size->Width, size->Height, std::move(pixels), TextureFormat::RGBA8_UNORM));
        }

        const char* ToString(MaterialTextureSlot slot)
        {
            constexpr const char* kNames[] = { "base_color", "metalness", "specular", "specular_color", "specular_roughness", "normal",
                "transmission", "transmission_color", "emission", "emission_color", "opacity", "coat", "coat_color", "coat_roughness",
                "sheen", "sheen_color", "sheen_roughness", "subsurface", "subsurface_color" };
            static_assert(std::size(kNames) == size_t(MaterialTextureSlot::Count));
            return slot < MaterialTextureSlot::Count ? kNames[size_t(slot)] : "?";
        }
    }

    std::optional<MaterialXMaterial> ImportMaterialX(const std::filesystem::path& path)
    {
        try
        {
            auto document = MaterialX::createDocument();
            MaterialX::readFromXmlFile(document, path.string());

            auto materialNodes = document->getMaterialNodes();
            if (materialNodes.empty())
            {
                std::fprintf(stderr, "[MaterialX] '%s' contains no material nodes\n", path.string().c_str());
                return std::nullopt;
            }

            MaterialX::NodePtr materialNode;
            for (const auto& candidate : materialNodes)
            {
                if (candidate->getName() == path.stem().string())
                {
                    materialNode = candidate;
                    break;
                }
            }
            if (!materialNode && materialNodes.size() == 1)
                materialNode = materialNodes.front();
            if (!materialNode)
            {
                std::fprintf(stderr, "[MaterialX] '%s' has multiple materials; name one after the file to select it\n", path.string().c_str());
                return std::nullopt;
            }

            MaterialXMaterial data;
            data.Name = materialNode->getName();
            const auto shaderNode = FindSurfaceShader(materialNode);
            if (!shaderNode || shaderNode->getCategory() != "standard_surface")
            {
                std::fprintf(stderr, "[MaterialX] '%s': only standard_surface shaders are supported\n", path.string().c_str());
                return std::nullopt;
            }
            ReadStandardSurface(shaderNode, data);
            ReadStandardSurfaceTextures(shaderNode, path, data);
            return data;
        }
        catch (const std::exception& exception)
        {
            std::fprintf(stderr, "[MaterialX] Failed to import '%s': %s\n", path.string().c_str(), exception.what());
            return std::nullopt;
        }
    }

    uint32_t AddMaterialX(Scene& scene, const MaterialXMaterial& source)
    {
        const std::filesystem::path* roughnessPath = nullptr;
        const std::filesystem::path* metalnessPath = nullptr;

        MaterialData material;
        for (const MaterialTextureReference& texture : source.Textures)
        {
            switch (texture.Slot)
            {
            case MaterialTextureSlot::BaseColor: material.BaseColorTexture = AddColorTexture(scene, texture.Path, TextureFormat::SRGBA8_UNORM); break;
            case MaterialTextureSlot::Normal: material.NormalTexture = AddColorTexture(scene, texture.Path, TextureFormat::RGBA8_UNORM); break;
            case MaterialTextureSlot::EmissionColor: material.EmissiveTexture = AddColorTexture(scene, texture.Path, TextureFormat::SRGBA8_UNORM); break;
            case MaterialTextureSlot::SpecularRoughness: roughnessPath = &texture.Path; break;
            case MaterialTextureSlot::Metalness: metalnessPath = &texture.Path; break;
            default:
                std::fprintf(stderr, "[MaterialX] %s: texture on '%s' is not supported by the renderer, ignored\n", source.Name.c_str(), ToString(texture.Slot));
                break;
            }
        }
        if (roughnessPath || metalnessPath)
            material.MetallicRoughnessTexture = AddMetallicRoughnessTexture(scene, roughnessPath, metalnessPath);

        // standard_surface -> renderer material. base_color only drives the diffuse lobe, which
        // transmission removes (weight 1 - transmission); the transmitted light is tinted by
        // transmission_color. The dielectric reflection uses specular_IOR, weighted by specular.
        // Not represented: specular_color, transmission depth / scatter / dispersion (the tint is
        // applied as is, like MaterialX's real-time shaders), coat, sheen, subsurface, thin film.
        const float3 color = source.BaseColor * source.Base;
        material.BaseColor[0] = color.x;
        material.BaseColor[1] = color.y;
        material.BaseColor[2] = color.z;
        material.Metallic = source.Metalness;
        material.Roughness = source.SpecularRoughness;
        material.TransmissionColor[0] = source.TransmissionColor.x;
        material.TransmissionColor[1] = source.TransmissionColor.y;
        material.TransmissionColor[2] = source.TransmissionColor.z;
        material.Transmission = std::clamp(source.Transmission, 0.0f, 1.0f);
        material.Specular = source.Specular;
        material.IOR = source.SpecularIOR;
        material.Emissive[0] = source.EmissionColor.x * source.Emission;
        material.Emissive[1] = source.EmissionColor.y * source.Emission;
        material.Emissive[2] = source.EmissionColor.z * source.Emission;

        std::printf("[MaterialX] %s: base color (%.3f, %.3f, %.3f), metallic %.2f, roughness %.2f, transmission %.2f tint (%.3f, %.3f, %.3f), specular %.2f, IOR %.2f, %zu texture(s)\n",
            source.Name.c_str(), color.x, color.y, color.z, material.Metallic, material.Roughness, material.Transmission,
            material.TransmissionColor[0], material.TransmissionColor[1], material.TransmissionColor[2], material.Specular, material.IOR, source.Textures.size());

        scene.Materials.push_back(material);
        return uint32_t(scene.Materials.size() - 1);
    }
}
