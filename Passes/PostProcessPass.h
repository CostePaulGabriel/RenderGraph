#pragma once

#include "RenderGraph/RenderPass.h"

namespace LynxEngine
{
    // Exposure, tone mapping, gamma and vignette.
    //   in : color (HDR, sampled with a linear clamp sampler)
    //   out: output (LDR, back buffer format)
    class PostProcessPass : public RenderPass
    {
    public:
        struct Params // b0, must match PostProcess.slang
        {
            float Exposure = 1.0f;
            float Vignette = 0.35f;
            float Padding[2] = {};
        };

        explicit PostProcessPass(TextureFormat outputFormat = TextureFormat::RGBA8_UNORM)
            : RenderPass(PassType::Graphics)
        {
            Input("color");
            Output("output", { .Format = outputFormat });

            AddShader(ShaderType::Vertex, "Fullscreen.slang", "VSMain");
            AddShader(ShaderType::Pixel, "PostProcess.slang", "PSMain");
            AddSampler({ .AddressU = AddressMode::Clamp, .AddressV = AddressMode::Clamp, .AddressW = AddressMode::Clamp });
            SetDraw([](DrawContext& context) { context.DrawFullscreenTriangle(); });
            SetParams(m_Params);
        }

        const Params& GetParams() const { return m_Params; }
        void SetParams(const Params& params)
        {
            m_Params = params;
            SetConstants(m_Params);
        }

    private:
        Params m_Params;
    };
}
