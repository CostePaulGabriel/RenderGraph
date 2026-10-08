// Placeholder for the Vulkan backend.
//
// The architecture is ready for it: the Renderer only uses DeviceBackend + NVRHI, and the
// shader compiler (Slang) can emit SPIR-V. What is missing is the API-specific part that
// mirrors D3D12Backend.cpp:
//   - VkInstance / VkPhysicalDevice / VkDevice + queues, VkSurfaceKHR from the HWND
//   - VkSwapchainKHR, acquire / present semaphores
//   - nvrhi::vulkan::createDevice(...) and createHandleForNativeTexture for swap chain images
// Enable RG_BACKEND_VULKAN in CMake (builds nvrhi_vk) when implementing it.

#include "Backends/Backend.h"

#include <cstdio>

namespace LynxEngine
{
    std::unique_ptr<DeviceBackend> CreateVulkanBackend(const BackendDesc&)
    {
        std::fprintf(stderr, "[Vulkan] The Vulkan backend is not implemented yet in this prototype.\n");
        return nullptr;
    }
}
