// Shared declarations. Binding convention: see Renderer/Renderer.h.
#pragma once

// b1: frame constants, written once per frame by the renderer (FrameConstants in Renderer.cpp).
cbuffer FrameConstants : register(b1)
{
    float4x4 gView;
    float4x4 gProjection;
    float4x4 gViewProjection;
    float4x4 gInverseViewProjection;
    float3 gCameraPosition;
    float gTime;
    float2 gResolution;
    float2 gInvResolution;
    float gDeltaTime;
    uint gFrameIndex;
    float2 gFramePadding;
};

// Output of Fullscreen.slang:VSMain, input of every fullscreen pixel shader.
struct FullscreenVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// Must match LightUpdatePass::GpuLight.
struct Light
{
    float3 position;
    float radius;
    float3 color;
    float intensity;
};
