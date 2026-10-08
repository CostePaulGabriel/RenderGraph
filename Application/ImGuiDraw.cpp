#include "Application/ImGuiDraw.h"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <vector>

#include "Scene/Math.h"

namespace LynxEngine
{
    // Must match the vertex layout declared by OverlayPass.
    static_assert(sizeof(ImDrawVert) == 20);
    static_assert(offsetof(ImDrawVert, pos) == 0 && offsetof(ImDrawVert, uv) == 8 && offsetof(ImDrawVert, col) == 16);

    void DrawImGui(DrawContext& context)
    {
        const ImDrawData* drawData = ImGui::GetCurrentContext() ? ImGui::GetDrawData() : nullptr;
        if (!drawData || drawData->CmdListsCount == 0 || drawData->TotalVtxCount == 0)
            return;

        std::vector<ImDrawVert> vertices;
        std::vector<ImDrawIdx> indices;
        vertices.reserve(size_t(drawData->TotalVtxCount));
        indices.reserve(size_t(drawData->TotalIdxCount));
        for (const ImDrawList* list : drawData->CmdLists)
        {
            vertices.insert(vertices.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
            indices.insert(indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
        }
        context.SetVertexData(0, std::as_bytes(std::span(vertices)));
        context.SetIndexData(std::as_bytes(std::span(indices)), sizeof(ImDrawIdx) == 2 ? IndexFormat::UInt16 : IndexFormat::UInt32);

        // Orthographic projection over the ImGui display rectangle.
        const float l = drawData->DisplayPos.x, r = l + drawData->DisplaySize.x;
        const float t = drawData->DisplayPos.y, b = t + drawData->DisplaySize.y;
        float4x4 projection;
        projection.m[0][0] = 2.0f / (r - l); projection.m[0][3] = (r + l) / (l - r);
        projection.m[1][1] = 2.0f / (t - b); projection.m[1][3] = (t + b) / (b - t);
        projection.m[2][2] = 0.5f;           projection.m[2][3] = 0.5f;
        context.SetPushConstants(projection);

        const float width = float(context.GetTargetWidth());
        const float height = float(context.GetTargetHeight());

        uint32_t vertexOffset = 0, indexOffset = 0;
        for (const ImDrawList* list : drawData->CmdLists)
        {
            for (const ImDrawCmd& command : list->CmdBuffer)
            {
                if (command.UserCallback)
                    continue;

                const float minX = std::max(command.ClipRect.x - l, 0.0f);
                const float minY = std::max(command.ClipRect.y - t, 0.0f);
                const float maxX = std::min(command.ClipRect.z - l, width);
                const float maxY = std::min(command.ClipRect.w - t, height);
                if (maxX <= minX || maxY <= minY)
                    continue;

                context.SetTexture(TextureId(command.GetTexID()));
                context.SetScissor({ int32_t(minX), int32_t(minY), int32_t(maxX), int32_t(maxY) });
                context.DrawIndexed({
                    .IndexCount = command.ElemCount,
                    .FirstIndex = command.IdxOffset + indexOffset,
                    .BaseVertex = int32_t(command.VtxOffset + vertexOffset) });
            }
            vertexOffset += uint32_t(list->VtxBuffer.Size);
            indexOffset += uint32_t(list->IdxBuffer.Size);
        }
    }
}
