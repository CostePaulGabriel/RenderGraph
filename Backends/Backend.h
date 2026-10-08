#pragma once

// The boundary between the Renderer and a graphics API.
//
// A backend creates the native device / queue / swap chain for one API, wraps them in an
// NVRHI device and exposes the swap chain images as NVRHI textures. Everything above this
// header (Renderer) talks to NVRHI only; everything below it (Backends/D3D12, Backends/Vulkan)
// is API specific. The RenderGraph never sees this header.

#include <cstdint>
#include <memory>

#include <nvrhi/nvrhi.h>

namespace LynxEngine
{
    struct BackendDesc
    {
        void* WindowHandle = nullptr; // HWND on Windows
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t BackBufferCount = 2;
        bool Debug = false;   // API debug layer + NVRHI validation
        bool VSync = true;
        nvrhi::IMessageCallback* MessageCallback = nullptr;
    };

    class DeviceBackend
    {
    public:
        virtual ~DeviceBackend() = default;

        virtual const char* GetName() const = 0;
        virtual nvrhi::IDevice* GetDevice() const = 0;
        virtual nvrhi::Format GetBackBufferFormat() const = 0;
        virtual uint32_t GetBackBufferCount() const = 0;
        virtual nvrhi::ITexture* GetBackBuffer(uint32_t index) const = 0;
        virtual uint32_t GetCurrentBackBufferIndex() const = 0;

        // BeginFrame waits until the current swap chain image may be reused.
        virtual void BeginFrame() = 0;
        virtual void Present() = 0;

        // The caller must release every reference to the back buffers before resizing.
        virtual void Resize(uint32_t width, uint32_t height) = 0;
    };

    // Return nullptr on failure (the reason is reported through the message callback / stderr).
    std::unique_ptr<DeviceBackend> CreateD3D12Backend(const BackendDesc& desc);
    std::unique_ptr<DeviceBackend> CreateVulkanBackend(const BackendDesc& desc);
}
