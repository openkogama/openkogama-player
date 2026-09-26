#pragma once

#include <format>
#include <string>
#include <string_view>

void setLogFile(const std::string& path);
void writeLog(std::string_view line);

template <class... Args>
void trace(std::format_string<Args...> format, Args&&... args)
{
    writeLog(std::format(format, std::forward<Args>(args)...));
}
