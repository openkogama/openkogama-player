#include "log.h"
#include "url.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>

namespace {

std::mutex lock;
std::ofstream file;

}

void setLogFile(const std::string& path)
{
    std::lock_guard guard(lock);
    file.open(pathFromUtf8(path), std::ios::app);
}

void writeLog(std::string_view line)
{
    auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    std::string text = std::format("[{:%T}] {}\n", now, line);

    std::lock_guard guard(lock);
    std::fwrite(text.data(), 1, text.size(), stderr);
    std::fflush(stderr);
    if (file.is_open())
        file << text << std::flush;
}
