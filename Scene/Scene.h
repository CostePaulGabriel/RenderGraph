#pragma once

// Scene data used by the sample. It is plain data built from the reference structures
// (MeshData, MaterialData, TextureData) and is independent from the RenderGraph: passes only
// draw it through their DrawContext (see Passes/SceneDraw.h) and say whether they need its materials.

#include <vector>

#include "Scene/MaterialData.h"
#include "Scene/Math.h"
#include "Scene/MeshData.h"
#include "Scene/PerspectiveCamera.h"
#include "Scene/TextureData.h"

namespace LynxEngine
{
    struct MeshInstance
    {
        uint32_t Mesh = 0;     // index into Scene::Meshes
        uint32_t Material = 0; // index into Scene::Materials
        float4x4 Transform;
    };

    struct Scene
    {
        std::vector<MeshData> Meshes;
        std::vector<TextureData> Textures;
        std::vector<MaterialData> Materials;
        std::vector<MeshInstance> Instances;
        PerspectiveCamera MainCamera; // the application keeps its aspect ratio in sync with the back buffer
    };

    // Procedural content for the sample (no asset loading in the prototype).
    MeshData CreateCubeMesh();
    MeshData CreatePlaneMesh(float halfSize, float uvScale);
    MeshData CreateSphereMesh(uint32_t segments, uint32_t rings);
    // Colors are 0xAABBGGRR. Use SRGBA8_UNORM for colors, RGBA8_UNORM for data (roughness / metallic).
    TextureData CreateCheckerTexture(uint32_t size, uint32_t cells, uint32_t colorA, uint32_t colorB, TextureFormat format = TextureFormat::SRGBA8_UNORM);
    // Tangent-space normal map of square tiles with bevelled edges (+Y up).
    TextureData CreateTileNormalTexture(uint32_t size, uint32_t cells, float bevel, float strength);

    Scene CreateDemoScene();
    void AnimateDemoScene(Scene& scene, float time);
}
