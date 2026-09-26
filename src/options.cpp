#include "options.h"
#include "url.h"

#include <charconv>
#include <fstream>
#include <iterator>

const char* const usage =
    "usage: openkogama-player <file.unity3d>[?query] [options]\n"
    "\n"
    "  --version name             load versions/<name>.args, falling back to shorter prefixes (1.8.11.1, 1.8.11, 1.8)\n"
    "  --param name=value         extra <embed> parameter\n"
    "  --reply function=json      answer a page callback with json, @file or an http(s) url\n"
    "  --send function=Method:arg SendMessage to the bridge object when the page function is called\n"
    "  --object name              bridge game object (default BrowserComm)\n"
    "  --exit-message Method      SendMessage to the bridge object when the window closes, then wait before quitting\n"
    "  --page url                 address of the hosting page\n"
    "  --serve-as url             show the local file to the game at this address\n"
    "  --plugin path              Unity Web Player plugin to load\n"
    "  --title text               window title\n"
    "  --size WxH                 window size (default 940x482)\n"
    "  --log path                 also write the log to a file\n"
    "  --probe seconds            periodically log the game state read from the Mono runtime\n"
    "  --after-load state=Method  call MVNetworkGame.Method when a level load finishes but the game stays in state\n"
    "  --start-session function   start the game with the reply to function when it shows its login form instead of asking for it\n";

namespace {

bool split(const std::string& text, char separator, std::pair<std::string, std::string>& parts)
{
    size_t at = text.find(separator);
    if (at == std::string::npos)
        return false;
    parts = { text.substr(0, at), text.substr(at + 1) };
    return true;
}

bool readValue(const std::string& text, std::string& value, std::string& error)
{
    if (!text.starts_with("@")) {
        value = text;
        return true;
    }

    std::ifstream in(pathFromUtf8(text.substr(1)), std::ios::binary);
    if (!in) {
        error = "cannot read " + text.substr(1);
        return false;
    }
    value.assign(std::istreambuf_iterator<char>(in), {});
    if (value.starts_with("\xEF\xBB\xBF"))
        value.erase(0, 3);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return true;
}

}

bool parseOptions(std::vector<std::string> args, const std::string& executableDir, Options& options, std::string& error)
{
    for (size_t i = 0; i + 1 < args.size(); i++) {
        if (args[i] != "--version")
            continue;

        std::string name = args[i + 1];
        std::ifstream in;
        while (true) {
            in.open(pathFromUtf8(executableDir + "/versions/" + name + ".args"));
            size_t dot = name.rfind('.');
            if (in || dot == std::string::npos)
                break;
            name.erase(dot);
        }
        if (!in) {
            error = "no settings for version " + args[i + 1] + " in " + executableDir + "/versions";
            return false;
        }

        std::vector<std::string> lines;
        for (std::string line; std::getline(in, line);) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (!line.empty() && !line.starts_with("#"))
                lines.push_back(line);
        }
        args.erase(args.begin() + i, args.begin() + i + 2);
        args.insert(args.begin(), lines.begin(), lines.end());
        break;
    }

    std::string source;
    std::string serveAs;
    for (size_t i = 0; i < args.size(); i++) {
        const std::string& arg = args[i];
        if (!arg.starts_with("--")) {
            if (!source.empty()) {
                error = "more than one file given";
                return false;
            }
            source = arg;
            continue;
        }

        if (i + 1 >= args.size()) {
            error = arg + " needs a value";
            return false;
        }
        const std::string& value = args[++i];
        std::pair<std::string, std::string> parts;

        if (arg == "--param") {
            if (!split(value, '=', parts)) {
                error = "--param expects name=value";
                return false;
            }
            options.host.params.push_back(parts);
        } else if (arg == "--reply") {
            if (!split(value, '=', parts)) {
                error = "--reply expects function=json";
                return false;
            }
            if (!readValue(parts.second, options.host.replies[parts.first], error))
                return false;
        } else if (arg == "--send") {
            std::pair<std::string, std::string> message;
            if (!split(value, '=', parts) || !split(parts.second, ':', message)) {
                error = "--send expects function=Method:argument";
                return false;
            }
            options.host.sends[parts.first] = message;
        } else if (arg == "--exit-message") {
            options.host.exitMessage = value;
        } else if (arg == "--object") {
            options.host.bridge = value;
        } else if (arg == "--serve-as") {
            serveAs = value;
        } else if (arg == "--page") {
            options.host.page = value;
        } else if (arg == "--plugin") {
            options.plugin = value;
        } else if (arg == "--title") {
            options.title = value;
        } else if (arg == "--size") {
            size_t x = value.find('x');
            const char* end = value.data() + value.size();
            bool parsed = x != std::string::npos
                && std::from_chars(value.data(), value.data() + x, options.width).ptr == value.data() + x
                && std::from_chars(value.data() + x + 1, end, options.height).ptr == end;
            if (!parsed || options.width <= 0 || options.height <= 0) {
                error = "--size expects WxH";
                return false;
            }
        } else if (arg == "--log") {
            options.log = value;
        } else if (arg == "--after-load") {
            int state = 0;
            if (!split(value, '=', parts) || std::from_chars(parts.first.data(), parts.first.data() + parts.first.size(), state).ptr != parts.first.data() + parts.first.size() || parts.second.empty()) {
                error = "--after-load expects state=Method";
                return false;
            }
            options.afterLoad[state] = parts.second;
        } else if (arg == "--start-session") {
            options.startSession = value;
        } else if (arg == "--probe") {
            const char* end = value.data() + value.size();
            if (std::from_chars(value.data(), end, options.probe).ptr != end || options.probe <= 0) {
                error = "--probe expects seconds";
                return false;
            }
        } else {
            error = "unknown option " + arg;
            return false;
        }
    }

    if (source.empty()) {
        error = "no file given";
        return false;
    }

    if (hasScheme(source)) {
        options.host.source = source;
        if (options.host.page.empty()) {
            std::string address = stripQuery(source);
            options.host.page = address.substr(0, address.rfind('/') + 1);
        }
        return true;
    }

    size_t query = source.find('?');
    std::filesystem::path file = std::filesystem::absolute(pathFromUtf8(source.substr(0, query)));
    if (!std::filesystem::exists(file)) {
        error = "file not found: " + pathToUtf8(file);
        return false;
    }

    std::string parameters = query == std::string::npos ? "" : source.substr(query);
    if (!serveAs.empty()) {
        options.host.localSource = pathToFileUrl(pathToUtf8(file));
        options.host.source = serveAs + parameters;
        if (options.host.page.empty())
            options.host.page = serveAs.substr(0, serveAs.rfind('/') + 1);
        return true;
    }

    options.host.source = pathToFileUrl(pathToUtf8(file)) + parameters;
    if (options.host.page.empty())
        options.host.page = pathToFileUrl(pathToUtf8(file.parent_path())) + "/";
    return true;
}
