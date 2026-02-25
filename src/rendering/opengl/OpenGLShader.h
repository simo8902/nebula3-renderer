// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.
#ifndef NDEVC_GL_SHADER_H
#define NDEVC_GL_SHADER_H

#include <memory>
#include "../abstract/IShader.h"

namespace NDEVC::Graphics {
class Shader;
}

namespace NDEVC::Graphics::OpenGL {

class OpenGLShader : public IShader {
public:
    OpenGLShader(const char* vertexPath, const char* fragmentPath);
    ~OpenGLShader() override;

    void Use() const override;
    void Reload() override;
    bool IsValid() const override;
    void* GetNativeHandle() const override;

    void SetMat4(const std::string& name, const glm::mat4& matrix) const override;
    void SetMat4(UniformID id, const glm::mat4& matrix) const override;
    void SetMat4Array(const std::string& name, const glm::mat4* values, uint32_t count) const override;
    void SetMat4Array(UniformID id, const glm::mat4* values, uint32_t count) const override;

    void SetInt(const std::string& name, int value) const override;
    void SetInt(UniformID id, int value) const override;

    void SetUint(const std::string& name, uint32_t value) const override;
    void SetUint(UniformID id, uint32_t value) const override;

    void SetFloat(const std::string& name, float value) const override;
    void SetFloat(UniformID id, float value) const override;

    void SetVec2(const std::string& name, const glm::vec2& value) const override;
    void SetVec2(UniformID id, const glm::vec2& value) const override;

    void SetVec3(const std::string& name, const glm::vec3& value) const override;
    void SetVec3(UniformID id, const glm::vec3& value) const override;

    void SetVec4(const std::string& name, const glm::vec4& value) const override;
    void SetVec4(UniformID id, const glm::vec4& value) const override;

    void PrecacheUniform(UniformID id, const char* name) const override;

    Shader* GetInternalShader() const { return shader_.get(); }

private:
    std::unique_ptr<Shader> shader_;
};

}
#endif