#pragma once

#include "RenderGraph/RenderGraph.h"

namespace LynxEngine
{
    // Builds the sample frame:
    //
    //   LightUpdate (compute) ──lights──────────────┐
    //   Depth ──depth──► GBuffer ──albedo,normal,material,emissive──► Lighting ──color──► PostProcess ──output──► Overlay ──► back buffer
    //         └──depth────────────────────────────►┘
    //   Shadow ──shadow (sun shadow map)───────────► Lighting
    //   DebugNormals (only used when the debug view is on, culled otherwise)
    //
    // Passes are added deliberately out of order: the execution order comes from the edges.
    void BuildDemoGraph(RenderGraph& graph, TextureFormat backBufferFormat);

    // Rewires PostProcess.color to DebugNormals.color (or back to Lighting.color).
    void SetDebugNormalsView(RenderGraph& graph, bool enabled);
}
