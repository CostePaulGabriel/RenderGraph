#pragma once

#include "RenderGraph/DrawContext.h"

namespace LynxEngine
{
    // Draws ImGui::GetDrawData() of the current frame. Meant for OverlayPass::SetDraw; the pass
    // must declare the ImDrawVert layout, a draw texture and a float4x4 as push constants.
    void DrawImGui(DrawContext& context);
}
