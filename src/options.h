#pragma once

#include "host.h"

#include <map>
#include <string>
#include <vector>

struct Options {
    HostOptions host;
    std::string plugin;
    std::string title = "OpenKogama Player";
    int width = 940;
    int height = 482;
    std::string log;
    int probe = 0;
    std::map<int, std::string> afterLoad;
    std::string startSession;
};

extern const char* const usage;

bool parseOptions(std::vector<std::string> args, const std::string& executableDir, Options& options, std::string& error);
