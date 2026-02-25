// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "OpenGLShader.h"
#include "../../Shader.h"

namespace NDEVC::Graphics::OpenGL {

OpenGLShader::OpenGLShader(const char* vertexPath, const char* fragmentPath)
    : shader_(std::make_unique<Shader>(vertexPath, fragmentPath)) {
}

OpenGLShader::~OpenGLShader() = default;

void OpenGLShader::Use() const {
    shader_->Use();
}

void OpenGLShader::Reload() {
    shader_->reload();
}

bool OpenGLShader::IsValid() const {
    return shader_->isValid();
}

void* OpenGLShader::GetNativeHandle() const {
    return (void*)(uintptr_t)shader_->getProgramID();
}

void OpenGLShader::SetMat4(const std::string& name, const glm::mat4& matrix) const {
    shader_->setMat4(name, matrix);
}

void OpenGLShader::SetMat4(UniformID id, const glm::mat4& matrix) const {
    shader_->setMat4(id, matrix);
}

void OpenGLShader::SetMat4Array(const std::string& name, const glm::mat4* values, uint32_t count) const {
    shader_->setMat4Array(name, values, count);
}

void OpenGLShader::SetMat4Array(UniformID id, const glm::mat4* values, uint32_t count) const {
    shader_->setMat4Array(id, values, count);
}

void OpenGLShader::SetInt(const std::string& name, int value) const {
    shader_->setInt(name, value);
}

void OpenGLShader::SetInt(UniformID id, int value) const {
    shader_->setInt(id, value);
}

void OpenGLShader::SetUint(const std::string& name, uint32_t value) const {
    shader_->setUint(name, value);
}

void OpenGLShader::SetUint(UniformID id, uint32_t value) const {
    shader_->setUint(id, value);
}

void OpenGLShader::SetFloat(const std::string& name, float value) const {
    shader_->setFloat(name, value);
}

void OpenGLShader::SetFloat(UniformID id, float value) const {
    shader_->setFloat(id, value);
}

void OpenGLShader::SetVec2(const std::string& name, const glm::vec2& value) const {
    shader_->setVec2(name, value);
}

void OpenGLShader::SetVec2(UniformID id, const glm::vec2& value) const {
    shader_->setVec2(id, value);
}

void OpenGLShader::SetVec3(const std::string& name, const glm::vec3& value) const {
    shader_->setVec3(name, value);
}

void OpenGLShader::SetVec3(UniformID id, const glm::vec3& value) const {
    shader_->setVec3(id, value);
}

void OpenGLShader::SetVec4(const std::string& name, const glm::vec4& value) const {
    shader_->setVec4(name, value);
}

void OpenGLShader::SetVec4(UniformID id, const glm::vec4& value) const {
    shader_->setVec4(id, value);
}

void OpenGLShader::PrecacheUniform(UniformID id, const char* name) const {
    shader_->PrecacheUniform(id, name);
}

}
