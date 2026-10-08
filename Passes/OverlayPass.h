#pragma once

#include "RenderGraph/RenderPass.h"

namespace LynxEngine
{
    // Draws a user interface on top of an existing image, in place.
    //   in/out: target (render target, alpha blended)
    // The pass declares the ImGui vertex layout, a per-draw texture and a projection matrix as
    // push constants. RenderGraphCore does not depend on ImGui, so the draw function is set by
    // the application (see Application/ImGuiDraw.h): overlay.SetDraw(DrawImGui).
    class OverlayPass : public RenderPass
    {
    public:
        explicit OverlayPass(DrawFunction draw = {})
            : RenderPass(PassType::Graphics)
        {
            InOut("target", ResourceUsage::RenderTarget);

            AddShader(ShaderType::Vertex, "ImGui.slang", "VSMain");
            AddShader(ShaderType::Pixel, "ImGui.slang", "PSMain");
            AddSampler({ .AddressU = AddressMode::Clamp, .AddressV = AddressMode::Clamp, .AddressW = AddressMode::Clamp });

            // ImDrawVert: float2 pos, float2 uv, uint32 col (checked in Application/ImGuiDraw.cpp).
            SetVertexLayout({ {
                .Attributes = {
                    { VertexSemantic::Position, VertexFormat::Float32x2, 0, 0 },
                    { VertexSemantic::TexCoord, VertexFormat::Float32x2, 0, 8 },
                    { VertexSemantic::Color, VertexFormat::UInt8x4Normalized, 0, 16 } },
                .Stride = 20 } });
            SetUsesDrawTexture(true);
            SetPushConstantsSize(16 * sizeof(float)); // float4x4 projection
            SetDraw(std::move(draw));

            PipelineState& state = State();
            state.Blend = {
                .Enable = true,
                .SrcColor = BlendFactor::SrcAlpha,
                .DstColor = BlendFactor::InvSrcAlpha,
                .ColorOp = BlendOp::Add,
                .SrcAlpha = BlendFactor::One,
                .DstAlpha = BlendFactor::InvSrcAlpha,
                .AlphaOp = BlendOp::Add,
            };
            state.Raster = { .CullMode = RasterCullMode::None, .ScissorEnable = true };
            state.Depth = { .TestEnable = false, .WriteEnable = false };
        }
    };
}
