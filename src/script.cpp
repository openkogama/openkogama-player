#include "script.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace {

struct Identifier {
    bool isString;
    std::string name;
    int32_t number;
};

std::mutex identifiersLock;
std::unordered_map<std::string, std::unique_ptr<Identifier>> strings;
std::unordered_map<int32_t, std::unique_ptr<Identifier>> numbers;

Identifier* identifierOf(NPIdentifier id)
{
    return static_cast<Identifier*>(id);
}

ScriptObject* scriptOf(NPObject* object)
{
    return static_cast<ScriptObject*>(object);
}

NPClass scriptClass = {
    3,
    [](NPP, NPClass*) -> NPObject* { return new ScriptObject(); },
    [](NPObject* object) { delete scriptOf(object); },
    nullptr,
    [](NPObject* object, NPIdentifier) { return static_cast<bool>(scriptOf(object)->call); },
    [](NPObject* object, NPIdentifier name, const NPVariant* args, uint32_t count, NPVariant* result) {
        ScriptObject* script = scriptOf(object);
        if (!script->call)
            return false;

        std::vector<Value> values;
        for (uint32_t i = 0; i < count; i++)
            values.push_back(Value::from(args[i]));

        Value output;
        if (!script->call(identifierName(name), values, output))
            return false;
        *result = output.toVariant();
        return true;
    },
    [](NPObject*, const NPVariant*, uint32_t, NPVariant*) { return false; },
    [](NPObject* object, NPIdentifier name) { return scriptOf(object)->properties.contains(identifierName(name)); },
    [](NPObject* object, NPIdentifier name, NPVariant* result) {
        auto& properties = scriptOf(object)->properties;
        auto it = properties.find(identifierName(name));
        if (it == properties.end())
            return false;
        *result = it->second.toVariant();
        return true;
    },
    [](NPObject* object, NPIdentifier name, const NPVariant* value) {
        scriptOf(object)->properties[identifierName(name)] = Value::from(*value);
        return true;
    },
    [](NPObject* object, NPIdentifier name) {
        return scriptOf(object)->properties.erase(identifierName(name)) > 0;
    },
    [](NPObject* object, NPIdentifier** ids, uint32_t* count) {
        auto& properties = scriptOf(object)->properties;
        *count = static_cast<uint32_t>(properties.size());
        *ids = static_cast<NPIdentifier*>(std::malloc(sizeof(NPIdentifier) * (properties.empty() ? 1 : properties.size())));
        uint32_t i = 0;
        for (const auto& entry : properties)
            (*ids)[i++] = identifier(entry.first);
        return true;
    },
    [](NPObject*, const NPVariant*, uint32_t, NPVariant*) { return false; },
};

class Parser {
public:
    explicit Parser(std::string_view text) : text(text) {}

    void skip()
    {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
            pos++;
    }

    bool atEnd()
    {
        skip();
        return pos == text.size();
    }

    bool eat(char c)
    {
        skip();
        if (pos < text.size() && text[pos] == c) {
            pos++;
            return true;
        }
        return false;
    }

    bool name(std::string& out)
    {
        skip();
        size_t start = pos;
        while (pos < text.size() && (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_' || text[pos] == '$'))
            pos++;
        out = std::string(text.substr(start, pos - start));
        if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) {
            pos = start;
            return false;
        }
        return true;
    }

    bool literal(Value& out)
    {
        skip();
        if (pos >= text.size())
            return false;

        char c = text[pos];
        if (c == '"' || c == '\'') {
            std::string value;
            if (!quoted(c, value))
                return false;
            out = Value::from(std::move(value));
            return true;
        }

        if (c == '-' || c == '.' || std::isdigit(static_cast<unsigned char>(c))) {
            std::string rest(text.substr(pos));
            char* end = nullptr;
            double number = std::strtod(rest.c_str(), &end);
            if (end == rest.c_str())
                return false;
            pos += end - rest.c_str();
            out = Value::from(number);
            return true;
        }

        size_t start = pos;
        std::string word;
        if (!name(word))
            return false;
        if (word == "true" || word == "false")
            out = Value::from(word == "true");
        else if (word == "null")
            out = Value::null();
        else if (word == "undefined")
            out = Value();
        else {
            pos = start;
            return false;
        }
        return true;
    }

private:
    bool quoted(char quote, std::string& out)
    {
        pos++;
        while (pos < text.size()) {
            char c = text[pos++];
            if (c == quote)
                return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos >= text.size())
                return false;

            char escape = text[pos++];
            switch (escape) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'v': out += '\v'; break;
            case '0': out += '\0'; break;
            case 'x':
            case 'u': {
                size_t digits = escape == 'x' ? 2 : 4;
                if (pos + digits > text.size())
                    return false;
                uint32_t code = std::strtoul(std::string(text.substr(pos, digits)).c_str(), nullptr, 16);
                pos += digits;
                appendUtf8(out, code);
                break;
            }
            default: out += escape; break;
            }
        }
        return false;
    }

    static void appendUtf8(std::string& out, uint32_t code)
    {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    std::string_view text;
    size_t pos = 0;
};

std::optional<Value> runStatement(NPObject* scope, Parser& parser)
{
    Value literal;
    if (parser.literal(literal))
        return literal;

    Value current = Value::from(scope);
    do {
        std::string name;
        if (!parser.name(name))
            return std::nullopt;

        bool call = false;
        std::vector<Value> args;
        if (parser.eat('(')) {
            call = true;
            if (!parser.eat(')')) {
                do {
                    Value arg;
                    if (!parser.literal(arg))
                        return std::nullopt;
                    args.push_back(std::move(arg));
                } while (parser.eat(','));
                if (!parser.eat(')'))
                    return std::nullopt;
            }
        }

        if (current.kind != Value::Kind::Object || !current.object.get())
            return std::nullopt;

        Value next;
        bool found = call ? invokeMethod(current.object.get(), name, args, next) : readProperty(current.object.get(), name, next);
        if (!found)
            return std::nullopt;
        current = std::move(next);
    } while (parser.eat('.'));

    return current;
}

}

NPIdentifier identifier(std::string_view name)
{
    std::lock_guard guard(identifiersLock);
    auto& slot = strings[std::string(name)];
    if (!slot)
        slot = std::make_unique<Identifier>(Identifier { true, std::string(name), 0 });
    return slot.get();
}

NPIdentifier intIdentifier(int32_t number)
{
    std::lock_guard guard(identifiersLock);
    auto& slot = numbers[number];
    if (!slot)
        slot = std::make_unique<Identifier>(Identifier { false, {}, number });
    return slot.get();
}

bool identifierIsString(NPIdentifier id)
{
    return id && identifierOf(id)->isString;
}

std::string identifierName(NPIdentifier id)
{
    if (!id)
        return {};
    Identifier* entry = identifierOf(id);
    return entry->isString ? entry->name : std::to_string(entry->number);
}

int32_t identifierInt(NPIdentifier id)
{
    return id && !identifierOf(id)->isString ? identifierOf(id)->number : INT32_MIN;
}

NPObject* createObject(NPP npp, NPClass* cls)
{
    NPObject* object = cls->allocate ? cls->allocate(npp, cls) : static_cast<NPObject*>(std::malloc(sizeof(NPObject)));
    object->_class = cls;
    object->referenceCount = 1;
    return object;
}

NPObject* retainObject(NPObject* object)
{
    if (object)
        object->referenceCount++;
    return object;
}

void releaseObject(NPObject* object)
{
    if (!object || --object->referenceCount > 0)
        return;
    if (object->_class && object->_class->deallocate)
        object->_class->deallocate(object);
    else
        std::free(object);
}

void releaseVariant(NPVariant& variant)
{
    if (variant.type == NPVariantType_String)
        std::free(const_cast<NPUTF8*>(variant.value.stringValue.UTF8Characters));
    else if (variant.type == NPVariantType_Object)
        releaseObject(variant.value.objectValue);
    variant.type = NPVariantType_Void;
}

Value Value::null()
{
    Value value;
    value.kind = Kind::Null;
    return value;
}

Value Value::from(bool boolean)
{
    Value value;
    value.kind = Kind::Bool;
    value.boolean = boolean;
    return value;
}

Value Value::from(double number)
{
    Value value;
    value.kind = Kind::Number;
    value.number = number;
    return value;
}

Value Value::from(std::string text)
{
    Value value;
    value.kind = Kind::String;
    value.text = std::move(text);
    return value;
}

Value Value::from(NPObject* object)
{
    Value value;
    value.kind = object ? Kind::Object : Kind::Null;
    value.object = ObjectRef(object);
    return value;
}

Value Value::from(const NPVariant& variant)
{
    switch (variant.type) {
    case NPVariantType_Null: return null();
    case NPVariantType_Bool: return from(variant.value.boolValue);
    case NPVariantType_Int32: return from(static_cast<double>(variant.value.intValue));
    case NPVariantType_Double: return from(variant.value.doubleValue);
    case NPVariantType_String: return from(std::string(variant.value.stringValue.UTF8Characters, variant.value.stringValue.UTF8Length));
    case NPVariantType_Object: return from(variant.value.objectValue);
    default: return {};
    }
}

NPVariant Value::toVariant() const
{
    NPVariant variant {};
    switch (kind) {
    case Kind::Void:
        variant.type = NPVariantType_Void;
        break;
    case Kind::Null:
        variant.type = NPVariantType_Null;
        break;
    case Kind::Bool:
        variant.type = NPVariantType_Bool;
        variant.value.boolValue = boolean;
        break;
    case Kind::Number:
        if (number == static_cast<int32_t>(number)) {
            variant.type = NPVariantType_Int32;
            variant.value.intValue = static_cast<int32_t>(number);
        } else {
            variant.type = NPVariantType_Double;
            variant.value.doubleValue = number;
        }
        break;
    case Kind::String: {
        char* copy = static_cast<char*>(std::malloc(text.size() + 1));
        std::memcpy(copy, text.c_str(), text.size() + 1);
        variant.type = NPVariantType_String;
        variant.value.stringValue = { copy, static_cast<uint32_t>(text.size()) };
        break;
    }
    case Kind::Object:
        variant.type = NPVariantType_Object;
        variant.value.objectValue = retainObject(object.get());
        break;
    }
    return variant;
}

std::string Value::describe() const
{
    switch (kind) {
    case Kind::Void: return "undefined";
    case Kind::Null: return "null";
    case Kind::Bool: return boolean ? "true" : "false";
    case Kind::Number: return std::format("{}", number);
    case Kind::String: return "\"" + (text.size() > 300 ? text.substr(0, 300) + "..." : text) + "\"";
    case Kind::Object: return "[object]";
    }
    return {};
}

ScriptObject* newScriptObject(NPP npp)
{
    return scriptOf(createObject(npp, &scriptClass));
}

bool readProperty(NPObject* object, std::string_view name, Value& result)
{
    NPIdentifier id = identifier(name);
    if (!object->_class->getProperty)
        return false;
    if (object->_class->hasProperty && !object->_class->hasProperty(object, id))
        return false;

    NPVariant variant {};
    if (!object->_class->getProperty(object, id, &variant))
        return false;
    result = Value::from(variant);
    releaseVariant(variant);
    return true;
}

bool invokeMethod(NPObject* object, std::string_view name, const std::vector<Value>& args, Value& result)
{
    if (!object->_class->invoke)
        return false;

    std::vector<NPVariant> variants;
    for (const Value& arg : args)
        variants.push_back(arg.toVariant());

    NPVariant output {};
    bool ok = object->_class->invoke(object, identifier(name), variants.data(), static_cast<uint32_t>(variants.size()), &output);
    for (NPVariant& variant : variants)
        releaseVariant(variant);

    if (ok) {
        result = Value::from(output);
        releaseVariant(output);
    }
    return ok;
}

std::optional<Value> runScript(NPObject* scope, std::string_view script)
{
    Parser parser(script);
    Value last;
    while (!parser.atEnd()) {
        std::optional<Value> value = runStatement(scope, parser);
        if (!value)
            return std::nullopt;
        last = std::move(*value);
        if (!parser.eat(';') && !parser.atEnd())
            return std::nullopt;
    }
    return last;
}
