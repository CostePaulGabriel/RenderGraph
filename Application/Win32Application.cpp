#include "Application/Win32Application.h"

#include <windows.h>

namespace LynxEngine
{
    namespace
    {
        constexpr wchar_t kWindowClass[] = L"RenderGraphPrototypeWindow";

        LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (message == WM_NCCREATE)
            {
                const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            }

            auto* app = reinterpret_cast<Win32Application*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (app)
                return LRESULT(app->HandleMessage(hwnd, message, uint64_t(wParam), int64_t(lParam)));
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    Win32Application::~Win32Application()
    {
        if (m_Window)
            DestroyWindow(static_cast<HWND>(m_Window));
        UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
    }

    bool Win32Application::Create(const std::wstring& title, uint32_t width, uint32_t height)
    {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        const HINSTANCE instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kWindowClass;
        if (!RegisterClassExW(&windowClass))
            return false;

        RECT rect{ 0, 0, LONG(width), LONG(height) };
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

        const HWND hwnd = CreateWindowExW(0, kWindowClass, title.c_str(), WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
            nullptr, nullptr, instance, this);
        if (!hwnd)
            return false;

        m_Window = hwnd;
        RECT client{};
        GetClientRect(hwnd, &client);
        m_Width = uint32_t(client.right - client.left);
        m_Height = uint32_t(client.bottom - client.top);
        m_Resized = false;

        ShowWindow(hwnd, SW_SHOWDEFAULT);
        UpdateWindow(hwnd);
        return true;
    }

    bool Win32Application::PumpMessages()
    {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                m_Running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return m_Running;
    }

    bool Win32Application::ConsumeResize()
    {
        const bool resized = m_Resized;
        m_Resized = false;
        return resized;
    }

    std::wstring Win32Application::GetExecutableDirectory()
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring directory(path);
        const size_t slash = directory.find_last_of(L"\\/");
        return slash == std::wstring::npos ? L"." : directory.substr(0, slash);
    }

    int64_t Win32Application::HandleMessage(void* window, uint32_t message, uint64_t wParam, int64_t lParam)
    {
        const HWND hwnd = static_cast<HWND>(window);

        if (m_Hook && m_Hook(window, message, wParam, lParam))
            return 1;

        switch (message)
        {
        case WM_SIZE:
        {
            const uint32_t width = LOWORD(lParam);
            const uint32_t height = HIWORD(lParam);
            if (width != m_Width || height != m_Height)
            {
                m_Width = width;
                m_Height = height;
                m_Resized = true;
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        case WM_CLOSE:
            // The window is destroyed by the destructor, after the renderer released its swap chain.
            m_Running = false;
            return 0;
        case WM_DESTROY:
            m_Window = nullptr;
            m_Running = false;
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd, message, WPARAM(wParam), LPARAM(lParam));
    }
}
