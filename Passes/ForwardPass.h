#pragma once

#include "Passes/LightingPass.h"
#include "Passes/SceneDraw.h"

namespace LynxEngine
{
    // Copies the lit image into a new texture that ForwardPass then draws into in place
    // (a pass cannot sample the texture it renders to).
    //   in : source (CopySource)
    //   out: color  (RGBA16F HDR, CopyDest)
    class SceneColorCopyPass : public RenderPass
    {
    public:
        SceneColorCopyPass()
            : RenderPass(PassType::Copy)
        {
            Input("source", ResourceUsage::CopySource);
            Output("color", { .Format = TextureFormat::RGBA16_FLOAT }, ResourceUsage::CopyDest);
        }
    };

    // Draws the transmissive instances (glass) over the lit scene, after Lighting. They are left
    // out of Depth / GBuffer (DrawOpaqueScene), so the scene behind them is lit and visible.
    //   in : scene  (lit HDR color, sampled along the refracted ray, t0)
    //        lights (point lights, t1)
    //        depth  (read-only depth attachment: glass hidden by opaque objects is not drawn)
    //   in/out: color (copy of the lit color, the glass is written over it)
    // Shading: Shaders/Forward.slang, same lights and sky as Lighting.slang.
    class ForwardPass : public RenderPass
    {
    public:
        using Params = LightingPass::Params; // b0, the lighting of the scene

        ForwardPass()
            : RenderPass(PassType::Graphics)
        {
            Input("scene");
            InputBuffer("lights");
            Input("depth", ResourceUsage::DepthRead);
            InOut("color", ResourceUsage::RenderTarget);

            AddShader(ShaderType::Vertex, "Scene.slang", "VSMain");
            AddShader(ShaderType::Pixel, "Forward.slang", "PSForward");
            AddSampler({ .MinFilter = FilterMode::Anisotropic, .MagFilter = FilterMode::Anisotropic }); // s0: material textures
            AddSampler({ .AddressU = AddressMode::Clamp, .AddressV = AddressMode::Clamp, .AddressW = AddressMode::Clamp }); // s1: scene color

            DeclareSceneGeometry(*this);
            SetDraw(DrawTransmissiveScene);
            SetUsesMaterials(true);
            State().Depth = { .TestEnable = true, .WriteEnable = false, .Func = ComparisonFunc::LessOrEqual };
            SetParams({});
        }

        void SetParams(const Params& params) { SetConstants(params); }
    };
}
