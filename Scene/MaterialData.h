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

        uint32_t BaseColorTexture = kNoTexture;          // sRGB color, alpha unused
        uint32_t MetallicRoughnessTexture = kNoTexture;  // G = roughness, B = metallic (glTF packing)
        uint32_t NormalTexture = kNoTexture;             // tangent space, +Y up (glTF / OpenGL convention)
        uint32_t OcclusionTexture = kNoTexture;          // R
        uint32_t EmissiveTexture = kNoTexture;           // sRGB color
    };
}
