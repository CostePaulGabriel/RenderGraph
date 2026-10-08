#pragma once

// MaterialX (.mtlx) import, ported from Idea's MaterialImporter (E:/AI/Claude/idea).
// Only standard_surface is read. The result is the MaterialX description; AddMaterialX turns it
// into what the renderer draws: a MaterialData (metallic-roughness) plus the TextureData it
// references, appended to the scene. Built into the sample application only (needs MaterialX
// and stb_image from vcpkg), RenderGraphCore stays dependency free.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Scene/Scene.h"

namespace LynxEngine
{
    enum class MaterialTextureSlot : uint8_t
    {
        BaseColor,
        Metalness,
        Specular,
        SpecularColor,
        SpecularRoughness,
        Normal,
        Transmission,
        TransmissionColor,
        Emission,
        EmissionColor,
        Opacity,
        Coat,
        CoatColor,
        CoatRoughness,
        Sheen,
        SheenColor,
        SheenRoughness,
        Subsurface,
        SubsurfaceColor,
        Count
    };

    // An image node connected to a standard_surface input. Path is resolved against the .mtlx folder.
    struct MaterialTextureReference
    {
        MaterialTextureSlot Slot = MaterialTextureSlot::BaseColor;
        std::filesystem::path Path;
    };

    // standard_surface inputs (defaults from the MaterialX specification). A textured input
    // is set to 1 (its texture is then the value).
    struct MaterialXMaterial
    {
        std::string Name;

        float Base = 1.0f;
        float3 BaseColor{ 0.8f, 0.8f, 0.8f };
        float DiffuseRoughness = 0.0f;
        float Metalness = 0.0f;
        float Specular = 1.0f;
        float3 SpecularColor{ 1.0f, 1.0f, 1.0f };
        float SpecularRoughness = 0.2f;
        float SpecularIOR = 1.5f;
        float SpecularAnisotropy = 0.0f;
        float SpecularRotation = 0.0f;
        float Transmission = 0.0f;
        float3 TransmissionColor{ 1.0f, 1.0f, 1.0f };
        float TransmissionDepth = 0.0f;
        float3 TransmissionScatter{ 0.0f, 0.0f, 0.0f };
        float TransmissionScatterAnisotropy = 0.0f;
        float TransmissionDispersion = 0.0f;
        float TransmissionExtraRoughness = 0.0f;
        float Subsurface = 0.0f;
        float3 SubsurfaceColor{ 1.0f, 1.0f, 1.0f };
        float3 SubsurfaceRadius{ 1.0f, 1.0f, 1.0f };
        float SubsurfaceScale = 1.0f;
        float SubsurfaceAnisotropy = 0.0f;
        float Sheen = 0.0f;
        float3 SheenColor{ 1.0f, 1.0f, 1.0f };
        float SheenRoughness = 0.3f;
        float Coat = 0.0f;
        float3 CoatColor{ 1.0f, 1.0f, 1.0f };
        float CoatRoughness = 0.1f;
        float CoatAnisotropy = 0.0f;
        float CoatRotation = 0.0f;
        float CoatIOR = 1.5f;
        float CoatAffectColor = 0.0f;
        float CoatAffectRoughness = 0.0f;
        float ThinFilmThickness = 0.0f;
        float ThinFilmIOR = 1.5f;
        float Emission = 0.0f;
        float3 EmissionColor{ 1.0f, 1.0f, 1.0f };
        float3 Opacity{ 1.0f, 1.0f, 1.0f };
        bool ThinWalled = false;

        std::vector<MaterialTextureReference> Textures;
    };

    // Reads the material named after the file (or the only material of the document).
    std::optional<MaterialXMaterial> ImportMaterialX(const std::filesystem::path& path);

    // Loads the referenced textures into scene.Textures and appends the converted MaterialData
    // to scene.Materials. Returns the material index.
    uint32_t AddMaterialX(Scene& scene, const MaterialXMaterial& material);
}
