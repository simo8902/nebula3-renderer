// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_ERRORS_H
#define NDEVC_ERRORS_H

#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "Core/Logger.h"
#include "glad/glad.h"

namespace NC::Errors {
class LoggedRuntimeError final : public std::runtime_error {
public:
    explicit LoggedRuntimeError(const std::string& message)
        : std::runtime_error(message) {
        NC::LOGGING::Error(message);
    }
};

static inline std::string FormatMessage(const char* fmt, va_list args) {
    if (!fmt) {
        return {};
    }
    va_list copy;
    va_copy(copy, args);
    const int len = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (len <= 0) {
        return {};
    }
    std::vector<char> buffer(static_cast<size_t>(len) + 1u, '\0');
    std::vsnprintf(buffer.data(), buffer.size(), fmt, args);
    return std::string(buffer.data(), static_cast<size_t>(len));
}

static inline void ParticleLog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const std::string msg = FormatMessage(fmt, args);
    va_end(args);
    NC::LOGGING::Log("[P] ", msg);
}
}

static inline void GLLogError(const char* where) {
    for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError()) {
        char code[16] = {0};
        std::snprintf(code, sizeof(code), "0x%04X", (unsigned)e);
        NC::LOGGING::Error("[GL_ERR] ", (where ? where : "<unknown>"), ": ", code);
    }
}

#ifdef ENABLE_PARTICLE_LOG
#define PLOG(fmt, ...) NC::Errors::ParticleLog(fmt, ##__VA_ARGS__)
#else
#define PLOG(...) ((void)0)
#endif

#ifdef ENABLE_PARTICLE_GL_CHECK
#define GLC(where) GLLogError(where)
#else
#define GLC(where) ((void)0)
#endif


#endif //NDEVC_ERRORS_H
