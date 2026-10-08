#include "Scene.slang"
#include "PBR.slang"

// ForwardPass: transmissive materials (glass) drawn over the lit scene. Vertex shader: Scene.slang:VSMain.
// scene -> t0, lights -> t1 (declaration order), material table and textures as in GBufferPass.
cbuffer LightingParams : register(b0) // LightingPass::Params, same values as the Lighting pass
{
    float3 gSunDirection;
    float gSunIntensity;
    float3 gAmbient;
    float gPointLightScale;
    float4x4 gLightViewProjection;
};

Texture2D<float4> gSceneColor : register(t0); // lit HDR scene without the glass
StructuredBuffer<Light> gLights : register(t1);
SamplerState gSceneSampler : register(s1);    // linear clamp

static const float3 kSunColor = float3(1.0, 0.95, 0.85);

// Same sky as Lighting.slang.
float3 SkyRadiance(float3 direction)
{
    const float3 ground = gAmbient * 0.25;
    return lerp(ground, gAmbient, saturate(direction.y * 0.5 + 0.5));
}

// Lit scene seen through the surface: the refracted ray travels 'thickness' inside the object,
// the scene behind the exit point is read from the lit image (screen-space refraction).
// Rough surfaces blur it with a small disc.
float3 RefractedScene(float3 worldPosition, float3 refracted, float thickness, float roughness)
{
    const float4 clip = mul(gViewProjection, float4(worldPosition + refracted * thickness, 1.0));
    const float2 uv = saturate(clip.xy / clip.w * float2(0.5, -0.5) + 0.5);

    const float radius = roughness * roughness * 0.05; // uv units
    const float2 taps[5] = { float2(0, 0), float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1) };
    float3 color = 0.0;
    for (uint i = 0; i < 5; ++i)
        color += gSceneColor.SampleLevel(gSceneSampler, uv + taps[i] * radius, 0).rgb;
    return color / 5.0;
}

[shader("fragment")]
float4 PSForward(SceneVaryings input) : SV_Target0
{
    const MaterialConstants material = gMaterials[gMaterialIndex];
    const float2 uv = input.texCoord;
    const float3 V = normalize(gCameraPosition - input.worldPosition);

    float3 normal = normalize(input.normal);
    if (dot(normal, V) < 0.0)
        discard; // back faces: the front face already accounts for the whole path through the object
    if (material.normalTexture != kNoTexture)
    {
        float3 tangentNormal = SampleMaterialTexture(material.normalTexture, uv, float4(0.5, 0.5, 1.0, 1.0)).xyz * 2.0 - 1.0;
        tangentNormal.xy *= material.normalScale;
        normal = ApplyNormalMap(normal, input.worldPosition, uv, normalize(tangentNormal));
    }

    const float4 metallicRoughness = SampleMaterialTexture(material.metallicRoughnessTexture, uv, 1.0);

    Surface surface;
    surface.baseColor = material.baseColor.rgb * SampleMaterialTexture(material.baseColorTexture, uv, 1.0).rgb;
    surface.metallic = saturate(material.metallic * metallicRoughness.b);
    surface.roughness = saturate(material.roughness * metallicRoughness.g);
    surface.occlusion = 1.0;
    surface.normal = normal;
    surface.transmission = saturate(material.transmission);
    surface.specular = saturate(material.specular);
    surface.ior = max(material.ior, 1.0);

    const float NoV = max(dot(normal, V), 1e-4);

    // Direct light: specular highlights (and the diffuse that transmission leaves).
    float3 color = EvaluateLight(surface, V, normalize(-gSunDirection), kSunColor * gSunIntensity);
    uint count, stride;
    gLights.GetDimensions(count, stride);
    for (uint i = 0; i < count; ++i)
    {
        const Light light = gLights[i];
        const float3 toLight = light.position - input.worldPosition;
        const float distanceSq = dot(toLight, toLight);
        if (distanceSq >= light.radius * light.radius)
            continue;
        const float ratio = distanceSq / (light.radius * light.radius);
        const float window = saturate(1.0 - ratio * ratio);
        const float attenuation = window * window / (distanceSq + 1.0);
        color += EvaluateLight(surface, V, toLight * rsqrt(max(distanceSq, 1e-8)), light.color * light.intensity * attenuation * gPointLightScale);
    }

    // Reflection of the sky, weighted by the Fresnel of the dielectric (IOR, specular weight).
    const float3 reflected = reflect(-V, normal);
    const float3 reflectance = EnvBRDFApprox(SpecularColor(surface), max(surface.roughness, kMinRoughness), NoV) * SpecularWeight(surface);
    color += reflectance * lerp(SkyRadiance(reflected), SkyRadiance(normal), surface.roughness);
    color += DiffuseColor(surface) * SkyRadiance(normal);

    // Transmission: what is not reflected passes through, tinted by the transmission color.
    const float transmission = surface.transmission * (1.0 - surface.metallic);
    if (transmission > 0.0)
    {
        float3 refracted = refract(-V, normal, 1.0 / surface.ior);
        if (dot(refracted, refracted) < 1e-6)
            refracted = reflected; // total internal reflection
        const float thickness = length(mul(gWorld, float4(1.0, 0.0, 0.0, 0.0)).xyz); // size of the unit mesh in world units
        color += transmission * (1.0 - reflectance) * material.transmissionColor
            * RefractedScene(input.worldPosition, refracted, thickness, surface.roughness);
    }

    color += material.emissive * SampleMaterialTexture(material.emissiveTexture, uv, 1.0).rgb;
    return float4(color, 1.0);
}
