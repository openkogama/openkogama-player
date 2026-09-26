#include "host.h"
#include "log.h"
#include "url.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <regex>

namespace {

constexpr const char* UnityMime = "application/vnd.unity";
constexpr int32_t ChunkSize = 256 * 1024;

Host* active = nullptr;

Host& self()
{
    return *active;
}

char* duplicate(const std::string& text)
{
    char* copy = static_cast<char*>(std::malloc(text.size() + 1));
    std::memcpy(copy, text.c_str(), text.size() + 1);
    return copy;
}

std::string mimeFor(const std::string& url)
{
    std::string path = stripQuery(url);
    if (path.ends_with(".unity3d") || path.ends_with(".unityweb"))
        return UnityMime;
    return "application/octet-stream";
}

std::string shorten(const std::string& text)
{
    return text.size() > 300 ? text.substr(0, 300) + "..." : text;
}

template <class Map>
auto lookup(const Map& map, const std::string& name)
{
    auto it = map.find(name);
    if (it == map.end() && name.starts_with("UNITY_"))
        it = map.find(name.substr(6));
    return it;
}

NPError npnGetUrl(NPP, const char* url, const char* target)
{
    return self().getUrl(url, target, false, nullptr);
}

NPError npnPostUrl(NPP, const char* url, const char* target, uint32_t length, const char* buffer, NPBool file)
{
    return self().postUrl(url, target, std::string(buffer, length), file, false, nullptr);
}

NPError npnRequestRead(NPStream*, NPByteRange*)
{
    return NPERR_GENERIC_ERROR;
}

NPError npnNewStream(NPP, NPMIMEType, const char* target, NPStream**)
{
    trace("plugin tried to open a stream to {}", target ? target : "");
    return NPERR_GENERIC_ERROR;
}

int32_t npnWrite(NPP, NPStream*, int32_t, void*)
{
    return -1;
}

NPError npnDestroyStream(NPP, NPStream* stream, NPReason reason)
{
    self().cancel(stream, reason);
    return NPERR_NO_ERROR;
}

void npnStatus(NPP, const char* message)
{
    trace("status: {}", message ? message : "");
}

const char* npnUserAgent(NPP)
{
    return self().userAgent();
}

void* npnMemAlloc(uint32_t size)
{
    return std::malloc(size);
}

void npnMemFree(void* pointer)
{
    std::free(pointer);
}

uint32_t npnMemFlush(uint32_t)
{
    return 0;
}

void npnReloadPlugins(NPBool) {}

void* npnGetJavaEnv()
{
    return nullptr;
}

void* npnGetJavaPeer(NPP)
{
    return nullptr;
}

NPError npnGetUrlNotify(NPP, const char* url, const char* target, void* notifyData)
{
    return self().getUrl(url, target, true, notifyData);
}

NPError npnPostUrlNotify(NPP, const char* url, const char* target, uint32_t length, const char* buffer, NPBool file, void* notifyData)
{
    return self().postUrl(url, target, std::string(buffer, length), file, true, notifyData);
}

NPError npnGetValue(NPP, NPNVariable variable, void* value)
{
    return self().getValue(variable, value);
}

NPError npnSetValue(NPP, NPPVariable variable, void* value)
{
    trace("plugin set value {} = {}", variable, value);
    return NPERR_NO_ERROR;
}

void npnInvalidateRect(NPP, NPRect*) {}
void npnInvalidateRegion(NPP, void*) {}
void npnForceRedraw(NPP) {}

NPIdentifier npnGetStringIdentifier(const NPUTF8* name)
{
    return identifier(name ? name : "");
}

void npnGetStringIdentifiers(const NPUTF8** names, int32_t count, NPIdentifier* ids)
{
    for (int32_t i = 0; i < count; i++)
        ids[i] = identifier(names[i] ? names[i] : "");
}

NPIdentifier npnGetIntIdentifier(int32_t number)
{
    return intIdentifier(number);
}

bool npnIdentifierIsString(NPIdentifier id)
{
    return identifierIsString(id);
}

NPUTF8* npnUtf8FromIdentifier(NPIdentifier id)
{
    return identifierIsString(id) ? duplicate(identifierName(id)) : nullptr;
}

int32_t npnIntFromIdentifier(NPIdentifier id)
{
    return identifierInt(id);
}

NPObject* npnCreateObject(NPP npp, NPClass* cls)
{
    return createObject(npp, cls);
}

NPObject* npnRetainObject(NPObject* object)
{
    return retainObject(object);
}

void npnReleaseObject(NPObject* object)
{
    releaseObject(object);
}

bool npnInvoke(NPP, NPObject* object, NPIdentifier method, const NPVariant* args, uint32_t count, NPVariant* result)
{
    return object && object->_class->invoke && object->_class->invoke(object, method, args, count, result);
}

bool npnInvokeDefault(NPP, NPObject* object, const NPVariant* args, uint32_t count, NPVariant* result)
{
    return object && object->_class->invokeDefault && object->_class->invokeDefault(object, args, count, result);
}

bool npnEvaluate(NPP, NPObject* object, NPString* script, NPVariant* result)
{
    return self().evaluate(object, std::string(script->UTF8Characters, script->UTF8Length), result);
}

bool npnGetProperty(NPP, NPObject* object, NPIdentifier name, NPVariant* result)
{
    if (!object || !object->_class->getProperty)
        return false;
    if (object->_class->hasProperty && !object->_class->hasProperty(object, name)) {
        trace("script read of unknown property {}", identifierName(name));
        return false;
    }
    return object->_class->getProperty(object, name, result);
}

bool npnSetProperty(NPP, NPObject* object, NPIdentifier name, const NPVariant* value)
{
    return object && object->_class->setProperty && object->_class->setProperty(object, name, value);
}

bool npnRemoveProperty(NPP, NPObject* object, NPIdentifier name)
{
    return object && object->_class->removeProperty && object->_class->removeProperty(object, name);
}

bool npnHasProperty(NPP, NPObject* object, NPIdentifier name)
{
    return object && object->_class->hasProperty && object->_class->hasProperty(object, name);
}

bool npnHasMethod(NPP, NPObject* object, NPIdentifier name)
{
    return object && object->_class->hasMethod && object->_class->hasMethod(object, name);
}

void npnReleaseVariantValue(NPVariant* variant)
{
    releaseVariant(*variant);
}

void npnSetException(NPObject*, const NPUTF8* message)
{
    trace("script exception: {}", message ? message : "");
}

void npnPushPopupsEnabledState(NPP, NPBool) {}
void npnPopPopupsEnabledState(NPP) {}

bool npnEnumerate(NPP, NPObject* object, NPIdentifier** ids, uint32_t* count)
{
    return object && object->_class->structVersion >= 2 && object->_class->enumerate && object->_class->enumerate(object, ids, count);
}

void npnPluginThreadAsyncCall(NPP, void (*function)(void*), void* data)
{
    static int calls = 0;
    if (++calls % 100 == 1)
        trace("async call #{}", calls);
    platform::post([function, data] { function(data); });
}

bool npnConstruct(NPP, NPObject* object, const NPVariant* args, uint32_t count, NPVariant* result)
{
    return object && object->_class->structVersion >= 2 && object->_class->construct && object->_class->construct(object, args, count, result);
}

NPError npnGetValueForUrl(NPP, int, const char*, char**, uint32_t*)
{
    return NPERR_GENERIC_ERROR;
}

NPError npnSetValueForUrl(NPP, int, const char*, const char*, uint32_t)
{
    return NPERR_NO_ERROR;
}

NPError npnGetAuthenticationInfo(NPP, const char*, const char*, int32_t, const char*, const char*, char**, uint32_t*, char**, uint32_t*)
{
    return NPERR_GENERIC_ERROR;
}

uint32_t npnScheduleTimer(NPP npp, uint32_t interval, NPBool repeat, void (*function)(NPP, uint32_t))
{
    trace("schedule timer {} ms repeat {}", interval, repeat);
    return platform::startTimer(interval, repeat, [npp, function](uint32_t id) { function(npp, id); });
}

void npnUnscheduleTimer(NPP, uint32_t id)
{
    platform::stopTimer(id);
}

NPError npnPopUpContextMenu(NPP, void*)
{
    return NPERR_GENERIC_ERROR;
}

NPBool npnConvertPoint(NPP, double, double, int, double*, double*, int)
{
    return false;
}

NPBool npnHandleEvent(NPP, void*, NPBool)
{
    return false;
}

NPBool npnUnfocusInstance(NPP, int)
{
    return false;
}

void npnUrlRedirectResponse(NPP, void*, NPBool) {}

NPNetscapeFuncs browserFunctions()
{
    NPNetscapeFuncs functions {};
    functions.size = sizeof(functions);
    functions.version = (NP_VERSION_MAJOR << 8) | NP_VERSION_MINOR;
    functions.geturl = npnGetUrl;
    functions.posturl = npnPostUrl;
    functions.requestread = npnRequestRead;
    functions.newstream = npnNewStream;
    functions.write = npnWrite;
    functions.destroystream = npnDestroyStream;
    functions.status = npnStatus;
    functions.uagent = npnUserAgent;
    functions.memalloc = npnMemAlloc;
    functions.memfree = npnMemFree;
    functions.memflush = npnMemFlush;
    functions.reloadplugins = npnReloadPlugins;
    functions.getJavaEnv = npnGetJavaEnv;
    functions.getJavaPeer = npnGetJavaPeer;
    functions.geturlnotify = npnGetUrlNotify;
    functions.posturlnotify = npnPostUrlNotify;
    functions.getvalue = npnGetValue;
    functions.setvalue = npnSetValue;
    functions.invalidaterect = npnInvalidateRect;
    functions.invalidateregion = npnInvalidateRegion;
    functions.forceredraw = npnForceRedraw;
    functions.getstringidentifier = npnGetStringIdentifier;
    functions.getstringidentifiers = npnGetStringIdentifiers;
    functions.getintidentifier = npnGetIntIdentifier;
    functions.identifierisstring = npnIdentifierIsString;
    functions.utf8fromidentifier = npnUtf8FromIdentifier;
    functions.intfromidentifier = npnIntFromIdentifier;
    functions.createobject = npnCreateObject;
    functions.retainobject = npnRetainObject;
    functions.releaseobject = npnReleaseObject;
    functions.invoke = npnInvoke;
    functions.invokeDefault = npnInvokeDefault;
    functions.evaluate = npnEvaluate;
    functions.getproperty = npnGetProperty;
    functions.setproperty = npnSetProperty;
    functions.removeproperty = npnRemoveProperty;
    functions.hasproperty = npnHasProperty;
    functions.hasmethod = npnHasMethod;
    functions.releasevariantvalue = npnReleaseVariantValue;
    functions.setexception = npnSetException;
    functions.pushpopupsenabledstate = npnPushPopupsEnabledState;
    functions.poppopupsenabledstate = npnPopPopupsEnabledState;
    functions.enumerate = npnEnumerate;
    functions.pluginthreadasynccall = npnPluginThreadAsyncCall;
    functions.construct = npnConstruct;
    functions.getvalueforurl = npnGetValueForUrl;
    functions.setvalueforurl = npnSetValueForUrl;
    functions.getauthenticationinfo = npnGetAuthenticationInfo;
    functions.scheduletimer = npnScheduleTimer;
    functions.unscheduletimer = npnUnscheduleTimer;
    functions.popupcontextmenu = npnPopUpContextMenu;
    functions.convertpoint = npnConvertPoint;
    functions.handleevent = npnHandleEvent;
    functions.unfocusinstance = npnUnfocusInstance;
    functions.urlredirectresponse = npnUrlRedirectResponse;
    return functions;
}

}

struct Host::Stream {
    NPStream np {};
    std::string url;
    std::string mime;
    std::string headers;
    std::string file;
    std::vector<char> body;
    size_t offset = 0;
    uint16_t type = NP_NORMAL;
    bool notify = false;
    void* notifyData = nullptr;
    bool done = false;
};

Host::Host(HostOptions options) : options(std::move(options)) {}

bool Host::start(NP_GetEntryPointsFunc getEntryPoints, NP_InitializeFunc initialize, void* handle, uint32_t width, uint32_t height)
{
    active = this;
    browser = browserFunctions();
    plugin.size = sizeof(plugin);

#if defined(__APPLE__)
    NPError error = initialize(&browser);
    if (error == NPERR_NO_ERROR)
        error = getEntryPoints(&plugin);
#else
    NPError error = getEntryPoints(&plugin);
    if (error == NPERR_NO_ERROR)
        error = initialize(&browser);
#endif
    if (error != NPERR_NO_ERROR) {
        trace("plugin failed to initialize: {}", error);
        return false;
    }

    argumentNames = { "src", "type", "width", "height" };
    argumentValues = { options.source, UnityMime, std::to_string(width), std::to_string(height) };
    for (const auto& [name, value] : options.params) {
        argumentNames.push_back(name);
        argumentValues.push_back(value);
    }
    if (std::find(argumentNames.begin(), argumentNames.end(), "disableContextMenu") == argumentNames.end()) {
        argumentNames.push_back("disableContextMenu");
        argumentValues.push_back("true");
    }
    for (size_t i = 0; i < argumentNames.size(); i++) {
        argn.push_back(argumentNames[i].data());
        argv.push_back(argumentValues[i].data());
    }

    createPage();
    running = true;

    trace("starting {}", options.source);
    error = plugin.newp(mime.data(), &instance, NP_EMBED, static_cast<int16_t>(argn.size()), argn.data(), argv.data(), nullptr);
    if (error != NPERR_NO_ERROR) {
        trace("plugin refused the content: {}", error);
        running = false;
        return false;
    }

    window.window = handle;
    resize(width, height);
    open(options.source, std::nullopt, false, nullptr);
    return true;
}

void Host::resize(uint32_t width, uint32_t height)
{
    window.x = 0;
    window.y = 0;
    window.width = width;
    window.height = height;
    window.clipRect = { 0, 0, static_cast<uint16_t>(height), static_cast<uint16_t>(width) };
    window.type = NPWindowTypeWindow;
    if (running)
        plugin.setwindow(&instance, &window);
}

void Host::stop(NP_ShutdownFunc shutdown)
{
    if (running) {
        running = false;
        for (auto& stream : streams)
            stream->done = true;
        streams.clear();
        plugin.destroy(&instance, nullptr);
    }
    platform::stopTimers();
    if (shutdown)
        shutdown();
}

bool Host::requestExit()
{
    if (!running || options.exitMessage.empty())
        return false;
    sendMessage(options.bridge, options.exitMessage, "");
    return true;
}

bool Host::wasCalled(const std::string& name) const
{
    return called.contains(name);
}

void Host::fetchReply(const std::string& name, std::function<void(std::string)> done)
{
    auto reply = lookup(options.replies, name);
    if (reply == options.replies.end())
        return;

    const std::string& data = reply->second;
    if (!data.starts_with("http://") && !data.starts_with("https://")) {
        platform::post([done, data] { done(data); });
        return;
    }
    platform::download(data, std::nullopt, [done, data](platform::Response response) {
        if (!response.ok) {
            trace("could not fetch {}", data);
            return;
        }
        done(std::string(response.body.begin(), response.body.end()));
    });
}

const char* Host::userAgent() const
{
    return options.userAgent.c_str();
}

void Host::createPage()
{
    UrlParts parts = splitUrl(options.page);

    ScriptObject* location = newScriptObject(&instance);
    location->properties["href"] = Value::from(options.page);
    location->properties["protocol"] = Value::from(parts.protocol);
    location->properties["host"] = Value::from(parts.host);
    location->properties["hostname"] = Value::from(parts.host.substr(0, parts.host.find(':')));
    location->properties["pathname"] = Value::from(parts.path);
    location->properties["search"] = Value::from(parts.query);
    location->properties["origin"] = Value::from(originOf(options.page));
    location->call = [href = options.page](const std::string& name, const std::vector<Value>&, Value& result) {
        if (name != "toString")
            return false;
        result = Value::from(href);
        return true;
    };

    ScriptObject* document = newScriptObject(&instance);
    document->properties["location"] = Value::from(location);
    document->properties["URL"] = Value::from(options.page);
    document->properties["domain"] = Value::from(parts.host);
    document->properties["referrer"] = Value::from(std::string());
    document->properties["title"] = Value::from(std::string("OpenKogama"));

    std::string userAgent = options.userAgent;
    ScriptObject* navigator = newScriptObject(&instance);
    navigator->properties["userAgent"] = Value::from(userAgent);
    navigator->properties["appName"] = Value::from(std::string("Netscape"));
    navigator->properties["appVersion"] = Value::from(userAgent.substr(userAgent.find('/') + 1));
    navigator->properties["platform"] = Value::from(std::string(platform::platformName()));
    navigator->properties["language"] = Value::from(std::string("en-US"));

    ScriptObject* root = newScriptObject(&instance);
    for (const char* name : { "window", "self", "top", "parent" })
        root->properties[name] = Value::from(static_cast<NPObject*>(root));
    root->properties["location"] = Value::from(location);
    root->properties["document"] = Value::from(document);
    root->properties["navigator"] = Value::from(navigator);
    root->call = [this](const std::string& name, const std::vector<Value>& args, Value& result) {
        return pageCall(name, args, result);
    };

    ScriptObject* embed = newScriptObject(&instance);
    for (size_t i = 0; i < argumentNames.size(); i++)
        embed->properties[argumentNames[i]] = Value::from(argumentValues[i]);

    releaseObject(location);
    releaseObject(document);
    releaseObject(navigator);
    page = ObjectRef(root, true);
    element = ObjectRef(embed, true);
}

bool Host::pageCall(const std::string& name, const std::vector<Value>& args, Value& result)
{
    std::string described;
    for (const Value& arg : args)
        described += (described.empty() ? "" : ", ") + arg.describe();
    trace("page call {}({})", name, described);
    result = Value();
    called.insert(name.starts_with("UNITY_") ? name.substr(6) : name);

    bool handled = false;
    if (auto reply = lookup(options.replies, name); reply != options.replies.end()) {
        std::smatch match;
        static const std::regex callbackId(R"("callbackId"\s*:\s*(-?\d+))");
        std::string first = args.empty() ? std::string() : args[0].text;
        if (std::regex_search(first, match, callbackId)) {
            std::string id = match[1].str();
            const std::string& data = reply->second;
            if (data.starts_with("http://") || data.starts_with("https://")) {
                platform::download(data, std::nullopt, [this, id, data](platform::Response response) {
                    if (!response.ok) {
                        trace("could not fetch {}", data);
                        return;
                    }
                    sendMessage(options.bridge, "ExternalCallback", "{\"callbackId\":" + id + ",\"data\":" + std::string(response.body.begin(), response.body.end()) + "}");
                });
            } else {
                std::string package = "{\"callbackId\":" + id + ",\"data\":" + data + "}";
                platform::post([this, package] { sendMessage(options.bridge, "ExternalCallback", package); });
            }
            handled = true;
        } else {
            trace("{} was called without a callbackId", name);
        }
    }

    if (auto send = lookup(options.sends, name); send != options.sends.end()) {
        auto [method, value] = send->second;
        platform::post([this, method, value] { sendMessage(options.bridge, method, value); });
        handled = true;
    }

    if (!handled)
        trace("page call {} is not handled", name);
    return true;
}

void Host::sendMessage(const std::string& object, const std::string& method, const std::string& value)
{
    if (!running)
        return;

    trace("SendMessage({}, {}, {})", object, method, shorten(value));
    NPObject* scriptable = nullptr;
    if (!plugin.getvalue || plugin.getvalue(&instance, NPPVpluginScriptableNPObject, &scriptable) != NPERR_NO_ERROR || !scriptable) {
        trace("plugin has no scriptable object");
        return;
    }

    ObjectRef owner(scriptable, true);
    Value result;
    if (!invokeMethod(scriptable, "SendMessage", { Value::from(object), Value::from(method), Value::from(value) }, result))
        trace("SendMessage to {} failed", object);
}

NPError Host::getUrl(const char* url, const char* target, bool notify, void* notifyData)
{
    std::string address = url ? url : "";
    trace("plugin get {}{}", shorten(address), target && *target ? std::string(" target ") + target : "");

    if (address.starts_with("javascript:")) {
        std::string script = decodeUrl(address.substr(11));
        platform::post([this, script, address, notify, notifyData] {
            NPVariant result {};
            if (evaluate(page.get(), script, &result))
                releaseVariant(result);
            notifyDone(address, NPRES_DONE, notify, notifyData);
        });
        return NPERR_NO_ERROR;
    }

    if (target && *target) {
        trace("navigation to {} ignored", address);
        platform::post([this, address, notify, notifyData] { notifyDone(address, NPRES_DONE, notify, notifyData); });
        return NPERR_NO_ERROR;
    }

    open(address, std::nullopt, notify, notifyData);
    return NPERR_NO_ERROR;
}

NPError Host::postUrl(const char* url, const char* target, std::string data, bool file, bool notify, void* notifyData)
{
    std::string address = url ? url : "";
    trace("plugin post {} ({} bytes)", shorten(address), data.size());

    if (file) {
        std::ifstream in(pathFromUtf8(isFileUrl(data) ? fileUrlToPath(data) : data), std::ios::binary);
        if (!in)
            return NPERR_GENERIC_ERROR;
        data.assign(std::istreambuf_iterator<char>(in), {});
    }

    if (target && *target) {
        trace("post to {} with target ignored", address);
        platform::post([this, address, notify, notifyData] { notifyDone(address, NPRES_DONE, notify, notifyData); });
        return NPERR_NO_ERROR;
    }

    open(address, std::move(data), notify, notifyData);
    return NPERR_NO_ERROR;
}

NPError Host::getValue(NPNVariable variable, void* value)
{
    switch (variable) {
    case NPNVnetscapeWindow:
        *static_cast<void**>(value) = platform::nativeWindow();
        return NPERR_NO_ERROR;
    case NPNVjavascriptEnabledBool:
        *static_cast<NPBool*>(value) = true;
        return NPERR_NO_ERROR;
    case NPNVasdEnabledBool:
    case NPNVisOfflineBool:
    case NPNVprivateModeBool:
    case NPNVSupportsXEmbedBool:
    case NPNVSupportsWindowless:
        *static_cast<NPBool*>(value) = false;
        return NPERR_NO_ERROR;
    case NPNVWindowNPObject:
        *static_cast<NPObject**>(value) = retainObject(page.get());
        return NPERR_NO_ERROR;
    case NPNVPluginElementNPObject:
        *static_cast<NPObject**>(value) = retainObject(element.get());
        return NPERR_NO_ERROR;
    case NPNVdocumentOrigin:
        *static_cast<char**>(value) = duplicate(originOf(options.page));
        return NPERR_NO_ERROR;
    default:
        trace("plugin asked for unknown value {}", variable);
        return NPERR_GENERIC_ERROR;
    }
}

bool Host::evaluate(NPObject* scope, const std::string& script, NPVariant* result)
{
    trace("eval {}", shorten(script));
    std::optional<Value> value = runScript(scope ? scope : page.get(), script);
    if (!value) {
        trace("eval not understood: {}", shorten(script));
        result->type = NPVariantType_Void;
        return false;
    }
    *result = value->toVariant();
    return true;
}

void Host::open(const std::string& target, std::optional<std::string> post, bool notify, void* notifyData)
{
    auto stream = std::make_shared<Stream>();
    stream->url = resolveUrl(options.page, target);
    stream->notify = notify;
    stream->notifyData = notifyData;
    trace("load {}", shorten(stream->url));

    bool mounted = !options.localSource.empty() && stripQuery(stream->url) == stripQuery(options.source);
    if (mounted || isFileUrl(stream->url)) {
        std::string local = mounted ? options.localSource : stream->url;
        platform::post([this, stream, local] {
            platform::Response response;
            response.file = fileUrlToPath(local);
            std::ifstream in(pathFromUtf8(response.file), std::ios::binary);
            if (in) {
                response.body.assign(std::istreambuf_iterator<char>(in), {});
                response.mime = mimeFor(stream->url);
                response.ok = true;
            }
            deliver(stream, std::move(response));
        });
        return;
    }

    std::string server = splitUrl(stream->url).host;
    if (server == "unity3d.com" || server.ends_with(".unity3d.com")) {
        trace("blocked {}", server);
        platform::post([this, stream] { deliver(stream, {}); });
        return;
    }

    platform::download(stream->url, std::move(post), [this, stream](platform::Response response) {
        deliver(stream, std::move(response));
    });
}

void Host::deliver(std::shared_ptr<Stream> stream, platform::Response response)
{
    if (!running)
        return;
    if (!response.ok) {
        trace("load failed {}", shorten(stream->url));
        notifyDone(stream->url, NPRES_NETWORK_ERR, stream->notify, stream->notifyData);
        return;
    }

    stream->mime = response.mime.empty() ? mimeFor(stream->url) : response.mime;
    stream->headers = std::move(response.headers);
    stream->body = std::move(response.body);
    stream->file = std::move(response.file);
    stream->np.url = stream->url.c_str();
    stream->np.end = static_cast<uint32_t>(stream->body.size());
    stream->np.notifyData = stream->notifyData;
    stream->np.headers = stream->headers.empty() ? nullptr : stream->headers.c_str();

    uint16_t type = NP_NORMAL;
    NPError error = plugin.newstream(&instance, stream->mime.data(), &stream->np, false, &type);
    if (error != NPERR_NO_ERROR) {
        trace("plugin refused stream {}: {}", shorten(stream->url), error);
        notifyDone(stream->url, NPRES_NETWORK_ERR, stream->notify, stream->notifyData);
        return;
    }

    stream->type = type;
    trace("stream {} type {} size {}", shorten(stream->url), type, stream->body.size());
    streams.push_back(stream);
    pump(stream);
}

void Host::pump(std::shared_ptr<Stream> stream)
{
    if (stream->done || !running)
        return;

    if (stream->type != NP_ASFILEONLY) {
        int32_t budget = 4 * ChunkSize;
        while (stream->offset < stream->body.size() && budget > 0) {
            int32_t ready = plugin.writeready(&instance, &stream->np);
            if (stream->done)
                return;
            if (ready <= 0) {
                platform::startTimer(10, false, [this, stream](uint32_t) { pump(stream); });
                return;
            }

            int32_t remaining = static_cast<int32_t>(stream->body.size() - stream->offset);
            int32_t length = std::min({ ready, ChunkSize, remaining });
            int32_t written = plugin.write(&instance, &stream->np, static_cast<int32_t>(stream->offset), length, stream->body.data() + stream->offset);
            if (stream->done)
                return;
            if (written < 0) {
                finish(stream, NPRES_NETWORK_ERR);
                return;
            }
            if (written == 0) {
                platform::startTimer(10, false, [this, stream](uint32_t) { pump(stream); });
                return;
            }
            stream->offset += written;
            budget -= written;
        }

        if (stream->offset < stream->body.size()) {
            platform::post([this, stream] { pump(stream); });
            return;
        }
    }

    if (stream->type == NP_ASFILE || stream->type == NP_ASFILEONLY) {
        if (stream->file.empty()) {
            stream->file = platform::temporaryPath("openkogama-" + std::to_string(reinterpret_cast<uintptr_t>(stream.get())));
            std::ofstream out(pathFromUtf8(stream->file), std::ios::binary);
            out.write(stream->body.data(), static_cast<std::streamsize>(stream->body.size()));
        }
        std::string path = platform::nativePath(stream->file);
        plugin.asfile(&instance, &stream->np, path.c_str());
    }

    finish(stream, NPRES_DONE);
}

void Host::finish(std::shared_ptr<Stream> stream, NPReason reason)
{
    if (stream->done)
        return;
    stream->done = true;
    trace("stream {} finished {} at {}", shorten(stream->url), reason, stream->offset);
    plugin.destroystream(&instance, &stream->np, reason);
    notifyDone(stream->url, reason, stream->notify, stream->notifyData);
    std::erase(streams, stream);
}

void Host::cancel(NPStream* np, NPReason reason)
{
    auto it = std::find_if(streams.begin(), streams.end(), [np](const auto& stream) { return &stream->np == np; });
    if (it != streams.end())
        finish(*it, reason);
}

void Host::notifyDone(const std::string& url, NPReason reason, bool notify, void* notifyData)
{
    if (running && notify && plugin.urlnotify)
        plugin.urlnotify(&instance, url.c_str(), reason, notifyData);
}
