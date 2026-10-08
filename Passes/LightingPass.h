#pragma once

#include "RenderGraph/RenderPass.h"

namespace LynxEngine
{
    // Deferred PBR lighting (GGX / Smith / Schlick, metallic-roughness) on a fullscreen triangle.
    //   in : albedo, normal, material, emissive, depth (SRV t0..t4), lights (StructuredBuffer t5)
    //   out: color (RGBA16F HDR)
    class LightingPass : public RenderPass
    {
    public:
        struct Params // b0, must match Lighting.slang
        {
            float SunDirection[3] = { -0.4f, -1.0f, 0.3f };
            float SunIntensity = 1.2f;
            float Ambient[3] = { 0.20f, 0.24f, 0.30f }; // sky radiance (the ground gets a darker share)
            float PointLightScale = 1.0f;
        };

        LightingPass()
            : RenderPass(PassType::Graphics)
        {
            Input("albedo");
            Input("normal");
            Input("material");
            Input("emissive");
            Input("depth");
            InputBuffer("lights");
            Output("color", { .Format = TextureFormat::RGBA16_FLOAT });

            AddShader(ShaderType::Vertex, "Fullscreen.slang", "VSMain");
            AddShader(ShaderType::Pixel, "Lighting.slang", "PSMain");
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
