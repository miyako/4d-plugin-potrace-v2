#ifndef PLUGIN_JSON_H
#define PLUGIN_JSON_H

#include "4DPluginAPI.h"
#include <string>
#include <vector>
#include <map>
#include <iostream>     // std::cout
#include <iterator>     // std::back_inserter
#include <vector>       // std::vector
#include <algorithm>    // std::copy

#ifdef _WIN32
//some external libraries assume first load; include this file after them 
//need to load winsock2 before windows
//BSD wrappers
#define close closesocket
#define TickCount GetTickCount
#define getpid GetCurrentProcessId
#include <winsock2.h>

#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#pragma comment(lib, "iphlpapi.lib")
#include <time.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netdb.h>
#include <arpa/inet.h>
#define SOCKET int
#define SOCKET_ERROR (-1)
#define INVALID_SOCKET (SOCKET)(~0)
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

typedef std::basic_string<PA_Unichar> CUTF16String;
typedef std::basic_string<uint8_t> CUTF8String;

void json_wconv(const wchar_t *value, CUTF16String *u16);

void ob_set_p(PA_ObjectRef obj, const wchar_t *_key, PA_Picture value);
void ob_set_s(PA_ObjectRef obj, const wchar_t *_key, const char *_value);
void ob_set_a(PA_ObjectRef obj, const wchar_t *_key, const wchar_t *_value);
void ob_set_c(PA_ObjectRef obj, const wchar_t *_key, PA_CollectionRef value);
void ob_set_o(PA_ObjectRef obj, const wchar_t *_key, PA_ObjectRef value);
void ob_set_i(PA_ObjectRef obj, const wchar_t *_key, PA_long32 value);
void ob_set_n(PA_ObjectRef obj, const wchar_t *_key, double value);
void ob_set_b(PA_ObjectRef obj, const wchar_t *_key, bool value);
bool ob_is_defined(PA_ObjectRef obj, const wchar_t *_key);
bool ob_get_a(PA_ObjectRef obj, const wchar_t *_key, CUTF8String *value);
bool ob_get_b(PA_ObjectRef obj, const wchar_t *_key);
double ob_get_n(PA_ObjectRef obj, const wchar_t *_key);
PA_CollectionRef ob_get_c(PA_ObjectRef obj, const wchar_t *_key);

#endif /* PLUGIN_JSON_H */
