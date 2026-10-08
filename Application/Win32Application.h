#pragma once

// Minimal Win32 platform layer: one window and the message loop. No graphics code here.

#include <cstdint>
#include <functional>
#include <string>

namespace LynxEngine
{
    class Win32Application
    {
    public:
        // Return true to mark the message as handled (used to forward input to ImGui).
        using MessageHook = std::function<bool(void* hwnd, uint32_t message, uint64_t wParam, int64_t lParam)>;

        Win32Application() = default;
        ~Win32Application();

        Win32Application(const Win32Application&) = delete;
        Win32Application& operator=(const Win32Application&) = delete;

        bool Create(const std::wstring& title, uint32_t width, uint32_t height);

        // Processes pending messages. Returns false once the window was closed.
        bool PumpMessages();

        void SetMessageHook(MessageHook hook) { m_Hook = std::move(hook); }

        void* GetNativeHandle() const { return m_Window; }
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        bool IsMinimized() const { return m_Width == 0 || m_Height == 0; }

        // True once after the client area changed size.
        bool ConsumeResize();

        static std::wstring GetExecutableDirectory();

        // Called by the window procedure (internal).
        int64_t HandleMessage(void* hwnd, uint32_t message, uint64_t wParam, int64_t lParam);

    private:
        void* m_Window = nullptr;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        bool m_Resized = false;
        bool m_Running = true;
        MessageHook m_Hook;
    };
}
