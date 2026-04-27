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

#include <cstdio>
#include <cstdlib>
#include <string>

inline bool ReadEnvToggle(const char* name) {
    if (!name || !name[0]) return false;
#if defined(_WIN32)
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) {
        return false;
    }
    const bool enabled = value[0] != '\0' && value[0] != '0';
    std::free(value);
    return enabled;
#else
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && value[0] != '0';
#endif
}

#define N3_ASSERT(expr, msg)                       \
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
    }


#endif //NDEVC_OPTIONAL_H
