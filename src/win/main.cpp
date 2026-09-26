#include "platform_win.h"
#include "unity.h"
#include "../host.h"
#include "../log.h"
#include "../options.h"

#include <knownfolders.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstdio>

using platform::win::narrow;
using platform::win::widen;

namespace {

Host* current = nullptr;
HWND pluginWindow = nullptr;
bool closing = false;

LRESULT CALLBACK frameProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_SIZE:
        if (pluginWindow && wParam != SIZE_MINIMIZED) {
            MoveWindow(pluginWindow, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
            if (current)
                current->resize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_CLOSE:
        closing = true;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

std::string findPlugin(const std::string& configured)
{
    if (!configured.empty())
        return configured;

    for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
        wchar_t path[MAX_PATH] {};
        DWORD size = sizeof(path);
        if (RegGetValueW(root, L"SOFTWARE\\MozillaPlugins\\@unity3d.com/UnityPlayer,version=1.0", L"Path", RRF_RT_REG_SZ, nullptr, path, &size) == ERROR_SUCCESS)
            return narrow(path);
    }

    PWSTR low = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &low))) {
        std::wstring path = std::wstring(low) + L"\\Unity\\WebPlayer\\loader\\npUnity3D32.dll";
        CoTaskMemFree(low);
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            return narrow(path);
    }
    return {};
}

int fail(const std::string& message)
{
    trace("{}", message);
    MessageBoxW(nullptr, widen(message).c_str(), L"OpenKogama Player", MB_ICONERROR);
    return 1;
}

}

int main()
{
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> args;
    for (int i = 1; i < count; i++)
        args.push_back(narrow(arguments[i]));
    LocalFree(arguments);

    Options options;
    std::string error;
    wchar_t executable[MAX_PATH] {};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    std::string executableDir = narrow(executable);
    executableDir = executableDir.substr(0, executableDir.find_last_of("\\/"));

    if (!parseOptions(args, executableDir, options, error)) {
        std::fprintf(stderr, "%s\n\n%s", error.c_str(), usage);
        return 2;
    }
    if (!options.log.empty())
        setLogFile(options.log);

    std::string pluginPath = findPlugin(options.plugin);
    if (pluginPath.empty())
        return fail("Unity Web Player is not installed.");

    trace("plugin {}", pluginPath);
    HMODULE module = LoadLibraryExW(widen(pluginPath).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module)
        return fail("Could not load " + pluginPath + " (error " + std::to_string(GetLastError()) + ")");

    auto getEntryPoints = reinterpret_cast<NP_GetEntryPointsFunc>(GetProcAddress(module, "NP_GetEntryPoints"));
    auto initialize = reinterpret_cast<NP_InitializeFunc>(GetProcAddress(module, "NP_Initialize"));
    auto shutdown = reinterpret_cast<NP_ShutdownFunc>(GetProcAddress(module, "NP_Shutdown"));
    if (!getEntryPoints || !initialize)
        return fail(pluginPath + " is not an NPAPI plugin");

    OleInitialize(nullptr);
    HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSW frameClass {};
    frameClass.lpfnWndProc = frameProc;
    frameClass.hInstance = instance;
    frameClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    frameClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    frameClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    frameClass.lpszClassName = L"OpenKogamaPlayer";
    RegisterClassW(&frameClass);

    WNDCLASSW pluginClass {};
    pluginClass.lpfnWndProc = DefWindowProcW;
    pluginClass.hInstance = instance;
    pluginClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    pluginClass.lpszClassName = L"OpenKogamaPlugin";
    RegisterClassW(&pluginClass);

    RECT bounds { 0, 0, options.width, options.height };
    AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE);
    HWND frame = CreateWindowExW(0, frameClass.lpszClassName, widen(options.title).c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, instance, nullptr);
    platform::win::initialize(instance, frame);

    pluginWindow = CreateWindowExW(0, pluginClass.lpszClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, options.width, options.height, frame, nullptr, instance, nullptr);
    ShowWindow(frame, SW_SHOW);
    UpdateWindow(frame);

    Host host(options.host);
    if (!host.start(getEntryPoints, initialize, pluginWindow, options.width, options.height))
        return fail("Unity Web Player could not start " + options.host.source);
    current = &host;
    UnityBridge unity;
    LevelLoadWatcher watcher(options.afterLoad);
    if (!options.afterLoad.empty())
        platform::startTimer(10, true, [&](uint32_t) { watcher.tick(unity); });
    if (options.probe > 0) {
        platform::startTimer(options.probe * 1000, true, [&](uint32_t) {
            if (std::optional<GameState> state = unity.read())
                trace("probe: frame {} loading {} level {} JoinState {} ConnState {}", state->frame, state->loadingLevel, state->level,
                    state->joinState.value_or(-1), state->connectionState.value_or(-1));
        });
    }

    MSG message;
    while (!closing && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    current = nullptr;
    host.stop(shutdown);
    DestroyWindow(frame);
    OleUninitialize();
    return 0;
}
