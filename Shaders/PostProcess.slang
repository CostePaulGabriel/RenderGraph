#include "Common.slang"

// PostProcessPass: field color -> t0, sampler -> s0, params -> b0.
cbuffer PostProcessParams : register(b0)
{
    float gExposure;
    float gVignette;
    float2 gPostPadding;
};

Texture2D<float4> gColor : register(t0);
SamplerState gLinearClamp : register(s0);

float3 ACESFilm(float3 x)
{
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

[shader("fragment")]
float4 PSMain(FullscreenVertex input) : SV_Target0
{
    const float3 hdr = gColor.Sample(gLinearClamp, input.uv).rgb * gExposure;
    float3 ldr = pow(ACESFilm(hdr), 1.0 / 2.2);

    const float2 centered = input.uv - 0.5;
    ldr *= saturate(1.0 - gVignette * dot(centered, centered) * 2.0);
    return float4(ldr, 1.0);
}
