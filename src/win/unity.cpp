#include "unity.h"
#include "../log.h"

#include <windows.h>

namespace {

using DomainGetById = void* (*)(int);
using ThreadAttach = void* (*)(void*);
using DomainSet = int (*)(void*, int);
using AssemblyForeach = void (*)(void (*)(void*, void*), void*);
using AssemblyGetImage = void* (*)(void*);
using ImageGetName = const char* (*)(void*);
using ClassFromName = void* (*)(void*, const char*, const char*);
using ClassGetPropertyFromName = void* (*)(void*, const char*);
using PropertyGetGetMethod = void* (*)(void*);
using ClassGetMethodFromName = void* (*)(void*, const char*, int);
using RuntimeInvoke = void* (*)(void*, void*, void**, void**);
using ObjectUnbox = void* (*)(void*);
using StringToUtf8 = char* (*)(void*);

struct Api {
    DomainGetById domainGetById;
    ThreadAttach threadAttach;
    DomainSet domainSet;
    AssemblyForeach assemblyForeach;
    AssemblyGetImage assemblyGetImage;
    ImageGetName imageGetName;
    ClassFromName classFromName;
    ClassGetPropertyFromName classGetPropertyFromName;
    PropertyGetGetMethod propertyGetGetMethod;
    ClassGetMethodFromName classGetMethodFromName;
    RuntimeInvoke runtimeInvoke;
    ObjectUnbox objectUnbox;
    StringToUtf8 stringToUtf8;
};

Api api {};

template <class T>
bool bind(HMODULE module, const char* name, T& target)
{
    target = reinterpret_cast<T>(GetProcAddress(module, name));
    return target != nullptr;
}

bool loadApi()
{
    HMODULE module = GetModuleHandleW(L"mono-1-vc.dll");
    return module
        && bind(module, "mono_domain_get_by_id", api.domainGetById)
        && bind(module, "mono_thread_attach", api.threadAttach)
        && bind(module, "mono_domain_set", api.domainSet)
        && bind(module, "mono_assembly_foreach", api.assemblyForeach)
        && bind(module, "mono_assembly_get_image", api.assemblyGetImage)
        && bind(module, "mono_image_get_name", api.imageGetName)
        && bind(module, "mono_class_from_name", api.classFromName)
        && bind(module, "mono_class_get_property_from_name", api.classGetPropertyFromName)
        && bind(module, "mono_property_get_get_method", api.propertyGetGetMethod)
        && bind(module, "mono_class_get_method_from_name", api.classGetMethodFromName)
        && bind(module, "mono_runtime_invoke", api.runtimeInvoke)
        && bind(module, "mono_object_unbox", api.objectUnbox)
        && bind(module, "mono_string_to_utf8", api.stringToUtf8);
}

struct Images {
    void* game = nullptr;
    void* engine = nullptr;
};

std::optional<int> integer(void* boxed)
{
    if (!boxed)
        return std::nullopt;
    return *static_cast<int*>(api.objectUnbox(boxed));
}

}

bool UnityBridge::attach()
{
    if (failed)
        return false;
    if (!loaded) {
        if (!GetModuleHandleW(L"mono-1-vc.dll"))
            return false;
        if (!loadApi()) {
            trace("unity: mono api is incomplete");
            failed = true;
            return false;
        }
        loaded = true;
    }

    void* current = nullptr;
    for (int id = 1; id < 32; id++)
        if (void* candidate = api.domainGetById(id))
            current = candidate;
    if (!current)
        return false;

    if (current != domain) {
        domain = current;
        gameImage = nullptr;
        engineImage = nullptr;
    }
    api.threadAttach(domain);
    api.domainSet(domain, 0);

    if (!gameImage || !engineImage) {
        Images images;
        api.assemblyForeach([](void* assembly, void* user) {
            auto* found = static_cast<Images*>(user);
            void* image = api.assemblyGetImage(assembly);
            std::string name = image ? api.imageGetName(image) : "";
            if (name == "Assembly-CSharp")
                found->game = image;
            else if (name == "UnityEngine")
                found->engine = image;
        }, &images);
        gameImage = images.game;
        engineImage = images.engine;
    }
    return gameImage && engineImage;
}

void* UnityBridge::property(void* image, const char* ns, const char* cls, const char* name, void* target)
{
    void* klass = api.classFromName(image, ns, cls);
    void* prop = klass ? api.classGetPropertyFromName(klass, name) : nullptr;
    void* getter = prop ? api.propertyGetGetMethod(prop) : nullptr;
    if (!getter)
        return nullptr;

    void* exception = nullptr;
    void* result = api.runtimeInvoke(getter, target, nullptr, &exception);
    return exception ? nullptr : result;
}

void* UnityBridge::game()
{
    void* controller = property(gameImage, "", "MVGameController", "Instance", nullptr);
    return controller ? property(gameImage, "", "MVGameController", "Game", controller) : nullptr;
}

std::optional<GameState> UnityBridge::read()
{
    if (!attach())
        return std::nullopt;

    GameState state;
    if (void* loading = property(engineImage, "UnityEngine", "Application", "isLoadingLevel", nullptr))
        state.loadingLevel = *static_cast<bool*>(api.objectUnbox(loading));
    if (void* level = property(engineImage, "UnityEngine", "Application", "loadedLevelName", nullptr)) {
        char* name = api.stringToUtf8(level);
        state.level = name ? name : "";
    }
    state.frame = integer(property(engineImage, "UnityEngine", "Time", "frameCount", nullptr)).value_or(0);

    if (void* network = game()) {
        state.joinState = integer(property(gameImage, "", "MVNetworkGame", "JoinState", network));
        state.connectionState = integer(property(gameImage, "", "MVNetworkGame", "ConnState", network));
    }
    return state;
}

bool UnityBridge::callGame(const std::string& method)
{
    if (!attach())
        return false;

    void* network = game();
    void* klass = api.classFromName(gameImage, "", "MVNetworkGame");
    void* target = klass ? api.classGetMethodFromName(klass, method.c_str(), 0) : nullptr;
    if (!network || !target) {
        trace("unity: MVNetworkGame.{} not found", method);
        return false;
    }

    void* exception = nullptr;
    api.runtimeInvoke(target, network, nullptr, &exception);
    if (exception)
        trace("unity: MVNetworkGame.{} threw", method);
    return !exception;
}

void LevelLoadWatcher::tick(UnityBridge& unity)
{
    std::optional<GameState> state = unity.read();
    if (!state)
        return;

    if (state->loadingLevel) {
        wasLoading = true;
        finishedAt = 0;
        return;
    }

    unsigned long now = GetTickCount();
    if (wasLoading) {
        wasLoading = false;
        finishedAt = now;
        stateAtFinish = state->joinState;
        return;
    }

    if (!finishedAt || now - finishedAt < 500)
        return;
    finishedAt = 0;

    if (!stateAtFinish || state->joinState != stateAtFinish)
        return;
    auto rule = rules.find(*stateAtFinish);
    if (rule == rules.end())
        return;

    trace("unity: level {} finished loading but the game is still in state {}, calling {}", state->level, *stateAtFinish, rule->second);
    unity.callGame(rule->second);
}
