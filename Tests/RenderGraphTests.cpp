// Headless tests for the RenderGraph: no window, no GPU, no NVRHI.
// They build the demo graph and a few broken graphs and check what the compiler reports.

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "Passes/DebugNormalsPass.h"
#include "Passes/DemoGraph.h"
#include "Passes/LightUpdatePass.h"
#include "Passes/OverlayPass.h"
#include "Passes/PostProcessPass.h"
#include "RenderGraph/RenderGraph.h"

using namespace LynxEngine;

namespace
{
    int g_Failures = 0;

#define CHECK(cond)                                                              \
    do                                                                           \
    {                                                                            \
        if (!(cond))                                                             \
        {                                                                        \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_Failures;                                                        \
        }                                                                        \
    } while (false)

    const GraphCompileDesc kDesc{ .BackBufferWidth = 1280, .BackBufferHeight = 720, .BackBufferFormat = TextureFormat::RGBA8_UNORM };

    int IndexOf(const CompiledGraph& graph, std::string_view name)
    {
        for (size_t i = 0; i < graph.Passes.size(); ++i)
        {
            if (graph.Passes[i].Name == name)
                return int(i);
        }
        return -1;
    }

    bool Contains(const std::string& text, std::string_view what)
    {
        return text.find(what) != std::string::npos;
    }

    // A plain RenderPass declared from the outside, no subclass needed.
    std::unique_ptr<RenderPass> MakeFullscreenPass(std::vector<std::string> inputs, std::vector<std::string> outputs)
    {
        auto pass = std::make_unique<RenderPass>(PassType::Graphics);
        for (std::string& input : inputs)
            pass->Input(std::move(input));
        for (std::string& output : outputs)
            pass->Output(std::move(output), { .Format = TextureFormat::RGBA8_UNORM });
        pass->AddShader(ShaderType::Vertex, "Fullscreen.slang", "VSMain");
        pass->AddShader(ShaderType::Pixel, "Test.slang", "PSMain");
        pass->SetDraw([](DrawContext& context) { context.DrawFullscreenTriangle(); });
        return pass;
    }

    void TestDemoGraph()
    {
        std::printf("[demo graph]\n");
        RenderGraph graph;
        BuildDemoGraph(graph, kDesc.BackBufferFormat);

        auto result = graph.Compile(kDesc);
        CHECK(result.has_value());
        if (!result)
        {
            std::printf("%s\n", result.error().c_str());
            return;
        }

        const CompiledGraph& g = *result;
        std::printf("%s", DescribeGraph(g).c_str());

        CHECK(g.Passes.size() == 9);
        CHECK(IndexOf(g, "DebugNormals") == -1);
        CHECK(g.CulledPasses.size() == 1 && g.CulledPasses[0] == "DebugNormals");
        CHECK(IndexOf(g, "Depth") < IndexOf(g, "GBuffer"));
        CHECK(IndexOf(g, "GBuffer") < IndexOf(g, "Lighting"));
        CHECK(IndexOf(g, "LightUpdate") < IndexOf(g, "Lighting"));
        CHECK(IndexOf(g, "Shadow") < IndexOf(g, "Lighting"));
        CHECK(IndexOf(g, "Lighting") < IndexOf(g, "SceneColorCopy"));
        CHECK(IndexOf(g, "SceneColorCopy") < IndexOf(g, "Forward"));
        CHECK(IndexOf(g, "Forward") < IndexOf(g, "PostProcess"));
        CHECK(IndexOf(g, "PostProcess") < IndexOf(g, "Overlay"));
        CHECK(IndexOf(g, "Overlay") == int(g.Passes.size()) - 1);

        // Depth, shadow, albedo, normal, material, emissive, lights, HDR color, its copy for the glass
        // (Forward works in place), LDR output (Overlay works in place).
        CHECK(g.Resources.size() == 10);
        CHECK(g.Output != kInvalidHandle);
        CHECK(g.Resources[g.Output].Name == "PostProcess.output");
        CHECK(g.Resources[g.Output].Texture.Width == 1280);

        // Shaders are deduplicated: Scene.slang:VSMain and Fullscreen.slang:VSMain are shared.
        CHECK(g.Shaders.size() == 10);
        CHECK(g.Samplers.size() == 2); // wrap anisotropic and linear clamp, shared by GBuffer / Forward / PostProcess

        // Switching to the debug view rewires one edge: Lighting and LightUpdate get culled.
        SetDebugNormalsView(graph, true);
        auto debug = graph.Compile(kDesc);
        CHECK(debug.has_value());
        if (debug)
        {
            CHECK(IndexOf(*debug, "DebugNormals") >= 0);
            CHECK(IndexOf(*debug, "Lighting") == -1);
            CHECK(IndexOf(*debug, "LightUpdate") == -1);
            std::printf("debug view order:");
            for (const CompiledPass& pass : debug->Passes)
                std::printf(" %s", pass.Name.c_str());
            std::printf("\n");
        }

        // Changing a declaration (light count) changes the compiled resource.
        SetDebugNormalsView(graph, false);
        graph.GetPass<LightUpdatePass>("LightUpdate")->SetLightCount(100);
        auto more = graph.Compile(kDesc);
        CHECK(more.has_value());
        if (more)
        {
            const CompiledPass& pass = more->Passes[size_t(IndexOf(*more, "LightUpdate"))];
            CHECK(more->Resources[pass.Bindings[0].Resource].Buffer.ElementCount == 100);
            CHECK(pass.Pass->GetDispatchSize()[0] == 2);
        }

        // Resize: back buffer relative textures follow the new size.
        auto small = graph.Compile({ .BackBufferWidth = 640, .BackBufferHeight = 360 });
        CHECK(small.has_value() && small->Resources[small->Output].Texture.Height == 360);
    }

    void TestErrors()
    {
        std::printf("\n[negative tests] The graphs below are broken on purpose; each error is EXPECTED:\n");

        {   // Missing input.
            RenderGraph graph;
            BuildDemoGraph(graph, kDesc.BackBufferFormat);
            graph.RemoveEdge("Lighting.lights");
            auto result = graph.Compile(kDesc);
            CHECK(!result && Contains(result.error(), "Lighting.lights' is not connected"));
            if (!result) std::printf("%s\n", result.error().c_str());
        }
        {   // Cycle.
            RenderGraph graph;
            RenderPass& a = graph.AddPass("A", std::make_unique<DebugNormalsPass>());
            graph.AddPass("B", std::make_unique<DebugNormalsPass>());
            graph.AddPass<OverlayPass>("Overlay");
            (void)a;
            graph.AddEdge("A.color", "B.normal");
            graph.AddEdge("B.color", "A.normal");
            graph.AddEdge("A.color", "Overlay.target");
            graph.MarkOutput("Overlay.target");
            auto result = graph.Compile(kDesc);
            CHECK(!result && Contains(result.error(), "Cycle"));
            if (!result) std::printf("%s\n", result.error().c_str());
        }
        {   // In-place modification while somebody else reads the same resource.
            RenderGraph graph;
            graph.AddPass("Producer", MakeFullscreenPass({}, { "color" }));
            graph.AddPass<OverlayPass>("Modifier");
            graph.AddPass("Reader", MakeFullscreenPass({ "a", "b" }, { "out" }));
            graph.AddEdge("Producer.color", "Modifier.target");
            graph.AddEdge("Producer.color", "Reader.a"); // sees the image before or after Modifier?
            graph.AddEdge("Modifier.target", "Reader.b");
            graph.MarkOutput("Reader.out");
            auto result = graph.Compile(kDesc);
            CHECK(!result && Contains(result.error(), "ambiguous"));
            if (!result) std::printf("%s\n", result.error().c_str());
        }
        {   // Output does not match the back buffer, and an unknown field.
            RenderGraph graph;
            BuildDemoGraph(graph, TextureFormat::RGBA16_FLOAT);
            graph.AddEdge("GBuffer.typo", "Lighting.albedo");
            auto result = graph.Compile(kDesc);
            CHECK(!result && Contains(result.error(), "unknown field 'typo'"));
            if (!result) std::printf("%s\n", result.error().c_str());

            graph.RemoveEdge("Lighting.albedo");
            graph.AddEdge("GBuffer.albedo", "Lighting.albedo");
            result = graph.Compile(kDesc);
            CHECK(!result && Contains(result.error(), "must match the back buffer"));
            if (!result) std::printf("%s\n", result.error().c_str());
        }
        {   // Wrong usage: a color texture used as depth attachment.
            RenderGraph graph;
            BuildDemoGraph(graph, kDesc.BackBufferFormat);
            graph.AddPass("ColorSource", MakeFullscreenPass({}, { "color" }));
            graph.RemoveEdge("GBuffer.depth");
            graph.AddEdge("ColorSource.color", "GBuffer.depth");
            auto result = graph.Compile(kDesc);
            CHECK(!result && Contains(result.error(), "has no depth format"));
            if (!result) std::printf("%s\n", result.error().c_str());
        }
    }
}

int main()
{
    TestDemoGraph();
    TestErrors();

    if (g_Failures == 0)
        std::printf("\nAll RenderGraph tests passed.\n");
    else
        std::printf("\n%d check(s) failed.\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
