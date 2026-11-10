#include "4DPlugin-JSON.h"
#include "PrivateTypes.h"
#include "EntryPoints.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace {
bool allocatedEmpty;
std::set<PA_Unichar*> strings;
std::map<void*, size_t> handles;
struct Object {
    int refs = 1;
    std::map<CUTF16String, PA_Variable> properties;
};
std::set<Object*> objects;

CUTF16String text(const PA_Unistring& value) {
    PA_Unichar* buffer = value.fString;
    if (!buffer) {
        assert(value.fLength == 0);
        return {};
    }
    assert(strings.count(buffer));
    assert(buffer[value.fLength] == 0);
    return CUTF16String(buffer, value.fLength);
}

PA_Unistring makeString(const CUTF16String& value) {
    PA_Unistring result = {};
    result.fLength = static_cast<PA_long32>(value.size());
    if (!value.empty() || allocatedEmpty) {
        result.fString = new PA_Unichar[value.size() + 1]();
        std::copy(value.begin(), value.end(), result.fString);
        PA_Unichar* buffer = result.fString;
        assert(strings.insert(buffer).second);
    }
    return result;
}

void disposeString(PA_Unistring value) {
    PA_Unichar* buffer = value.fString;
    if (buffer) {
        assert(strings.erase(buffer) == 1);
        delete[] buffer;
    }
}

Object* object() {
    Object* value = new Object;
    objects.insert(value);
    return value;
}

PA_Variable copy(PA_Variable value) {
    if (value.fType == eVK_Unistring)
        value.uValue.fString = makeString(text(value.uValue.fString));
    else if (value.fType == eVK_Object || value.fType == eVK_Collection) {
        Object* ref = static_cast<Object*>(value.fType == eVK_Object
            ? value.uValue.fObject : value.uValue.fCollection);
        if (ref) {
            assert(objects.count(ref));
            ++ref->refs;
        }
    }
    return value;
}

void clear(PA_Variable& value) {
    if (value.fType == eVK_Unistring)
        disposeString(value.uValue.fString);
    else if (value.fType == eVK_Object || value.fType == eVK_Collection) {
        Object* ref = static_cast<Object*>(value.fType == eVK_Object
            ? value.uValue.fObject : value.uValue.fCollection);
        if (ref) {
            assert(objects.count(ref));
            if (--ref->refs == 0) {
                for (auto& property : ref->properties)
                    clear(property.second);
                objects.erase(ref);
                delete ref;
            }
        }
    } else {
        assert(value.fType == eVK_Undefined || value.fType == eVK_Boolean
            || value.fType == eVK_Real || value.fType == eVK_Longint);
    }
    value = {};
    value.fType = eVK_Undefined;
}

void engine(short selector, EngineBlock* eb) {
    switch (selector) {
        case EX_CREATE_UNISTRING: {
            const auto* input = reinterpret_cast<PA_Unichar*>(eb->fHandle);
            eb->fUniString1 = makeString(input ? CUTF16String(input) : CUTF16String());
            break;
        }
        case EX_DISPOSE_UNISTRING:
            disposeString(eb->fUniString1);
            break;
        case EX_SET_UNISTRING: {
            CUTF16String input(reinterpret_cast<PA_Unichar*>(eb->fHandle));
            disposeString(eb->fUniString1);
            eb->fUniString1 = makeString(input);
            break;
        }
        case EX_CLEAR_VARIABLE:
            clear(*static_cast<PA_Variable*>(eb->fPtr1));
            break;
        case EX_VARIABLE_TO_STRING: {
            const auto& value = *reinterpret_cast<PA_Variable*>(eb->fHandle);
            assert(value.fType == eVK_Unistring);
            text(value.uValue.fString);
            eb->fUniString1 = value.uValue.fString;
            break;
        }
        case EX_SET_OBJ_VALUE: {
            assert(eb->fShort == eVK_Object);
            auto* ref = static_cast<Object*>(eb->fPtr1);
            assert(objects.count(ref));
            PA_Variable undefined = {};
            undefined.fType = eVK_Undefined;
            auto& property = ref->properties.emplace(text(eb->fUniString1), undefined).first->second;
            clear(property);
            property = copy(*static_cast<PA_Variable*>(eb->fPtr2));
            break;
        }
        case EX_GET_OBJ_VALUE: {
            assert(eb->fShort == eVK_Object);
            auto* ref = static_cast<Object*>(eb->fPtr1);
            assert(objects.count(ref));
            *static_cast<PA_Variable*>(eb->fPtr2) =
                copy(ref->properties.at(text(eb->fUniString1)));
            break;
        }
        case EX_EXECUTE_COMMAND_BY_ID: {
            assert(eb->fParam1 == 1231 && eb->fParam2 == 2);
            auto** args = static_cast<PA_Variable**>(eb->fPtr1);
            auto* ref = static_cast<Object*>(args[0]->uValue.fObject);
            assert(objects.count(ref));
            PA_SetBooleanVariable(static_cast<PA_Variable*>(eb->fPtr2),
                ref->properties.count(text(args[1]->uValue.fString)) != 0);
            break;
        }
        case EX_HANDLE_MANAGER: {
            void* handle = eb->fHandle;
            switch (eb->fParam1) {
                case 1:
                    handle = std::calloc(1, eb->fParam2 ? eb->fParam2 : 1);
                    assert(handle);
                    handles[handle] = eb->fParam2;
                    eb->fHandle = reinterpret_cast<PA_Handle>(handle);
                    break;
                case 2:
                    assert(handles.erase(handle) == 1);
                    std::free(handle);
                    break;
                case 5:
                    assert(handles.count(handle));
                    eb->fParam3 = reinterpret_cast<sLONG_PTR>(handle);
                    break;
                case 6:
                    assert(handles.count(handle));
                    break;
                default:
                    std::abort();
            }
            break;
        }
        default:
            std::abort();
    }
    eb->fError = eER_NoErr;
}

const PA_Unichar keyData[] = {'k', 0};
CUTF16String key(keyData);

void checkValue(Object* ref, const CUTF16String& expected) {
    const auto& value = ref->properties.at(key);
    assert(value.fType == eVK_Unistring);
    assert(text(value.uValue.fString) == expected);
}

void setters(bool wide) {
    const char* utf8[] = {"ASCII", "\xc3\xa9\xe6\xbc\xa2", "\xf0\x9f\x98\x80", ""};
    const wchar_t* unicode[] = {L"ASCII", L"\u00e9\u6f22", L"\U0001f600", L""};
    const CUTF16String expected[] = {
        { 'A', 'S', 'C', 'I', 'I' }, {0x00e9, 0x6f22}, {0xd83d, 0xde00}, {}
    };
    auto* ref = object();
    ob_set_s(nullptr, L"k", "ignored");
    ob_set_a(nullptr, L"k", L"ignored");
    ob_set_s(ref, L"k", nullptr);
    ob_set_a(ref, L"k", nullptr);
    assert(ref->properties.empty());
    for (int repeat = 0; repeat < 20; ++repeat) {
        for (size_t i = 0; i < 4; ++i) {
            if (wide)
                ob_set_a(ref, L"k", unicode[i]);
            else
                ob_set_s(ref, L"k", utf8[i]);
            checkValue(ref, expected[i]);
        }
    }
    PA_DisposeObject(ref);
    assert(strings.empty() && "setter leaked an overwritten empty allocation");
}

void getters(const char* reader) {
    auto* ref = object();
    const CUTF16String inputs[] = {
        {'A'}, {0x00e9, 0x6f22}, {0xd83d, 0xde00}, {}
    };
    const char* expected[] = {"A", "\xc3\xa9\xe6\xbc\xa2", "\xf0\x9f\x98\x80", ""};
    for (size_t i = 0; i < 4; ++i) {
        PA_Variable value = {};
        value.fType = eVK_Unistring;
        value.uValue.fString = makeString(inputs[i]);
        PA_Unistring propertyKey = PA_CreateUnistring(const_cast<PA_Unichar*>(key.c_str()));
        PA_SetObjectProperty(ref, &propertyKey, value);
        PA_DisposeUnistring(&propertyKey);
        PA_ClearVariable(&value);
        for (int repeat = 0; repeat < 20; ++repeat) {
            if (std::strcmp(reader, "text") == 0) {
                CUTF8String output;
                assert(ob_get_a(ref, L"k", &output));
                assert(output == CUTF8String(reinterpret_cast<const uint8_t*>(expected[i])));
            } else if (std::strcmp(reader, "boolean") == 0) {
                assert(!ob_get_b(ref, L"k"));
            } else if (std::strcmp(reader, "number") == 0) {
                assert(ob_get_n(ref, L"k") == 0);
            } else {
                assert(std::strcmp(reader, "collection") == 0);
                assert(ob_get_c(ref, L"k") == nullptr);
            }
            checkValue(ref, inputs[i]);
        }
    }
    PA_DisposeObject(ref);
    assert(strings.empty() && "property reader leaked an owning result");
}

void handoffs() {
    auto* ref = object();
    auto* collection = object();
    ob_set_c(ref, L"k", collection);
    assert(collection->refs == 1);
    auto* result = static_cast<Object*>(ob_get_c(ref, L"k"));
    assert(result == collection && collection->refs == 2);
    CUTF8String output;
    assert(ob_get_a(ref, L"k", &output));
    assert(!ob_get_b(ref, L"k"));
    assert(ob_get_n(ref, L"k") == 0);
    assert(collection->refs == 2);
    PA_DisposeObject(ref);
    assert(objects.count(result) && result->refs == 1);
    PA_DisposeCollection(result);

    ref = object();
    auto* child = object();
    ob_set_o(ref, L"k", child);
    assert(child->refs == 1);
    assert(ob_get_a(ref, L"k", &output));
    assert(!ob_get_b(ref, L"k"));
    assert(ob_get_n(ref, L"k") == 0);
    assert(ob_get_c(ref, L"k") == nullptr);
    assert(child->refs == 1 && "mismatched readers leaked object references");
    ob_set_b(ref, L"k", true);
    assert(ob_get_b(ref, L"k"));
    ob_set_n(ref, L"k", 2.5);
    assert(ob_get_n(ref, L"k") == 2.5);
    assert(!ob_get_a(ref, L"missing", &output));
    ob_set_a(ref, L"k", L"\u00e9\U0001f600");
    assert(ob_get_a(ref, L"k", &output));
    ob_set_a(ref, L"k", L"replaced");
    assert(!ob_get_b(nullptr, L"k"));
    assert(ob_get_n(nullptr, L"k") == 0);
    assert(ob_get_c(nullptr, L"k") == nullptr);
    PA_DisposeObject(ref);
    assert(output == CUTF8String(reinterpret_cast<const uint8_t*>(
        "\xc3\xa9\xf0\x9f\x98\x80")));
}
}

extern "C" void PluginMain(PA_long32, PA_PluginParameters) {}

int main(int argc, char** argv) {
    assert(argc == 3);
    allocatedEmpty = std::strcmp(argv[2], "allocated") == 0;
    gCall4D = engine;
    if (std::strcmp(argv[1], "utf8") == 0)
        setters(false);
    else if (std::strcmp(argv[1], "wide") == 0)
        setters(true);
    else if (std::strcmp(argv[1], "handoffs") == 0)
        handoffs();
    else
        getters(argv[1]);
    assert(strings.empty() && objects.empty() && handles.empty());
}
