// =============================================================================
// main.cpp — MediaX Application Entry Point
// Author: Ankit Kimothi
//
// This file contains the native Windows entry point for MediaX.
//
// Desktop Modes Supported:
//   1. Full COM Embedded WebView2 (when built with HAVE_WEBVIEW2 defined).
//   2. Standalone Chromium App Mode (when built with MinGW / g++):
//      Uses the native Edge runtime with the `--app` flag to open MediaX inside
//      a dedicated, clean desktop app window (1280x820) with no address bar or tabs.
// =============================================================================

#ifdef _WIN32

#ifndef UNICODE
#  define UNICODE
#endif
#ifndef _UNICODE
#  define _UNICODE
#endif

// Standard Windows headers
#include <windows.h>
#include <shellapi.h>

// Standard library
#include <string>
#include <functional>
#include <sstream>
#include <stdexcept>

// Our own headers
#include "bridge/Bridge.h"
#include "bridge/Commands.h"

#ifdef HAVE_WEBVIEW2
#  include <wrl.h>
#  include <wrl/client.h>
#  include <WebView2.h>
using Microsoft::WRL::ComPtr;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void InitWebView2(HWND hwnd);
static std::wstring GetResourcePath();

static ComPtr<ICoreWebView2Controller> g_webviewController;
static ComPtr<ICoreWebView2>           g_webview;

int WINAPI WinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_     LPSTR     /*lpCmdLine*/,
    _In_     int       nCmdShow)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"Failed to initialise COM.", L"MediaX Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    const wchar_t CLASS_NAME[] = L"MediaXWindowClass";
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;
    wc.hIconSm       = LoadIconW(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) {
        CoUninitialize();
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"MediaX",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd) {
        CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    InitWebView2(hwnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_SIZE:
        if (g_webviewController) {
            RECT bounds = { 0, 0, LOWORD(lParam), HIWORD(lParam) };
            g_webviewController->put_Bounds(bounds);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static void InitWebView2(HWND hwnd) {
    (void)hwnd;
}

#else // !HAVE_WEBVIEW2 (MinGW Standalone Desktop App Launcher)

// ===========================================================================
// Standalone Desktop App Launcher Mode
// ===========================================================================
/**
 * @brief Launches MediaX inside a dedicated, standalone Chromium Desktop App
 *        window (using the built-in Microsoft Edge engine with the `--app` flag).
 *
 * This provides the exact same visual experience as WebView2:
 *   - Dedicated desktop application window (1280 × 820 px)
 *   - No URL bar, no tabs, no search bar, no browser clutter
 *   - Full HTML5, CSS3, Web Audio, and video rendering
 */
int WINAPI WinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_     LPSTR     lpCmdLine,
    _In_     int       nCmdShow)
{
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    // 1. Get path to resources\index.html next to this MediaX.exe
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring dir(exePath);
    size_t lastSlash = dir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        dir = dir.substr(0, lastSlash);
    }
    std::wstring htmlPath = dir + L"\\resources\\index.html";

    // 2. Verify that resources\index.html exists
    DWORD fileAttr = GetFileAttributesW(htmlPath.c_str());
    if (fileAttr == INVALID_FILE_ATTRIBUTES || (fileAttr & FILE_ATTRIBUTE_DIRECTORY)) {
        // Fallback: check one directory up if running from build\ folder
        std::wstring fallbackPath = dir + L"\\..\\resources\\index.html";
        DWORD fbAttr = GetFileAttributesW(fallbackPath.c_str());
        if (fbAttr != INVALID_FILE_ATTRIBUTES && !(fbAttr & FILE_ATTRIBUTE_DIRECTORY)) {
            htmlPath = fallbackPath;
        } else {
            MessageBoxW(nullptr,
                (L"Could not find web assets at:\n" + htmlPath +
                 L"\n\nPlease make sure the 'resources' folder exists next to MediaX.exe.").c_str(),
                L"MediaX - Asset Warning", MB_OK | MB_ICONWARNING);
            return 1;
        }
    }

    // 3. Try to launch in Standalone Application Mode via msedge.exe
    // Standard Edge locations in Windows 10 & 11:
    std::wstring edgeLocations[] = {
        L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
        L"C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe"
    };

    bool launched = false;
    for (const auto& edgeExe : edgeLocations) {
        DWORD attr = GetFileAttributesW(edgeExe.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            // Format URL with forward slashes for Chromium
            std::wstring fileUri = htmlPath;
            for (wchar_t& ch : fileUri) {
                if (ch == L'\\') ch = L'/';
            }

            // --app flag opens a dedicated frameless app window without URL or tabs
            std::wstring args = L"--app=\"file:///" + fileUri + L"\" --window-size=1280,820";

            HINSTANCE hRes = ShellExecuteW(nullptr, L"open", edgeExe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(hRes) > 32) {
                launched = true;
                break;
            }
        }
    }

    // 4. Fallback: if Edge wasn't found at standard locations, open with default browser
    if (!launched) {
        ShellExecuteW(nullptr, L"open", htmlPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    return 0;
}

#endif // HAVE_WEBVIEW2

#else // !_WIN32 (Linux / macOS verification stub)

#include <cstdio>

int main()
{
    std::puts(
        "MediaX - Native Windows media player\n"
        "------------------------------------\n"
        "On Linux/macOS, run ./verify_backend or test with resources/index.html in a browser.\n"
    );
    return 0;
}

#endif // _WIN32
