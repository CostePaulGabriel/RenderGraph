#include "Common.slang"

// Used by DepthPass (VSMain only) and GBufferPass (VSMain + PSGBuffer). Sharing the vertex
// shader keeps the depth of both passes bit-identical for the LessOrEqual depth test.

// b2: per-draw push constants (SceneDrawConstants in Passes/SceneDraw.h).
cbuffer DrawConstants : register(b2)
{
    float4x4 gWorld;
    uint gMaterialIndex;
};

// Scene material table, bound because GBufferPass declares SetUsesMaterials(true).
// Must match GpuMaterial in Renderer.cpp. Texture fields are bindless descriptor indices.
struct MaterialConstants
{
    float4 baseColor;
    float3 emissive;
    float metallic;
    float roughness;
    float normalScale;
    float occlusionStrength;
    uint baseColorTexture;
    uint metallicRoughnessTexture;
    uint normalTexture;
    uint occlusionTexture;
    uint emissiveTexture;
};
static const uint kNoTexture = 0xFFFFFFFF;

StructuredBuffer<MaterialConstants> gMaterials : register(t0, space1);
Texture2D gBindlessTextures[] : register(t0, space2); // DescriptorTableManager

SamplerState gMaterialSampler : register(s0);

float4 SampleMaterialTexture(uint index, float2 uv, float4 fallback)
{
    if (index == kNoTexture)
        return fallback;
    return gBindlessTextures[NonUniformResourceIndex(index)].Sample(gMaterialSampler, uv);
}

struct SceneVertex
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
};

struct SceneVaryings
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD1;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
};

struct GBufferTargets
{
    float4 albedo : SV_Target0;   // SRGBA8: base color, A = occlusion
    float4 normal : SV_Target1;   // RGBA16F: world normal
    float4 material : SV_Target2; // RGBA8: R = metallic, G = roughness
    float4 emissive : SV_Target3; // RGBA16F: emitted radiance
};

[shader("vertex")]
SceneVaryings VSMain(SceneVertex input)
{
    const float4 world = mul(gWorld, float4(input.position, 1.0));

    SceneVaryings output;
    output.position = mul(gViewProjection, world);
    output.worldPosition = world.xyz;
    output.normal = mul((float3x3)gWorld, input.normal);
    output.texCoord = input.texCoord;
    return output;
}

// Tangent frame from screen-space derivatives, so meshes need no tangent attribute.
// Solves dP = T * dU + B * dV for both screen axes; T follows +U, B follows +V (down the image).
float3 ApplyNormalMap(float3 N, float3 worldPosition, float2 uv, float3 tangentNormal)
{
    const float3 dp1 = ddx(worldPosition);
    const float3 dp2 = ddy(worldPosition);
    const float2 duv1 = ddx(uv);
    const float2 duv2 = ddy(uv);

    const float det = duv1.x * duv2.y - duv2.x * duv1.y;
    float3 T = (dp1 * duv2.y - dp2 * duv1.y) * sign(det);
    float3 B = (dp2 * duv1.x - dp1 * duv2.x) * sign(det);
    T -= N * dot(N, T); // Gram-Schmidt against the interpolated normal
    B -= N * dot(N, B);
    if (dot(T, T) < 1e-20 || dot(B, B) < 1e-20)
        return N;         // degenerate UVs: keep the vertex normal
    T = normalize(T);
    B = normalize(B);

    // Normal maps use +Y up (glTF), the image V axis goes down: Y maps to -B.
    return normalize(T * tangentNormal.x - B * tangentNormal.y + N * tangentNormal.z);
}

[shader("fragment")]
GBufferTargets PSGBuffer(SceneVaryings input)
{
    const MaterialConstants material = gMaterials[gMaterialIndex];
    const float2 uv = input.texCoord;

    // Sampled textures are linear: color textures use sRGB formats.
    const float4 baseColor = material.baseColor * SampleMaterialTexture(material.baseColorTexture, uv, 1.0);
    const float4 metallicRoughness = SampleMaterialTexture(material.metallicRoughnessTexture, uv, 1.0);
    const float occlusion = lerp(1.0, SampleMaterialTexture(material.occlusionTexture, uv, 1.0).r, material.occlusionStrength);
    const float3 emissive = material.emissive * SampleMaterialTexture(material.emissiveTexture, uv, 1.0).rgb;

    float3 normal = normalize(input.normal);
    if (material.normalTexture != kNoTexture)
    {
        float3 tangentNormal = SampleMaterialTexture(material.normalTexture, uv, float4(0.5, 0.5, 1.0, 1.0)).xyz * 2.0 - 1.0;
        tangentNormal.xy *= material.normalScale;
        normal = ApplyNormalMap(normal, input.worldPosition, uv, normalize(tangentNormal));
    }

    GBufferTargets output;
    output.albedo = float4(baseColor.rgb, occlusion);
    output.normal = float4(normal, 0.0);
    output.material = float4(saturate(material.metallic * metallicRoughness.b), saturate(material.roughness * metallicRoughness.g), 0.0, 0.0);
    output.emissive = float4(emissive, 0.0);
    return output;
}
