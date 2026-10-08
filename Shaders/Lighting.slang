#include "Common.slang"
#include "PBR.slang"

// LightingPass: fields albedo, normal, material, emissive, depth -> t0..t4, lights -> t5
// (declaration order). Deferred metallic-roughness shading of the G-buffer written by Scene.slang.
cbuffer LightingParams : register(b0)
{
    float3 gSunDirection;
    float gSunIntensity;
    float3 gAmbient;       // sky radiance
    float gPointLightScale;
};

Texture2D<float4> gAlbedo : register(t0);   // base color, A = occlusion
Texture2D<float4> gNormal : register(t1);
Texture2D<float4> gMaterial : register(t2); // R = metallic, G = roughness
Texture2D<float4> gEmissive : register(t3);
Texture2D<float> gDepth : register(t4);
StructuredBuffer<Light> gLights : register(t5);

static const float3 kSunColor = float3(1.0, 0.95, 0.85);

// Simple sky: gAmbient above the horizon, a darker ground bounce below.
float3 SkyRadiance(float3 direction)
{
    const float3 ground = gAmbient * 0.25;
    return lerp(ground, gAmbient, saturate(direction.y * 0.5 + 0.5));
}

[shader("fragment")]
float4 PSMain(FullscreenVertex input) : SV_Target0
{
    const int3 pixel = int3(int2(input.position.xy), 0);
    const float depth = gDepth.Load(pixel);
    if (depth >= 1.0)
        return float4(lerp(float3(0.30, 0.42, 0.65), float3(0.03, 0.05, 0.10), input.uv.y), 1.0);

    const float4 albedo = gAlbedo.Load(pixel);
    const float2 metallicRoughness = gMaterial.Load(pixel).rg;

    Surface surface;
    surface.baseColor = albedo.rgb;
    surface.occlusion = albedo.a;
    surface.metallic = metallicRoughness.r;
    surface.roughness = metallicRoughness.g;
    surface.normal = normalize(gNormal.Load(pixel).xyz);

    // Reconstruct the world position from depth.
    const float4 ndc = float4(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0, depth, 1.0);
    float4 world = mul(gInverseViewProjection, ndc);
    world.xyz /= world.w;

    const float3 V = normalize(gCameraPosition - world.xyz);
    const float NoV = max(dot(surface.normal, V), 1e-4);

    // Sun.
    float3 color = EvaluateLight(surface, V, normalize(-gSunDirection), kSunColor * gSunIntensity);

    // Point lights: inverse-square falloff, windowed to reach zero at the light radius.
    uint count, stride;
    gLights.GetDimensions(count, stride);
    for (uint i = 0; i < count; ++i)
    {
        const Light light = gLights[i];
        const float3 toLight = light.position - world.xyz;
        const float distanceSq = dot(toLight, toLight);
        if (distanceSq >= light.radius * light.radius)
            continue;
        const float ratio = distanceSq / (light.radius * light.radius);
        const float window = saturate(1.0 - ratio * ratio);
        const float attenuation = window * window / (distanceSq + 1.0);
        color += EvaluateLight(surface, V, toLight * rsqrt(max(distanceSq, 1e-8)), light.color * light.intensity * attenuation * gPointLightScale);
    }

    // Ambient: hemisphere irradiance for the diffuse part, the sky in the reflected direction
    // weighted by the split-sum BRDF for the specular part. No image-based lighting yet.
    const float3 reflected = reflect(-V, surface.normal);
    const float3 ambientDiffuse = DiffuseColor(surface) * SkyRadiance(surface.normal);
    const float3 ambientSpecular = EnvBRDFApprox(SpecularColor(surface), max(surface.roughness, kMinRoughness), NoV)
        * lerp(SkyRadiance(reflected), SkyRadiance(surface.normal), surface.roughness);
    color += (ambientDiffuse + ambientSpecular) * surface.occlusion;

    color += gEmissive.Load(pixel).rgb;
    return float4(color, 1.0);
}
