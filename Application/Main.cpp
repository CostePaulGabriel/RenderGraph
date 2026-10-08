// Sample application: builds the demo RenderGraph, compiles it, and lets the Renderer execute
// it through NVRHI on D3D12. ImGui is drawn by the graph's OverlayPass.
//
// Command line:
//   --debug        D3D12 debug layer + NVRHI validation (default in Debug builds)
//   --no-debug     disable the above
//   --frames N     exit after N frames (smoke test)
//   --vulkan       request the Vulkan backend (placeholder, not implemented yet)

#include <windows.h>

#include <imgui.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "Application/ImGuiDraw.h"
#include "Application/Win32Application.h"
#include "Passes/DemoGraph.h"
#include "Passes/ForwardPass.h"
#include "Passes/LightUpdatePass.h"
#include "Passes/LightingPass.h"
#include "Passes/PostProcessPass.h"
#include "RenderGraph/RenderGraph.h"
#include "Renderer/Renderer.h"
#include "Scene/MaterialXImporter.h"
#include "Scene/Scene.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace LynxEngine;

namespace
{
    struct UiState
    {
        bool DebugNormals = false;
        bool Animate = true;
        int LightCount = 16;
        bool GraphDirty = true;
        std::string Error;
    };

    const char* ToString(PassType type)
    {
        switch (type)
        {
        case PassType::Graphics: return "Graphics";
        case PassType::Compute:  return "Compute";
        case PassType::Copy:     return "Copy";
        }
        return "?";
    }

    std::string UsageList(const CompiledResource& resource)
    {
        std::string text;
        for (uint32_t usage = 0; usage <= uint32_t(ResourceUsage::CopyDest); ++usage)
        {
            if (resource.HasUsage(ResourceUsage(usage)))
                text += (text.empty() ? "" : ", ") + std::string(LynxEngine::ToString(ResourceUsage(usage)));
        }
        return text;
    }

    void DrawGraphWindow(RenderGraph& graph, const CompiledGraph& compiled, UiState& ui, const Renderer& renderer, float fps)
    {
        ImGui::SetNextWindowPos({ 12, 12 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ 520, 640 }, ImGuiCond_FirstUseEver);
        ImGui::Begin("Render Graph");

        ImGui::Text("%s  |  %ux%u  |  %.1f FPS", renderer.GetBackendName(), renderer.GetWidth(), renderer.GetHeight(), fps);
        ImGui::Separator();

        // Per-frame pass parameters: written into the pass constants, no recompile.
        ImGui::TextDisabled("Pass parameters (no recompile)");
        if (auto* post = graph.GetPass<PostProcessPass>("PostProcess"))
        {
            PostProcessPass::Params params = post->GetParams();
            bool changed = ImGui::SliderFloat("Exposure", &params.Exposure, 0.1f, 4.0f);
            changed |= ImGui::SliderFloat("Vignette", &params.Vignette, 0.0f, 1.5f);
            if (changed)
                post->SetParams(params);
        }
        if (auto* lighting = graph.GetPass<LightingPass>("Lighting"))
        {
            LightingPass::Params params = lighting->GetParams();
            bool changed = ImGui::SliderFloat("Sun intensity", &params.SunIntensity, 0.0f, 4.0f);
            changed |= ImGui::SliderFloat("Point lights", &params.PointLightScale, 0.0f, 4.0f);
            changed |= ImGui::ColorEdit3("Sky (ambient)", params.Ambient, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
            if (changed)
            {
                lighting->SetParams(params);
                if (auto* forward = graph.GetPass<ForwardPass>("Forward"))
                    forward->SetParams(params); // the glass is lit like the rest of the scene
            }
        }
        ImGui::Checkbox("Animate scene", &ui.Animate);

        // Graph changes: edges or resource descriptions change, the graph is compiled again.
        ImGui::Spacing();
        ImGui::TextDisabled("Graph changes (recompile)");
        if (ImGui::Checkbox("Debug view: G-buffer normals", &ui.DebugNormals))
        {
            SetDebugNormalsView(graph, ui.DebugNormals);
            ui.GraphDirty = true;
        }
        if (ImGui::SliderInt("Light count", &ui.LightCount, 1, 256))
        {
            if (auto* lights = graph.GetPass<LightUpdatePass>("LightUpdate"))
                lights->SetLightCount(uint32_t(ui.LightCount));
            ui.GraphDirty = true;
        }

        if (!ui.Error.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::TextWrapped("%s", ui.Error.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Execution order", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (size_t i = 0; i < compiled.Passes.size(); ++i)
            {
                const CompiledPass& pass = compiled.Passes[i];
                std::string deps;
                for (const uint32_t dep : pass.DependsOn)
                    deps += (deps.empty() ? "" : ", ") + compiled.Passes[dep].Name;

                const std::string label = std::to_string(i) + ". " + pass.Name + "  [" + ToString(pass.Pass->GetType()) + "]";
                if (ImGui::TreeNode(label.c_str()))
                {
                    ImGui::TextDisabled("depends on: %s", deps.empty() ? "-" : deps.c_str());
                    for (const CompiledBinding& binding : pass.Bindings)
                    {
                        static constexpr const char* kAccess[] = { "read", "write", "read-write" };
                        ImGui::BulletText("%-7s %-10s %-15s #%u %s", binding.Field.c_str(), kAccess[size_t(binding.Access)],
                            std::string(LynxEngine::ToString(binding.Usage)).c_str(), binding.Resource,
                            compiled.Resources[binding.Resource].Name.c_str());
                    }
                    ImGui::TreePop();
                }
                else
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("<- %s", deps.empty() ? "-" : deps.c_str());
                }
            }
        }

        if (ImGui::CollapsingHeader("Resources", ImGuiTreeNodeFlags_DefaultOpen) &&
            ImGui::BeginTable("resources", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("#");
            ImGui::TableSetupColumn("Resource");
            ImGui::TableSetupColumn("Description");
            ImGui::TableSetupColumn("Lifetime");
            ImGui::TableHeadersRow();
            for (const CompiledResource& r : compiled.Resources)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%u", r.Handle);
                ImGui::TableNextColumn(); ImGui::Text("%s%s", r.Name.c_str(), r.IsGraphOutput ? " (back buffer)" : "");
                ImGui::TableNextColumn();
                if (r.Kind == ResourceKind::Texture)
                    ImGui::Text("%ux%u %s", r.Texture.Width, r.Texture.Height, std::string(LynxEngine::ToString(r.Texture.Format)).c_str());
                else
                    ImGui::Text("%u x %u B", r.Buffer.ElementCount, r.Buffer.ElementSize);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", UsageList(r).c_str());
                ImGui::TableNextColumn(); ImGui::Text("pass %u..%u", r.FirstPass, r.LastPass);
            }
            ImGui::EndTable();
        }

        if (ImGui::CollapsingHeader("Culled passes / warnings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (compiled.Warnings.empty())
                ImGui::TextDisabled("none");
            for (const std::string& warning : compiled.Warnings)
                ImGui::BulletText("%s", warning.c_str());
            ImGui::TextDisabled("%zu shaders, %zu samplers", compiled.Shaders.size(), compiled.Samplers.size());
        }

        ImGui::End();
    }
}

int main(int argc, char** argv)
{
#ifdef _DEBUG
    bool debug = true;
#else
    bool debug = false;
#endif
    int maxFrames = 0;
    GraphicsAPI api = GraphicsAPI::D3D12;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--debug")) debug = true;
        else if (!std::strcmp(argv[i], "--no-debug")) debug = false;
        else if (!std::strcmp(argv[i], "--vulkan")) api = GraphicsAPI::Vulkan;
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::atoi(argv[++i]);
    }

    Win32Application window;
    if (!window.Create(L"RenderGraph Prototype - NVRHI / D3D12", 1280, 720))
    {
        std::fprintf(stderr, "Could not create the window\n");
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(window.GetNativeHandle());
    window.SetMessageHook([](void* hwnd, uint32_t message, uint64_t wParam, int64_t lParam)
    {
        return ImGui_ImplWin32_WndProcHandler(static_cast<HWND>(hwnd), message, WPARAM(wParam), LPARAM(lParam)) != 0;
    });

    std::filesystem::path shaderDirectory = std::filesystem::path(Win32Application::GetExecutableDirectory()) / "Shaders";
#ifdef RG_SOURCE_SHADER_DIR
    if (!std::filesystem::exists(shaderDirectory))
        shaderDirectory = RG_SOURCE_SHADER_DIR;
#endif

    Scene scene = CreateDemoScene();

    // MaterialX material (Dark_Glass_2k_8b_oCH0KDS) on the center cube, instance 1 of CreateDemoScene.
    if (std::optional<MaterialXMaterial> material = ImportMaterialX(RG_MATERIALX_FILE))
        scene.Instances[1].Material = AddMaterialX(scene, *material);
    else
        std::fprintf(stderr, "MaterialX material '%s' not loaded, the center cube keeps its demo material\n", RG_MATERIALX_FILE);

    scene.MainCamera.SetAspectRatio(window.GetWidth(), window.GetHeight());

    int exitCode = 0;
    {
        Renderer renderer;
        RendererDesc desc;
        desc.WindowHandle = window.GetNativeHandle();
        desc.Width = window.GetWidth();
        desc.Height = window.GetHeight();
        desc.Api = api;
        desc.Debug = debug;
        desc.ShaderDirectory = shaderDirectory;

        if (!renderer.Init(desc) || !renderer.SetScene(scene))
        {
            std::fprintf(stderr, "Renderer initialization failed: %s\n", renderer.GetLastError().c_str());
            exitCode = 1;
        }
        else
        {
            RenderGraph graph;
            BuildDemoGraph(graph, renderer.GetBackBufferFormat());
            graph.GetPass("Overlay")->SetDraw(DrawImGui);

            CompiledGraph compiled;
            UiState ui;

            const auto rebuildGraph = [&]
            {
                auto result = graph.Compile({ renderer.GetWidth(), renderer.GetHeight(), renderer.GetBackBufferFormat() });
                if (!result)
                {
                    ui.Error = result.error();
                    std::fprintf(stderr, "%s\n", ui.Error.c_str());
                    return;
                }
                compiled = std::move(*result);
                std::printf("%s\n", DescribeGraph(compiled).c_str());
                ui.Error = renderer.SetGraph(compiled) ? std::string() : renderer.GetLastError();
            };

            using Clock = std::chrono::steady_clock;
            auto lastTime = Clock::now();
            float sceneTime = 0.0f;
            float fps = 0.0f;
            uint32_t frameIndex = 0;

            while (window.PumpMessages())
            {
                if (window.ConsumeResize() && !window.IsMinimized())
                {
                    renderer.Resize(window.GetWidth(), window.GetHeight());
                    scene.MainCamera.SetAspectRatio(window.GetWidth(), window.GetHeight());
                    ui.GraphDirty = true;
                }
                if (window.IsMinimized())
                {
                    Sleep(16);
                    continue;
                }
                if (ui.GraphDirty)
                {
                    ui.GraphDirty = false;
                    rebuildGraph();
                }

                const auto now = Clock::now();
                const float dt = std::chrono::duration<float>(now - lastTime).count();
                lastTime = now;
                fps = fps == 0.0f ? 1.0f / std::max(dt, 1e-4f) : fps * 0.95f + 0.05f / std::max(dt, 1e-4f);
                if (ui.Animate)
                    sceneTime += dt;
                AnimateDemoScene(scene, sceneTime);

                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
                DrawGraphWindow(graph, compiled, ui, renderer, fps);
                ImGui::Render();

                // Executes the compiled graph; the OverlayPass draws ImGui::GetDrawData().
                renderer.Render(scene, { sceneTime, dt, frameIndex });

                ++frameIndex;
                if (maxFrames > 0 && frameIndex >= uint32_t(maxFrames))
                {
                    std::printf("Rendered %u frames, exiting.\n", frameIndex);
                    break;
                }
            }
        }
        renderer.Shutdown();
    }

    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    return exitCode;
}
