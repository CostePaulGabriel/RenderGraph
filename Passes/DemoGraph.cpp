#include "Passes/DemoGraph.h"

#include "Passes/DebugNormalsPass.h"
#include "Passes/DepthPass.h"
#include "Passes/ForwardPass.h"
#include "Passes/GBufferPass.h"
#include "Passes/LightUpdatePass.h"
#include "Passes/LightingPass.h"
#include "Passes/OverlayPass.h"
#include "Passes/PostProcessPass.h"
#include "Passes/ShadowPass.h"

namespace LynxEngine
{
    void BuildDemoGraph(RenderGraph& graph, TextureFormat backBufferFormat)
    {
        graph.AddPass<OverlayPass>("Overlay");
        graph.AddPass<PostProcessPass>("PostProcess", backBufferFormat);
        auto& forward = graph.AddPass<ForwardPass>("Forward");
        graph.AddPass<SceneColorCopyPass>("SceneColorCopy");
        auto& lighting = graph.AddPass<LightingPass>("Lighting");
        graph.AddPass<DebugNormalsPass>("DebugNormals");
        graph.AddPass<GBufferPass>("GBuffer");
        graph.AddPass<DepthPass>("Depth");
        auto& shadow = graph.AddPass<ShadowPass>("Shadow");

        // Shadow and Lighting must use the same sun matrix.
        LightingPass::Params params = lighting.GetParams();
        params.LightViewProjection = ShadowPass::ComputeLightViewProjection({ params.SunDirection[0], params.SunDirection[1], params.SunDirection[2] });
        lighting.SetParams(params);
        shadow.SetLightViewProjection(params.LightViewProjection);
        forward.SetParams(params);
        graph.AddPass<LightUpdatePass>("LightUpdate", 16u);

        graph.AddEdge("Depth.depth", "GBuffer.depth");
        graph.AddEdge("Depth.depth", "Lighting.depth");
        graph.AddEdge("GBuffer.albedo", "Lighting.albedo");
        graph.AddEdge("GBuffer.normal", "Lighting.normal");
        graph.AddEdge("GBuffer.material", "Lighting.material");
        graph.AddEdge("GBuffer.emissive", "Lighting.emissive");
        graph.AddEdge("GBuffer.normal", "DebugNormals.normal");
        graph.AddEdge("LightUpdate.lights", "Lighting.lights");
        graph.AddEdge("Shadow.shadow", "Lighting.shadow");
        // Glass after Lighting: drawn over a copy of the lit image, sampling the original.
        graph.AddEdge("Lighting.color", "SceneColorCopy.source");
        graph.AddEdge("Lighting.color", "Forward.scene");
        graph.AddEdge("SceneColorCopy.color", "Forward.color");
        graph.AddEdge("LightUpdate.lights", "Forward.lights");
        graph.AddEdge("Depth.depth", "Forward.depth");
        graph.AddEdge("Forward.color", "PostProcess.color");
        graph.AddEdge("PostProcess.output", "Overlay.target");

        graph.MarkOutput("Overlay.target");
    }

    void SetDebugNormalsView(RenderGraph& graph, bool enabled)
    {
        graph.RemoveEdge("PostProcess.color");
        graph.AddEdge(enabled ? "DebugNormals.color" : "Forward.color", "PostProcess.color");
    }
}
