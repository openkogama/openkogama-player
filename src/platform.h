#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace platform {

struct Response {
    bool ok = false;
    std::string mime;
    std::string headers;
    std::vector<char> body;
    std::string file;
};

void post(std::function<void()> task);
uint32_t startTimer(uint32_t interval, bool repeat, std::function<void(uint32_t)> tick);
void stopTimer(uint32_t id);
void stopTimers();
void download(const std::string& url, std::optional<std::string> post, std::function<void(Response)> done);
void* nativeWindow();
std::string temporaryPath(const std::string& name);
std::string nativePath(const std::string& path);
const char* platformName();

}
