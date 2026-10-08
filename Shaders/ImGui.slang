// OverlayPass: draws ImGui draw lists (Application/ImGuiDraw.cpp).
// t0 = draw texture (DrawContext::SetTexture), s0 = sampler declared by the pass, b2 = projection.
cbuffer UiConstants : register(b2)
{
    float4x4 gUiProjection;
};

Texture2D gUiTexture : register(t0);
SamplerState gUiSampler : register(s0);

struct UiVertex
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct UiVaryings
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
UiVaryings VSMain(UiVertex input)
{
    UiVaryings output;
    output.position = mul(gUiProjection, float4(input.position, 0.0, 1.0));
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 PSMain(UiVaryings input) : SV_Target0
{
    return input.color * gUiTexture.Sample(gUiSampler, input.uv);
}
