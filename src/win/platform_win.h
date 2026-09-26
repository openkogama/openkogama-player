#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace platform::win {

void initialize(HINSTANCE instance, HWND frame);
std::wstring widen(std::string_view text);
std::string narrow(std::wstring_view text);

}
