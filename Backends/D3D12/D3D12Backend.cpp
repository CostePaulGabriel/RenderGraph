// D3D12 backend: native device, queue and DXGI swap chain wrapped in an NVRHI device.
// This is the only place in the project that includes D3D12 / DXGI headers.

#include "Backends/Backend.h"

#include <nvrhi/d3d12.h>
#include <nvrhi/validation.h>

#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace LynxEngine
{
    namespace
    {
        constexpr DXGI_FORMAT kSwapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

        bool Check(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return true;
            std::fprintf(stderr, "[D3D12] %s failed (HRESULT 0x%08lX)\n", what, static_cast<unsigned long>(hr));
            return false;
        }

        std::string Narrow(const wchar_t* text)
        {
            std::string result;
            for (; *text; ++text)
                result.push_back(*text < 128 ? char(*text) : '?');
            return result;
        }

        class D3D12Backend final : public DeviceBackend
        {
        public:
            ~D3D12Backend() override
            {
                if (m_NvrhiDevice)
                    m_NvrhiDevice->waitForIdle();
                ReleaseBackBuffers();
                m_NvrhiDevice = nullptr;
                for (HANDLE event : m_FrameEvents)
                    CloseHandle(event);
            }

            bool Init(const BackendDesc& desc)
            {
                m_Desc = desc;
                const HWND hwnd = static_cast<HWND>(desc.WindowHandle);

                if (desc.Debug)
                {
                    ComPtr<ID3D12Debug> debug;
                    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
                        debug->EnableDebugLayer();
                    else
                        std::fprintf(stderr, "[D3D12] Debug layer not available (install 'Graphics Tools').\n");
                }

                // The debug factory needs the Graphics Tools; fall back to a normal one.
                if (!desc.Debug || FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&m_Factory))))
                {
                    if (!Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_Factory)), "CreateDXGIFactory2"))
                        return false;
                }

                if (!CreateDevice())
                    return false;

                D3D12_COMMAND_QUEUE_DESC queueDesc{};
                queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                if (!Check(m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_Queue)), "CreateCommandQueue"))
                    return false;
                m_Queue->SetName(L"Graphics Queue");

                DXGI_SWAP_CHAIN_DESC1 swapDesc{};
                swapDesc.Width = desc.Width;
                swapDesc.Height = desc.Height;
                swapDesc.Format = kSwapChainFormat;
                swapDesc.SampleDesc.Count = 1;
                swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                swapDesc.BufferCount = desc.BackBufferCount;
                swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

                ComPtr<IDXGISwapChain1> swapChain;
                if (!Check(m_Factory->CreateSwapChainForHwnd(m_Queue.Get(), hwnd, &swapDesc, nullptr, nullptr, &swapChain), "CreateSwapChainForHwnd"))
                    return false;
                if (!Check(swapChain.As(&m_SwapChain), "IDXGISwapChain3"))
                    return false;
                m_Factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

                nvrhi::d3d12::DeviceDesc deviceDesc;
                deviceDesc.errorCB = desc.MessageCallback;
                deviceDesc.pDevice = m_Device.Get();
                deviceDesc.pGraphicsCommandQueue = m_Queue.Get();
                m_NvrhiDevice = nvrhi::d3d12::createDevice(deviceDesc);
                if (!m_NvrhiDevice)
                {
                    std::fprintf(stderr, "[D3D12] nvrhi::d3d12::createDevice failed\n");
                    return false;
                }
                if (desc.Debug)
                    m_NvrhiDevice = nvrhi::validation::createValidationLayer(m_NvrhiDevice);

                if (!Check(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_FrameFence)), "CreateFence"))
                    return false;
                for (uint32_t i = 0; i < desc.BackBufferCount; ++i)
                    m_FrameEvents.push_back(CreateEvent(nullptr, FALSE, TRUE, nullptr));

                return CreateBackBuffers();
            }

            const char* GetName() const override { return "D3D12"; }
            nvrhi::IDevice* GetDevice() const override { return m_NvrhiDevice; }
            nvrhi::Format GetBackBufferFormat() const override { return nvrhi::Format::RGBA8_UNORM; }
            uint32_t GetBackBufferCount() const override { return uint32_t(m_BackBuffers.size()); }
            nvrhi::ITexture* GetBackBuffer(uint32_t index) const override { return m_BackBuffers[index]; }
            uint32_t GetCurrentBackBufferIndex() const override { return m_SwapChain->GetCurrentBackBufferIndex(); }

            void BeginFrame() override
            {
                WaitForSingleObject(m_FrameEvents[GetCurrentBackBufferIndex()], INFINITE);
            }

            void Present() override
            {
                const uint32_t index = GetCurrentBackBufferIndex();
                const HRESULT hr = m_SwapChain->Present(m_Desc.VSync ? 1 : 0, 0);
                if (FAILED(hr))
                    Check(hr, "Present");

                ++m_FrameCount;
                m_FrameFence->SetEventOnCompletion(m_FrameCount, m_FrameEvents[index]);
                m_Queue->Signal(m_FrameFence.Get(), m_FrameCount);
            }

            void Resize(uint32_t width, uint32_t height) override
            {
                m_NvrhiDevice->waitForIdle();
                ReleaseBackBuffers();

                if (!Check(m_SwapChain->ResizeBuffers(m_Desc.BackBufferCount, width, height, kSwapChainFormat, 0), "ResizeBuffers"))
                {
                    CreateBackBuffers(); // keep the old buffers usable rather than leaving none
                    return;
                }

                m_Desc.Width = width;
                m_Desc.Height = height;
                for (HANDLE event : m_FrameEvents)
                    SetEvent(event); // the GPU is idle, every image is free
                CreateBackBuffers();
            }

        private:
            bool CreateDevice()
            {
                ComPtr<IDXGIFactory6> factory6;
                m_Factory.As(&factory6);

                for (UINT i = 0;; ++i)
                {
                    ComPtr<IDXGIAdapter1> adapter;
                    const HRESULT hr = factory6
                        ? factory6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter))
                        : m_Factory->EnumAdapters1(i, &adapter);
                    if (hr == DXGI_ERROR_NOT_FOUND)
                        break;
                    if (FAILED(hr))
                        continue;

                    DXGI_ADAPTER_DESC1 adapterDesc{};
                    adapter->GetDesc1(&adapterDesc);
                    if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                        continue;

                    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_Device))))
                    {
                        std::printf("[D3D12] Adapter: %s\n", Narrow(adapterDesc.Description).c_str());
                        break;
                    }
                }

                if (!m_Device)
                {
                    std::fprintf(stderr, "[D3D12] No adapter supports feature level 12_0\n");
                    return false;
                }

                if (m_Desc.Debug)
                {
                    ComPtr<ID3D12InfoQueue> infoQueue;
                    if (SUCCEEDED(m_Device.As(&infoQueue)))
                    {
                        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
                        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
                    }
                }
                return true;
            }

            bool CreateBackBuffers()
            {
                for (uint32_t i = 0; i < m_Desc.BackBufferCount; ++i)
                {
                    ComPtr<ID3D12Resource> buffer;
                    if (!Check(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&buffer)), "GetBuffer"))
                        return false;

                    nvrhi::TextureDesc textureDesc;
                    textureDesc.width = m_Desc.Width;
                    textureDesc.height = m_Desc.Height;
                    textureDesc.format = GetBackBufferFormat();
                    textureDesc.isRenderTarget = true;
                    textureDesc.isShaderResource = false;
                    textureDesc.debugName = "BackBuffer" + std::to_string(i);
                    textureDesc.initialState = nvrhi::ResourceStates::Present;
                    textureDesc.keepInitialState = true;

                    m_BackBuffers.push_back(m_NvrhiDevice->createHandleForNativeTexture(
                        nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(buffer.Get()), textureDesc));
                    m_NativeBackBuffers.push_back(std::move(buffer));
                }
                return true;
            }

            void ReleaseBackBuffers()
            {
                m_BackBuffers.clear();
                m_NativeBackBuffers.clear();
            }

            BackendDesc m_Desc;
            ComPtr<IDXGIFactory2> m_Factory;
            ComPtr<ID3D12Device> m_Device;
            ComPtr<ID3D12CommandQueue> m_Queue;
            ComPtr<IDXGISwapChain3> m_SwapChain;
            nvrhi::DeviceHandle m_NvrhiDevice;

            std::vector<ComPtr<ID3D12Resource>> m_NativeBackBuffers;
            std::vector<nvrhi::TextureHandle> m_BackBuffers;

            ComPtr<ID3D12Fence> m_FrameFence;
            std::vector<HANDLE> m_FrameEvents;
            uint64_t m_FrameCount = 0;
        };
    }

    std::unique_ptr<DeviceBackend> CreateD3D12Backend(const BackendDesc& desc)
    {
        auto backend = std::make_unique<D3D12Backend>();
        if (!backend->Init(desc))
            return nullptr;
        return backend;
    }
}
