// ShadowPass: depth-only draw of the scene from the sun. Per-draw constants are the same as Scene.slang.

cbuffer ShadowParams : register(b0)
{
    float4x4 gLightViewProjection;
};

cbuffer DrawConstants : register(b2)
{
    float4x4 gWorld;
    uint gMaterialIndex;
};

struct ShadowVertex
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
};

[shader("vertex")]
float4 VSMain(ShadowVertex input) : SV_Position
{
    return mul(gLightViewProjection, mul(gWorld, float4(input.position, 1.0)));
}
