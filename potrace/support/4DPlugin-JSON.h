#ifndef PLUGIN_JSON_H
#define PLUGIN_JSON_H

#include "4DPluginAPI.h"
#include <string>
#include <vector>
#include <CoreFoundation/CoreFoundation.h>

typedef std::basic_string<PA_Unichar> CUTF16String;
typedef std::basic_string<uint8_t> CUTF8String;

void json_wconv(const wchar_t *value, CUTF16String *u16);

void ob_set_p(PA_ObjectRef obj, const wchar_t *_key, PA_Picture value);
void ob_set_s(PA_ObjectRef obj, const wchar_t *_key, const char *_value);
void ob_set_a(PA_ObjectRef obj, const wchar_t *_key, const wchar_t *_value);
void ob_set_c(PA_ObjectRef obj, const wchar_t *_key, PA_CollectionRef value);
void ob_set_i(PA_ObjectRef obj, const wchar_t *_key, PA_long32 value);
void ob_set_b(PA_ObjectRef obj, const wchar_t *_key, bool value);
bool ob_is_defined(PA_ObjectRef obj, const wchar_t *_key);
bool ob_get_a(PA_ObjectRef obj, const wchar_t *_key, CUTF8String *value);
bool ob_get_b(PA_ObjectRef obj, const wchar_t *_key);
double ob_get_n(PA_ObjectRef obj, const wchar_t *_key);
PA_CollectionRef ob_get_c(PA_ObjectRef obj, const wchar_t *_key);

#endif /* PLUGIN_JSON_H */
