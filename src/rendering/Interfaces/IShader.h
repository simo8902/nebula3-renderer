// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_ISHADER_H
#define NDEVC_ISHADER_H

#include <string>
#include <memory>
#include <cstdint>
#include "glm.hpp"

namespace NDEVC::Graphics {

class IShader {
public:
    using UniformID = uint32_t;

    static constexpr UniformID MakeUniformID(const char* name) {
        uint32_t hash = 2166136261u;
        for (const char* p = name; *p; ++p) {
            hash ^= static_cast<uint8_t>(*p);
            hash *= 16777619u;
        }
        return hash;
    }

    virtual ~IShader() = default;

    virtual void Use() const = 0;
    virtual void Reload() = 0;
    virtual bool IsValid() const = 0;
    virtual void* GetNativeHandle() const = 0;

    virtual void SetMat4(const std::string& name, const glm::mat4& matrix) const = 0;
    virtual void SetMat4(UniformID id, const glm::mat4& matrix) const = 0;
    virtual void SetMat4Array(const std::string& name, const glm::mat4* values, uint32_t count) const = 0;
    virtual void SetMat4Array(UniformID id, const glm::mat4* values, uint32_t count) const = 0;

    virtual void SetInt(const std::string& name, int value) const = 0;
    virtual void SetInt(UniformID id, int value) const = 0;

    virtual void SetUint(const std::string& name, uint32_t value) const = 0;
    virtual void SetUint(UniformID id, uint32_t value) const = 0;

    virtual void SetFloat(const std::string& name, float value) const = 0;
    virtual void SetFloat(UniformID id, float value) const = 0;

    virtual void SetVec2(const std::string& name, const glm::vec2& value) const = 0;
    virtual void SetVec2(UniformID id, const glm::vec2& value) const = 0;

    virtual void SetVec3(const std::string& name, const glm::vec3& value) const = 0;
    virtual void SetVec3(UniformID id, const glm::vec3& value) const = 0;

    virtual void SetVec4(const std::string& name, const glm::vec4& value) const = 0;
    virtual void SetVec4(UniformID id, const glm::vec4& value) const = 0;

    virtual void PrecacheUniform(UniformID id, const char* name) const = 0;
};

}
#endif