#include "Common.slang"

// LightUpdatePass: animates the point lights on the GPU.
RWStructuredBuffer<Light> gLights : register(u0);

float3 HueToRgb(float hue)
{
    return saturate(float3(abs(hue * 6.0 - 3.0) - 1.0, 2.0 - abs(hue * 6.0 - 2.0), 2.0 - abs(hue * 6.0 - 4.0)));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint count, stride;
    gLights.GetDimensions(count, stride);
    if (id.x >= count)
        return;

    const float f = float(id.x) / float(max(count, 1u));
    const float angle = gTime * (0.3 + 0.4 * frac(f * 7.31)) + f * 6.2831853;
    const float radius = 2.0 + 4.5 * frac(f * 3.17 + 0.13);

    Light light;
    light.position = float3(cos(angle) * radius, 0.3 + 0.6 * frac(f * 5.13) + 0.2 * sin(gTime * 1.7 + f * 20.0), sin(angle) * radius);
    light.radius = 3.5;
    light.color = HueToRgb(f);
    light.intensity = 2.5;
    gLights[id.x] = light;
}
