#pragma once

#include "Passes/SceneDraw.h"

namespace LynxEngine
{
    // Draws the scene with its PBR materials into the G-buffer, testing against the pre-pass depth.
    //   in : depth    (read-only depth attachment)
    //   out: albedo   (SRGBA8:  base color, A = ambient occlusion)
    //        normal   (RGBA16F: world-space normal, normal-mapped)
    //        material (RGBA8:   R = metallic, G = roughness)
    //        emissive (RGBA16F: emitted radiance)
    // The pass only states that it needs materials; the renderer binds the scene's material
    // table (MaterialConstants buffer + bindless textures). No Material object enters the pass.
    class GBufferPass : public RenderPass
    {
    public:
        GBufferPass()
            : RenderPass(PassType::Graphics)
        {
            Input("depth", ResourceUsage::DepthRead);
            Output("albedo", { .Format = TextureFormat::SRGBA8_UNORM }).Clear = true;
            Output("normal", { .Format = TextureFormat::RGBA16_FLOAT }).Clear = true;
            Output("material", { .Format = TextureFormat::RGBA8_UNORM }).Clear = true;
            Output("emissive", { .Format = TextureFormat::RGBA16_FLOAT }).Clear = true;

            AddShader(ShaderType::Vertex, "Scene.slang", "VSMain");
            AddShader(ShaderType::Pixel, "Scene.slang", "PSGBuffer");
            AddSampler({ .MinFilter = FilterMode::Anisotropic, .MagFilter = FilterMode::Anisotropic }); // wrap, for material textures

            DeclareSceneGeometry(*this);
            SetDraw(DrawOpaqueScene);
            SetUsesMaterials(true);
            State().Depth = { .TestEnable = true, .WriteEnable = false, .Func = ComparisonFunc::LessOrEqual };
        }
    };
}
