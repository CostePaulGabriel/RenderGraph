#pragma once

#include "RenderGraph/RenderPass.h"

namespace LynxEngine
{
    // Compute pass that animates a set of point lights and writes them into a structured buffer.
    //   out: lights (StructuredBuffer<Light>, UAV)
    class LightUpdatePass : public RenderPass
    {
    public:
        struct GpuLight // must match Lights.slang
        {
            float Position[3];
            float Radius;
            float Color[3];
            float Intensity;
        };

        static constexpr uint32_t kThreadGroupSize = 64;

        explicit LightUpdatePass(uint32_t lightCount = 16)
            : RenderPass(PassType::Compute)
        {
            OutputBuffer("lights", { .ElementCount = lightCount, .ElementSize = sizeof(GpuLight) });
            AddShader(ShaderType::Compute, "Lights.slang", "CSMain");
            SetLightCount(lightCount);
        }

        // Changes a resource description: the graph must be compiled again.
        void SetLightCount(uint32_t count)
        {
            FindField("lights")->Buffer.ElementCount = count;
            SetDispatchSize((count + kThreadGroupSize - 1) / kThreadGroupSize);
        }

        uint32_t GetLightCount() const { return FindField("lights")->Buffer.ElementCount; }
    };
}
