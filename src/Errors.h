// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_ERRORS_H
#define NDEVC_ERRORS_H

#include <cstdio>

#include "glad/glad.h"

static inline void GLLogError(const char* where) {
    for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError()) {
        std::printf("[GL_ERR] %s: 0x%04X\n", where, (unsigned)e);
    }
}

#ifdef ENABLE_PARTICLE_LOG
#define PLOG(fmt, ...) std::printf("[P] " fmt "\n", ##__VA_ARGS__)
#else
#define PLOG(...) ((void)0)
#endif

#ifdef ENABLE_PARTICLE_GL_CHECK
#define GLC(where) GLLogError(where)
#else
#define GLC(where) ((void)0)
#endif


#endif //NDEVC_ERRORS_H
