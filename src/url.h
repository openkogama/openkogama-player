#pragma once

#include <filesystem>
#include <string>
#include <string_view>

struct UrlParts {
    std::string protocol;
    std::string host;
    std::string path;
    std::string query;
};

std::filesystem::path pathFromUtf8(const std::string& path);
std::string pathToUtf8(const std::filesystem::path& path);

std::string pathToFileUrl(const std::string& path);
std::string fileUrlToPath(std::string_view url);
std::string resolveUrl(const std::string& base, const std::string& url);
std::string stripQuery(std::string_view url);
std::string decodeUrl(std::string_view text);
std::string originOf(const std::string& url);
UrlParts splitUrl(const std::string& url);
bool hasScheme(std::string_view url);
bool isFileUrl(std::string_view url);
