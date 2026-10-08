#pragma once

#include "Passes/SceneDraw.h"

namespace LynxEngine
{
    // Depth pre-pass: draws the scene into a new depth buffer.
    //   out: depth (D32, cleared to 1)
    class DepthPass : public RenderPass
    {
    public:
        DepthPass()
            : RenderPass(PassType::Graphics)
        {
            Output("depth", { .Format = TextureFormat::D32, .ClearValue = { 1.0f, 0.0f, 0.0f, 0.0f } }, ResourceUsage::DepthStencil).Clear = true;

            AddShader(ShaderType::Vertex, "Scene.slang", "VSMain");
            DeclareSceneGeometry(*this);
            SetDraw(DrawOpaqueScene);
            State().Depth = { .TestEnable = true, .WriteEnable = true, .Func = ComparisonFunc::Less };
        }
    };
}
