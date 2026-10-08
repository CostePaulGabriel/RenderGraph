#pragma once

#include "RenderGraph/RenderPass.h"

namespace LynxEngine
{
    // Visualizes the G-buffer normals. Only runs when something consumes its output; in the
    // default graph nothing does, so the compiler culls it.
    //   in : normal
    //   out: color (RGBA16F, same format as LightingPass so it can replace it)
    class DebugNormalsPass : public RenderPass
    {
    public:
        DebugNormalsPass()
            : RenderPass(PassType::Graphics)
        {
            Input("normal");
            Output("color", { .Format = TextureFormat::RGBA16_FLOAT });

            AddShader(ShaderType::Vertex, "Fullscreen.slang", "VSMain");
            AddShader(ShaderType::Pixel, "DebugNormals.slang", "PSMain");
            SetDraw([](DrawContext& context) { context.DrawFullscreenTriangle(); });
        }
    };
}
