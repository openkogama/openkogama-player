#include "platform_win.h"
#include "../log.h"
#include "../platform.h"

#include <wininet.h>

#include <map>
#include <memory>
#include <thread>

namespace {

constexpr UINT TaskMessage = WM_APP + 1;

struct Timer {
    bool repeat;
    std::function<void(uint32_t)> tick;
};

HWND frameWindow = nullptr;
HWND taskWindow = nullptr;
std::map<UINT_PTR, Timer> timers;
UINT_PTR nextTimer = 1;

LRESULT CALLBACK taskProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == TaskMessage) {
        std::unique_ptr<std::function<void()>> task(reinterpret_cast<std::function<void()>*>(lParam));
        (*task)();
        return 0;
    }

    if (message == WM_TIMER) {
        auto it = timers.find(wParam);
        if (it == timers.end()) {
            KillTimer(hwnd, wParam);
            return 0;
        }
        auto tick = it->second.tick;
        if (!it->second.repeat) {
            KillTimer(hwnd, wParam);
            timers.erase(it);
        }
        tick(static_cast<uint32_t>(wParam));
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

std::string queryText(HINTERNET request, DWORD info)
{
    DWORD size = 0;
    HttpQueryInfoW(request, info, nullptr, &size, nullptr);
    if (size == 0)
        return {};
    std::wstring text(size / sizeof(wchar_t), L'\0');
    if (!HttpQueryInfoW(request, info, text.data(), &size, nullptr))
        return {};
    text.resize(size / sizeof(wchar_t));
    return platform::win::narrow(text);
}

platform::Response fetch(const std::string& url, const std::optional<std::string>& post)
{
    platform::Response response;
    std::wstring address = platform::win::widen(url);

    wchar_t host[256] {};
    wchar_t path[4096] {};
    wchar_t extra[4096] {};
    URL_COMPONENTSW parts { sizeof(parts) };
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!InternetCrackUrlW(address.c_str(), 0, 0, &parts))
        return response;

    HINTERNET session = InternetOpenW(L"OpenKogama Player", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    HINTERNET connection = session ? InternetConnectW(session, host, parts.nPort, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0) : nullptr;
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI;
    if (parts.nScheme == INTERNET_SCHEME_HTTPS)
        flags |= INTERNET_FLAG_SECURE;
    std::wstring target = std::wstring(path) + extra;
    HINTERNET request = connection ? HttpOpenRequestW(connection, post ? L"POST" : L"GET", target.c_str(), nullptr, nullptr, nullptr, flags, 0) : nullptr;

    if (request) {
        std::wstring headers;
        std::string body = post.value_or("");
        size_t split = body.find("\r\n\r\n");
        if (post && split != std::string::npos && body.find(':') < split) {
            headers = platform::win::widen(body.substr(0, split + 2));
            body.erase(0, split + 4);
        }

        if (HttpSendRequestW(request, headers.empty() ? nullptr : headers.c_str(), static_cast<DWORD>(headers.size()), body.empty() ? nullptr : body.data(), static_cast<DWORD>(body.size()))) {
            DWORD status = 0;
            DWORD size = sizeof(status);
            HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &size, nullptr);
            response.mime = queryText(request, HTTP_QUERY_CONTENT_TYPE);
            response.headers = queryText(request, HTTP_QUERY_RAW_HEADERS_CRLF);

            char buffer[64 * 1024];
            DWORD read = 0;
            while (InternetReadFile(request, buffer, sizeof(buffer), &read) && read > 0)
                response.body.insert(response.body.end(), buffer, buffer + read);
            response.ok = status >= 200 && status < 400;
            trace("{} {} ({} bytes)", status, url, response.body.size());
        } else {
            trace("request to {} failed: {}", url, GetLastError());
        }
    }

    if (request)
        InternetCloseHandle(request);
    if (connection)
        InternetCloseHandle(connection);
    if (session)
        InternetCloseHandle(session);
    return response;
}

}

namespace platform::win {

void initialize(HINSTANCE instance, HWND frame)
{
    frameWindow = frame;

    WNDCLASSW tasks {};
    tasks.lpfnWndProc = taskProc;
    tasks.hInstance = instance;
    tasks.lpszClassName = L"OpenKogamaTasks";
    RegisterClassW(&tasks);
    taskWindow = CreateWindowExW(0, tasks.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
}

std::wstring widen(std::string_view text)
{
    if (text.empty())
        return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string narrow(std::wstring_view text)
{
    if (text.empty())
        return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
    return utf8;
}

}

namespace platform {

void post(std::function<void()> task)
{
    auto* copy = new std::function<void()>(std::move(task));
    if (!taskWindow || !PostMessageW(taskWindow, TaskMessage, 0, reinterpret_cast<LPARAM>(copy)))
        delete copy;
}

uint32_t startTimer(uint32_t interval, bool repeat, std::function<void(uint32_t)> tick)
{
    UINT_PTR id = nextTimer++;
    timers[id] = { repeat, std::move(tick) };
    SetTimer(taskWindow, id, interval, nullptr);
    return static_cast<uint32_t>(id);
}

void stopTimer(uint32_t id)
{
    KillTimer(taskWindow, id);
    timers.erase(id);
}

void stopTimers()
{
    for (const auto& entry : timers)
        KillTimer(taskWindow, entry.first);
    timers.clear();
}

void download(const std::string& url, std::optional<std::string> post, std::function<void(Response)> done)
{
    std::thread([url, post = std::move(post), done = std::move(done)] {
        auto response = std::make_shared<Response>(fetch(url, post));
        platform::post([done, response] { done(std::move(*response)); });
    }).detach();
}

void* nativeWindow()
{
    return frameWindow;
}

std::string temporaryPath(const std::string& name)
{
    wchar_t directory[MAX_PATH] {};
    GetTempPathW(MAX_PATH, directory);
    return win::narrow(directory) + name;
}

std::string nativePath(const std::string& path)
{
    std::wstring wide = win::widen(path);
    int size = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string ansi(size, '\0');
    WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, ansi.data(), size, nullptr, nullptr);
    ansi.resize(size > 0 ? size - 1 : 0);
    return ansi;
}

const char* platformName()
{
    return "Win32";
}

}
