#include "Passes/DemoGraph.h"

#include "Passes/DebugNormalsPass.h"
#include "Passes/DepthPass.h"
#include "Passes/GBufferPass.h"
#include "Passes/LightUpdatePass.h"
#include "Passes/LightingPass.h"
#include "Passes/OverlayPass.h"
#include "Passes/PostProcessPass.h"

namespace LynxEngine
{
    void BuildDemoGraph(RenderGraph& graph, TextureFormat backBufferFormat)
    {
        graph.AddPass<OverlayPass>("Overlay");
        graph.AddPass<PostProcessPass>("PostProcess", backBufferFormat);
        graph.AddPass<LightingPass>("Lighting");
        graph.AddPass<DebugNormalsPass>("DebugNormals");
        graph.AddPass<GBufferPass>("GBuffer");
        graph.AddPass<DepthPass>("Depth");
        graph.AddPass<LightUpdatePass>("LightUpdate", 16u);

        graph.AddEdge("Depth.depth", "GBuffer.depth");
        graph.AddEdge("Depth.depth", "Lighting.depth");
        graph.AddEdge("GBuffer.albedo", "Lighting.albedo");
        graph.AddEdge("GBuffer.normal", "Lighting.normal");
        graph.AddEdge("GBuffer.material", "Lighting.material");
        graph.AddEdge("GBuffer.emissive", "Lighting.emissive");
        graph.AddEdge("GBuffer.normal", "DebugNormals.normal");
        graph.AddEdge("LightUpdate.lights", "Lighting.lights");
        graph.AddEdge("Lighting.color", "PostProcess.color");
        graph.AddEdge("PostProcess.output", "Overlay.target");

        graph.MarkOutput("Overlay.target");
    }

    void SetDebugNormalsView(RenderGraph& graph, bool enabled)
    {
        graph.RemoveEdge("PostProcess.color");
        graph.AddEdge(enabled ? "DebugNormals.color" : "Lighting.color", "PostProcess.color");
    }
}
