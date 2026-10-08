#include "Common.slang"

// One triangle covering the screen, no vertex buffer (DrawContext::DrawFullscreenTriangle).
[shader("vertex")]
FullscreenVertex VSMain(uint vertexId : SV_VertexID)
{
    FullscreenVertex output;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}
