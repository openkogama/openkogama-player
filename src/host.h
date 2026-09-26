#pragma once

#include "npapi.h"
#include "platform.h"
#include "script.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

struct HostOptions {
    std::string source;
    std::string page;
    std::vector<std::pair<std::string, std::string>> params;
    std::map<std::string, std::string> replies;
    std::map<std::string, std::pair<std::string, std::string>> sends;
    std::string bridge = "BrowserComm";
    std::string exitMessage;
    std::string localSource;
    std::string userAgent = "Mozilla/5.0 (Windows NT 6.1; rv:52.0) Gecko/20100101 Firefox/52.0";
};

class Host {
public:
    explicit Host(HostOptions options);

    bool start(NP_GetEntryPointsFunc getEntryPoints, NP_InitializeFunc initialize, void* handle, uint32_t width, uint32_t height);
    void resize(uint32_t width, uint32_t height);
    void stop(NP_ShutdownFunc shutdown);

    NPError getUrl(const char* url, const char* target, bool notify, void* notifyData);
    NPError postUrl(const char* url, const char* target, std::string data, bool file, bool notify, void* notifyData);
    NPError getValue(NPNVariable variable, void* value);
    bool evaluate(NPObject* scope, const std::string& script, NPVariant* result);
    void cancel(NPStream* stream, NPReason reason);
    const char* userAgent() const;
    bool requestExit();
    bool wasCalled(const std::string& name) const;
    void fetchReply(const std::string& name, std::function<void(std::string)> done);

private:
    struct Stream;

    void createPage();
    bool pageCall(const std::string& name, const std::vector<Value>& args, Value& result);
    void sendMessage(const std::string& object, const std::string& method, const std::string& value);
    void open(const std::string& url, std::optional<std::string> post, bool notify, void* notifyData);
    void deliver(std::shared_ptr<Stream> stream, platform::Response response);
    void pump(std::shared_ptr<Stream> stream);
    void finish(std::shared_ptr<Stream> stream, NPReason reason);
    void notifyDone(const std::string& url, NPReason reason, bool notify, void* notifyData);

    HostOptions options;
    NPNetscapeFuncs browser {};
    NPPluginFuncs plugin {};
    NPP_t instance {};
    NPWindow window {};
    std::string mime = "application/vnd.unity";
    std::vector<std::string> argumentNames;
    std::vector<std::string> argumentValues;
    std::vector<char*> argn;
    std::vector<char*> argv;
    ObjectRef page;
    ObjectRef element;
    std::vector<std::shared_ptr<Stream>> streams;
    bool running = false;
    std::set<std::string> called;
};
