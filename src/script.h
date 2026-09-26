#pragma once

#include "npapi.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

NPIdentifier identifier(std::string_view name);
NPIdentifier intIdentifier(int32_t number);
bool identifierIsString(NPIdentifier id);
std::string identifierName(NPIdentifier id);
int32_t identifierInt(NPIdentifier id);

NPObject* createObject(NPP npp, NPClass* cls);
NPObject* retainObject(NPObject* object);
void releaseObject(NPObject* object);
void releaseVariant(NPVariant& variant);

class ObjectRef {
public:
    ObjectRef() = default;
    explicit ObjectRef(NPObject* object, bool adopt = false) : object(object)
    {
        if (object && !adopt)
            retainObject(object);
    }
    ObjectRef(const ObjectRef& other) : ObjectRef(other.object) {}
    ObjectRef(ObjectRef&& other) noexcept : object(std::exchange(other.object, nullptr)) {}
    ObjectRef& operator=(ObjectRef other) noexcept
    {
        std::swap(object, other.object);
        return *this;
    }
    ~ObjectRef()
    {
        if (object)
            releaseObject(object);
    }

    NPObject* get() const { return object; }

private:
    NPObject* object = nullptr;
};

struct Value {
    enum class Kind { Void, Null, Bool, Number, String, Object };

    Kind kind = Kind::Void;
    bool boolean = false;
    double number = 0;
    std::string text;
    ObjectRef object;

    static Value null();
    static Value from(bool value);
    static Value from(double value);
    static Value from(std::string value);
    static Value from(NPObject* value);
    static Value from(const NPVariant& variant);

    NPVariant toVariant() const;
    std::string describe() const;
};

struct ScriptObject : NPObject {
    std::map<std::string, Value> properties;
    std::function<bool(const std::string& name, const std::vector<Value>& args, Value& result)> call;
};

ScriptObject* newScriptObject(NPP npp);

bool readProperty(NPObject* object, std::string_view name, Value& result);
bool invokeMethod(NPObject* object, std::string_view name, const std::vector<Value>& args, Value& result);
std::optional<Value> runScript(NPObject* scope, std::string_view script);
