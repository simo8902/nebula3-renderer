#ifndef NDEVC_OPENGL_GLHANDLES_H
#define NDEVC_OPENGL_GLHANDLES_H

#include "glad/glad.h"

namespace NDEVC::GL {

struct GLTexHandle {
    GLuint id = 0;

    GLTexHandle() = default;
    explicit GLTexHandle(GLuint h) : id(h) {}
    ~GLTexHandle() { if (id) { glDeleteTextures(1, &id); } }

    GLTexHandle(GLTexHandle&& o) noexcept : id(o.id) { o.id = 0; }
    GLTexHandle& operator=(GLTexHandle&& o) noexcept {
        if (this != &o) {
            if (id) { glDeleteTextures(1, &id); }
            id = o.id;
            o.id = 0;
        }
        return *this;
    }

    GLTexHandle(const GLTexHandle&) = delete;
    GLTexHandle& operator=(const GLTexHandle&) = delete;

    operator GLuint() const { return id; }
    GLuint* put() { if (id) { glDeleteTextures(1, &id); id = 0; } return &id; }
    bool valid() const { return id != 0; }
    void reset() { if (id) { glDeleteTextures(1, &id); id = 0; } }
};

struct GLFBOHandle {
    GLuint id = 0;

    GLFBOHandle() = default;
    explicit GLFBOHandle(GLuint h) : id(h) {}
    ~GLFBOHandle() { if (id) { glDeleteFramebuffers(1, &id); } }

    GLFBOHandle(GLFBOHandle&& o) noexcept : id(o.id) { o.id = 0; }
    GLFBOHandle& operator=(GLFBOHandle&& o) noexcept {
        if (this != &o) {
            if (id) { glDeleteFramebuffers(1, &id); }
            id = o.id;
            o.id = 0;
        }
        return *this;
    }

    GLFBOHandle(const GLFBOHandle&) = delete;
    GLFBOHandle& operator=(const GLFBOHandle&) = delete;

    operator GLuint() const { return id; }
    GLuint* put() { if (id) { glDeleteFramebuffers(1, &id); id = 0; } return &id; }
    bool valid() const { return id != 0; }
    void reset() { if (id) { glDeleteFramebuffers(1, &id); id = 0; } }
};

struct GLBufHandle {
    GLuint id = 0;

    GLBufHandle() = default;
    explicit GLBufHandle(GLuint h) : id(h) {}
    ~GLBufHandle() { if (id) { glDeleteBuffers(1, &id); } }

    GLBufHandle(GLBufHandle&& o) noexcept : id(o.id) { o.id = 0; }
    GLBufHandle& operator=(GLBufHandle&& o) noexcept {
        if (this != &o) {
            if (id) { glDeleteBuffers(1, &id); }
            id = o.id;
            o.id = 0;
        }
        return *this;
    }

    GLBufHandle(const GLBufHandle&) = delete;
    GLBufHandle& operator=(const GLBufHandle&) = delete;

    operator GLuint() const { return id; }
    GLuint* put() { if (id) { glDeleteBuffers(1, &id); id = 0; } return &id; }
    bool valid() const { return id != 0; }
    void reset() { if (id) { glDeleteBuffers(1, &id); id = 0; } }
};

struct GLVAOHandle {
    GLuint id = 0;

    GLVAOHandle() = default;
    explicit GLVAOHandle(GLuint h) : id(h) {}
    ~GLVAOHandle() { if (id) { glDeleteVertexArrays(1, &id); } }

    GLVAOHandle(GLVAOHandle&& o) noexcept : id(o.id) { o.id = 0; }
    GLVAOHandle& operator=(GLVAOHandle&& o) noexcept {
        if (this != &o) {
            if (id) { glDeleteVertexArrays(1, &id); }
            id = o.id;
            o.id = 0;
        }
        return *this;
    }

    GLVAOHandle(const GLVAOHandle&) = delete;
    GLVAOHandle& operator=(const GLVAOHandle&) = delete;

    operator GLuint() const { return id; }
    GLuint* put() { if (id) { glDeleteVertexArrays(1, &id); id = 0; } return &id; }
    bool valid() const { return id != 0; }
    void reset() { if (id) { glDeleteVertexArrays(1, &id); id = 0; } }
};

struct GLSamplerHandle {
    GLuint id = 0;

    GLSamplerHandle() = default;
    explicit GLSamplerHandle(GLuint h) : id(h) {}
    ~GLSamplerHandle() { if (id) { glDeleteSamplers(1, &id); } }

    GLSamplerHandle(GLSamplerHandle&& o) noexcept : id(o.id) { o.id = 0; }
    GLSamplerHandle& operator=(GLSamplerHandle&& o) noexcept {
        if (this != &o) {
            if (id) { glDeleteSamplers(1, &id); }
            id = o.id;
            o.id = 0;
        }
        return *this;
    }

    GLSamplerHandle(const GLSamplerHandle&) = delete;
    GLSamplerHandle& operator=(const GLSamplerHandle&) = delete;

    operator GLuint() const { return id; }
    GLuint* put() { if (id) { glDeleteSamplers(1, &id); id = 0; } return &id; }
    bool valid() const { return id != 0; }
    void reset() { if (id) { glDeleteSamplers(1, &id); id = 0; } }
};

struct GLProgHandle {
    GLuint id = 0;

    GLProgHandle() = default;
    explicit GLProgHandle(GLuint h) : id(h) {}
    ~GLProgHandle() { if (id) { glDeleteProgram(id); } }

    GLProgHandle(GLProgHandle&& o) noexcept : id(o.id) { o.id = 0; }
    GLProgHandle& operator=(GLProgHandle&& o) noexcept {
        if (this != &o) {
            if (id) { glDeleteProgram(id); }
            id = o.id;
            o.id = 0;
        }
        return *this;
    }

    GLProgHandle(const GLProgHandle&) = delete;
    GLProgHandle& operator=(const GLProgHandle&) = delete;

    operator GLuint() const { return id; }
    GLuint* put() { if (id) { glDeleteProgram(id); id = 0; } return &id; }
    bool valid() const { return id != 0; }
    void reset() { if (id) { glDeleteProgram(id); id = 0; } }
};

}

#endif
