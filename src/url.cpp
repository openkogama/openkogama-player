#include "url.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace {

bool plain(unsigned char c)
{
    return std::isalnum(c) || std::string_view("-._~/:!$&'()*+,;=@").find(static_cast<char>(c)) != std::string_view::npos;
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}

std::filesystem::path pathFromUtf8(const std::string& path)
{
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

std::string pathToUtf8(const std::filesystem::path& path)
{
    std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

std::string pathToFileUrl(const std::string& path)
{
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    std::string url = normalized.starts_with("/") ? "file://" : "file:///";
    for (unsigned char c : normalized) {
        if (plain(c))
            url += static_cast<char>(c);
        else
            url += std::format("%{:02X}", c);
    }
    return url;
}

std::string decodeUrl(std::string_view text)
{
    std::string decoded;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '%' && i + 2 < text.size() && hexDigit(text[i + 1]) >= 0 && hexDigit(text[i + 2]) >= 0) {
            decoded += static_cast<char>(hexDigit(text[i + 1]) * 16 + hexDigit(text[i + 2]));
            i += 2;
        } else {
            decoded += text[i];
        }
    }
    return decoded;
}

std::string stripQuery(std::string_view url)
{
    return std::string(url.substr(0, url.find_first_of("?#")));
}

std::string fileUrlToPath(std::string_view url)
{
    std::string_view rest = url.substr(url.find(':') + 1);
    if (rest.starts_with("//")) {
        rest.remove_prefix(2);
        if (rest.starts_with("localhost/"))
            rest.remove_prefix(9);
    }

    std::string path = decodeUrl(stripQuery(rest));
    if (path.size() > 2 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':')
        path.erase(0, 1);
    return path;
}

bool hasScheme(std::string_view url)
{
    size_t colon = url.find(':');
    if (colon == std::string_view::npos || colon < 2)
        return false;
    return std::all_of(url.begin(), url.begin() + colon, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.';
    });
}

bool isFileUrl(std::string_view url)
{
    return url.starts_with("file:");
}

UrlParts splitUrl(const std::string& url)
{
    UrlParts parts;
    size_t colon = url.find(':');
    if (!hasScheme(url))
        return parts;

    parts.protocol = url.substr(0, colon + 1);
    std::string rest = url.substr(colon + 1);
    if (rest.starts_with("//")) {
        size_t slash = rest.find_first_of("/?#", 2);
        parts.host = rest.substr(2, slash == std::string::npos ? std::string::npos : slash - 2);
        rest = slash == std::string::npos ? "" : rest.substr(slash);
    }

    size_t query = rest.find('?');
    parts.path = rest.substr(0, query);
    if (query != std::string::npos)
        parts.query = rest.substr(query);
    return parts;
}

std::string originOf(const std::string& url)
{
    UrlParts parts = splitUrl(url);
    return parts.protocol + "//" + parts.host;
}

std::string resolveUrl(const std::string& base, const std::string& url)
{
    if (hasScheme(url))
        return url;
    if (url.starts_with("//"))
        return splitUrl(base).protocol + url;
    if (url.starts_with("/"))
        return originOf(base) + url;

    std::string directory = stripQuery(base);
    return directory.substr(0, directory.rfind('/') + 1) + url;
}
