#include "Common.slang"

// DebugNormalsPass: field normal -> t0.
Texture2D<float4> gNormal : register(t0);

[shader("fragment")]
float4 PSMain(FullscreenVertex input) : SV_Target0
{
    const float3 normal = normalize(gNormal.Load(int3(int2(input.position.xy), 0)).xyz);
    // Pre-compensate the gamma applied by PostProcess so the colors stay readable.
    return float4(pow(normal * 0.5 + 0.5, 2.2) * 1.6, 1.0);
}
