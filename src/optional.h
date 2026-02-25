// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_OPTIONAL_H
#define NDEVC_OPTIONAL_H

#ifdef _WIN32
#include <Windows.h>
#endif

#ifdef _WIN32
    #ifdef LIBDATA_EXPORTS
      #define LIBDATA_API __declspec(dllexport)
    #else
      #define LIBDATA_API __declspec(dllimport)
    #endif
#else
    #define LIBDATA_API
#endif

#define N3_ASSERT(expr, msg)                       \
do                                              \
{                                               \
if (!(expr))                                \
{                                           \
char buffer[512];                       \
std::snprintf(buffer, sizeof(buffer),   \
"*** N3 ERROR ***\n"                   \
"expression: %s\n"                 \
"file: %s\n"                       \
"line: %d\n"                       \
"%s\n",                            \
#expr, __FILE__, __LINE__, msg);   \
MessageBoxA(nullptr, buffer,       \
"Assertion Failed lol", MB_OK |        \
MB_ICONERROR);                     \
exit(EXIT_FAILURE);                \
}                                           \
}   while (0)

extern "C" LIBDATA_API void imguieffects();
struct ImGuiContext;
extern ImGuiContext* g_ImGuiContext;

#endif //NDEVC_OPTIONAL_H
