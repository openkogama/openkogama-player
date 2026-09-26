#pragma once

#include <cstdint>

#if defined(_WIN32)
#define NP_CALLBACK __stdcall
#else
#define NP_CALLBACK
#endif

using NPError = int16_t;
using NPReason = int16_t;
using NPBool = unsigned char;
using NPMIMEType = char*;
using NPUTF8 = char;
using NPIdentifier = void*;

constexpr NPError NPERR_NO_ERROR = 0;
constexpr NPError NPERR_GENERIC_ERROR = 1;
constexpr NPError NPERR_INVALID_INSTANCE_ERROR = 2;
constexpr NPError NPERR_INVALID_PARAM = 9;
constexpr NPError NPERR_INVALID_URL = 10;

constexpr NPReason NPRES_DONE = 0;
constexpr NPReason NPRES_NETWORK_ERR = 1;
constexpr NPReason NPRES_USER_BREAK = 2;

constexpr uint16_t NP_EMBED = 1;
constexpr uint16_t NP_NORMAL = 1;
constexpr uint16_t NP_SEEK = 2;
constexpr uint16_t NP_ASFILE = 3;
constexpr uint16_t NP_ASFILEONLY = 4;

constexpr uint16_t NP_VERSION_MAJOR = 0;
constexpr uint16_t NP_VERSION_MINOR = 27;

using NPNVariable = int;
constexpr NPNVariable NPNVnetscapeWindow = 3;
constexpr NPNVariable NPNVjavascriptEnabledBool = 4;
constexpr NPNVariable NPNVasdEnabledBool = 5;
constexpr NPNVariable NPNVisOfflineBool = 6;
constexpr NPNVariable NPNVToolkit = 13;
constexpr NPNVariable NPNVSupportsXEmbedBool = 14;
constexpr NPNVariable NPNVWindowNPObject = 15;
constexpr NPNVariable NPNVPluginElementNPObject = 16;
constexpr NPNVariable NPNVSupportsWindowless = 17;
constexpr NPNVariable NPNVprivateModeBool = 18;
constexpr NPNVariable NPNVdocumentOrigin = 22;

using NPPVariable = int;
constexpr NPPVariable NPPVpluginNameString = 1;
constexpr NPPVariable NPPVpluginDescriptionString = 2;
constexpr NPPVariable NPPVpluginWindowBool = 3;
constexpr NPPVariable NPPVpluginTransparentBool = 4;
constexpr NPPVariable NPPVpluginScriptableNPObject = 15;

struct NPP_t {
    void* pdata;
    void* ndata;
};
using NPP = NPP_t*;

struct NPRect {
    uint16_t top;
    uint16_t left;
    uint16_t bottom;
    uint16_t right;
};

struct NPSavedData {
    int32_t len;
    void* buf;
};

struct NPByteRange {
    int32_t offset;
    uint32_t length;
    NPByteRange* next;
};

struct NPStream {
    void* pdata;
    void* ndata;
    const char* url;
    uint32_t end;
    uint32_t lastmodified;
    void* notifyData;
    const char* headers;
};

enum NPWindowType {
    NPWindowTypeWindow = 1,
    NPWindowTypeDrawable
};

struct NPWindow {
    void* window;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    NPRect clipRect;
#if !defined(_WIN32) && !defined(__APPLE__)
    void* ws_info;
#endif
    NPWindowType type;
};

struct NPString {
    const NPUTF8* UTF8Characters;
    uint32_t UTF8Length;
};

enum NPVariantType {
    NPVariantType_Void,
    NPVariantType_Null,
    NPVariantType_Bool,
    NPVariantType_Int32,
    NPVariantType_Double,
    NPVariantType_String,
    NPVariantType_Object
};

struct NPObject;

struct NPVariant {
    NPVariantType type;
    union {
        bool boolValue;
        int32_t intValue;
        double doubleValue;
        NPString stringValue;
        NPObject* objectValue;
    } value;
};

struct NPClass {
    uint32_t structVersion;
    NPObject* (*allocate)(NPP, NPClass*);
    void (*deallocate)(NPObject*);
    void (*invalidate)(NPObject*);
    bool (*hasMethod)(NPObject*, NPIdentifier);
    bool (*invoke)(NPObject*, NPIdentifier, const NPVariant*, uint32_t, NPVariant*);
    bool (*invokeDefault)(NPObject*, const NPVariant*, uint32_t, NPVariant*);
    bool (*hasProperty)(NPObject*, NPIdentifier);
    bool (*getProperty)(NPObject*, NPIdentifier, NPVariant*);
    bool (*setProperty)(NPObject*, NPIdentifier, const NPVariant*);
    bool (*removeProperty)(NPObject*, NPIdentifier);
    bool (*enumerate)(NPObject*, NPIdentifier**, uint32_t*);
    bool (*construct)(NPObject*, const NPVariant*, uint32_t, NPVariant*);
};

struct NPObject {
    NPClass* _class;
    uint32_t referenceCount;
};

struct NPNetscapeFuncs {
    uint16_t size;
    uint16_t version;
    NPError (*geturl)(NPP, const char*, const char*);
    NPError (*posturl)(NPP, const char*, const char*, uint32_t, const char*, NPBool);
    NPError (*requestread)(NPStream*, NPByteRange*);
    NPError (*newstream)(NPP, NPMIMEType, const char*, NPStream**);
    int32_t (*write)(NPP, NPStream*, int32_t, void*);
    NPError (*destroystream)(NPP, NPStream*, NPReason);
    void (*status)(NPP, const char*);
    const char* (*uagent)(NPP);
    void* (*memalloc)(uint32_t);
    void (*memfree)(void*);
    uint32_t (*memflush)(uint32_t);
    void (*reloadplugins)(NPBool);
    void* (*getJavaEnv)();
    void* (*getJavaPeer)(NPP);
    NPError (*geturlnotify)(NPP, const char*, const char*, void*);
    NPError (*posturlnotify)(NPP, const char*, const char*, uint32_t, const char*, NPBool, void*);
    NPError (*getvalue)(NPP, NPNVariable, void*);
    NPError (*setvalue)(NPP, NPPVariable, void*);
    void (*invalidaterect)(NPP, NPRect*);
    void (*invalidateregion)(NPP, void*);
    void (*forceredraw)(NPP);
    NPIdentifier (*getstringidentifier)(const NPUTF8*);
    void (*getstringidentifiers)(const NPUTF8**, int32_t, NPIdentifier*);
    NPIdentifier (*getintidentifier)(int32_t);
    bool (*identifierisstring)(NPIdentifier);
    NPUTF8* (*utf8fromidentifier)(NPIdentifier);
    int32_t (*intfromidentifier)(NPIdentifier);
    NPObject* (*createobject)(NPP, NPClass*);
    NPObject* (*retainobject)(NPObject*);
    void (*releaseobject)(NPObject*);
    bool (*invoke)(NPP, NPObject*, NPIdentifier, const NPVariant*, uint32_t, NPVariant*);
    bool (*invokeDefault)(NPP, NPObject*, const NPVariant*, uint32_t, NPVariant*);
    bool (*evaluate)(NPP, NPObject*, NPString*, NPVariant*);
    bool (*getproperty)(NPP, NPObject*, NPIdentifier, NPVariant*);
    bool (*setproperty)(NPP, NPObject*, NPIdentifier, const NPVariant*);
    bool (*removeproperty)(NPP, NPObject*, NPIdentifier);
    bool (*hasproperty)(NPP, NPObject*, NPIdentifier);
    bool (*hasmethod)(NPP, NPObject*, NPIdentifier);
    void (*releasevariantvalue)(NPVariant*);
    void (*setexception)(NPObject*, const NPUTF8*);
    void (*pushpopupsenabledstate)(NPP, NPBool);
    void (*poppopupsenabledstate)(NPP);
    bool (*enumerate)(NPP, NPObject*, NPIdentifier**, uint32_t*);
    void (*pluginthreadasynccall)(NPP, void (*)(void*), void*);
    bool (*construct)(NPP, NPObject*, const NPVariant*, uint32_t, NPVariant*);
    NPError (*getvalueforurl)(NPP, int, const char*, char**, uint32_t*);
    NPError (*setvalueforurl)(NPP, int, const char*, const char*, uint32_t);
    NPError (*getauthenticationinfo)(NPP, const char*, const char*, int32_t, const char*, const char*, char**, uint32_t*, char**, uint32_t*);
    uint32_t (*scheduletimer)(NPP, uint32_t, NPBool, void (*)(NPP, uint32_t));
    void (*unscheduletimer)(NPP, uint32_t);
    NPError (*popupcontextmenu)(NPP, void*);
    NPBool (*convertpoint)(NPP, double, double, int, double*, double*, int);
    NPBool (*handleevent)(NPP, void*, NPBool);
    NPBool (*unfocusinstance)(NPP, int);
    void (*urlredirectresponse)(NPP, void*, NPBool);
    void* initasyncsurface;
    void* finalizeasyncsurface;
    void* setcurrentasyncsurface;
};

struct NPPluginFuncs {
    uint16_t size;
    uint16_t version;
    NPError (*newp)(NPMIMEType, NPP, uint16_t, int16_t, char**, char**, NPSavedData*);
    NPError (*destroy)(NPP, NPSavedData**);
    NPError (*setwindow)(NPP, NPWindow*);
    NPError (*newstream)(NPP, NPMIMEType, NPStream*, NPBool, uint16_t*);
    NPError (*destroystream)(NPP, NPStream*, NPReason);
    void (*asfile)(NPP, NPStream*, const char*);
    int32_t (*writeready)(NPP, NPStream*);
    int32_t (*write)(NPP, NPStream*, int32_t, int32_t, void*);
    void (*print)(NPP, void*);
    int16_t (*event)(NPP, void*);
    void (*urlnotify)(NPP, const char*, NPReason, void*);
    void* javaClass;
    NPError (*getvalue)(NPP, NPPVariable, void*);
    NPError (*setvalue)(NPP, NPNVariable, void*);
    NPBool (*gotfocus)(NPP, int);
    void (*lostfocus)(NPP);
    void (*urlredirectnotify)(NPP, const char*, int32_t, void*);
    NPError (*clearsitedata)(const char*, uint64_t, uint64_t);
    char** (*getsitesforclearing)();
    void (*didComposite)(NPP);
};

using NP_GetEntryPointsFunc = NPError(NP_CALLBACK*)(NPPluginFuncs*);
using NP_InitializeFunc = NPError(NP_CALLBACK*)(NPNetscapeFuncs*);
using NP_ShutdownFunc = NPError(NP_CALLBACK*)();
