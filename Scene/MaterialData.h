#pragma once

// PBR material, metallic-roughness workflow (same meaning as a glTF 2.0 material).
// Texture fields are indices into Scene::Textures, kNoTexture when unused. Factors multiply
// the texture values; without a texture the factor alone is used.
//
// This is the CPU description. The renderer converts it into the GPU layout (Shaders/Scene.slang
// MaterialConstants), replacing scene texture indices by bindless descriptor indices.
// @TODO : implement a structure that represents an MaterialX.

#include <cstdint>

namespace LynxEngine
{
    inline constexpr uint32_t kNoTexture = ~0u;

    struct MaterialData
    {
        float BaseColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // linear RGBA
        float Emissive[3] = { 0.0f, 0.0f, 0.0f };        // linear RGB, HDR
        float Metallic = 0.0f;
        float Roughness = 0.5f;                          // perceptual roughness
        float NormalScale = 1.0f;                        // scales the XY of the normal map
        float OcclusionStrength = 1.0f;

        // Dielectric layer and transmission (standard_surface meaning). The defaults give the
        // plain glTF dielectric: IOR 1.5 (F0 = 0.04), full specular weight, opaque.
        float TransmissionColor[3] = { 1.0f, 1.0f, 1.0f }; // linear RGB tint of the transmitted light
        float Transmission = 0.0f;                         // 0 = opaque, 1 = no diffuse, all refracted
        float Specular = 1.0f;                             // weight of the dielectric reflection
        float IOR = 1.5f;

        uint32_t BaseColorTexture = kNoTexture;          // sRGB color, alpha unused
        uint32_t MetallicRoughnessTexture = kNoTexture;  // G = roughness, B = metallic (glTF packing)
        uint32_t NormalTexture = kNoTexture;             // tangent space, +Y up (glTF / OpenGL convention)
        uint32_t OcclusionTexture = kNoTexture;          // R
        uint32_t EmissiveTexture = kNoTexture;           // sRGB color
    };
}
