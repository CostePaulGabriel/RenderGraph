#pragma once

#include "Passes/SceneDraw.h"

namespace LynxEngine
{
    // Directional-light shadow map: draws the scene from the sun into a fixed-size depth texture.
    //   out: shadow (D32, cleared to 1), read by Lighting
    // The light matrix is an orthographic box around the scene origin; it must match the one
    // given to LightingPass (see BuildDemoGraph).
    class ShadowPass : public RenderPass
    {
    public:
        static constexpr uint32_t kResolution = 2048;
        static constexpr float kSceneRadius = 20.0f;

        struct Params // b0, must match Shadow.slang
        {
            float4x4 LightViewProjection;
        };

        // Sun view-projection (orthographic, left-handed, depth [0, 1]) for light travelling along sunDirection.
        static float4x4 ComputeLightViewProjection(const float3& sunDirection)
        {
            const float3 direction = Normalize(sunDirection);
            const float3 up = std::abs(direction.y) > 0.99f ? float3{ 0, 0, 1 } : float3{ 0, 1, 0 };
            const float4x4 view = float4x4::LookAtLH(direction * -kSceneRadius, {}, up);

            float4x4 projection; // box [-R, R] x [-R, R] x [0, 2R]
            projection.m[0][0] = 1.0f / kSceneRadius;
            projection.m[1][1] = 1.0f / kSceneRadius;
            projection.m[2][2] = 1.0f / (2.0f * kSceneRadius);
            return projection * view;
        }

        ShadowPass()
            : RenderPass(PassType::Graphics)
        {
            Output("shadow", { .Format = TextureFormat::D32, .SizeMode = TextureSizeMode::Fixed, .Width = kResolution, .Height = kResolution, .ClearValue = { 1.0f, 0.0f, 0.0f, 0.0f } }, ResourceUsage::DepthStencil).Clear = true;

            AddShader(ShaderType::Vertex, "Shadow.slang", "VSMain");
            DeclareSceneGeometry(*this);
            SetDraw(DrawScene);
            State().Depth = { .TestEnable = true, .WriteEnable = true, .Func = ComparisonFunc::Less };
        }

        void SetLightViewProjection(const float4x4& matrix) { SetConstants(Params{ matrix }); }
    };
}
