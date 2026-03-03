// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.
#ifndef NDEVC_GL_SHADER_H
#define NDEVC_GL_SHADER_H

#include "glad/glad.h"
#include "Rendering/Interfaces/IShader.h"
#include "Rendering/OpenGL/GLHandles.h"
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace NDEVC::Graphics::OpenGL {

class OpenGLShader : public IShader {
public:
    struct Paths {
        std::string vertex;
        std::string fragment;
    };

    OpenGLShader(const char* vertexPath, const char* fragmentPath);
    OpenGLShader(const char* vertexPath, const char* fragmentPath,
                 const std::string& vertexDefines, const std::string& fragmentDefines);
    ~OpenGLShader() override = default;

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

    void SetInt64(const std::string& name, int64_t value) const override;
    void SetInt64(UniformID id, int64_t value) const override;

    void SetUint64(const std::string& name, uint64_t value) const override;
    void SetUint64(UniformID id, uint64_t value) const override;

    void PrecacheUniform(UniformID id, const char* name) const override;

    void ReloadFromPath(const std::string& path);
    const Paths& GetPaths() const { return currentPaths_; }

private:
    static std::string InjectDefines(const std::string& source, const std::string& defines);
    GLint GetCachedUniformLocation(const std::string& name) const;
    GLint GetCachedUniformLocation(UniformID id) const;
    void CompileAndLink(const std::string& vertexCode, const std::string& fragmentCode);
    GLuint CompileShader(const std::string& source, GLenum type);
    std::pair<std::string, std::string> ParseCombinedShader(const std::string& path);
    std::string ReadFile(const std::string& path);
    void LoadSeparateShaders(const char* vertexPath, const char* fragmentPath);
    void LoadCombinedShader(const char* path);
    void ValidateActiveUniforms() const;

    NDEVC::GL::GLProgHandle program_;
    bool isCombined_ = false;
    Paths currentPaths_;
    std::string vertexDefines_;
    std::string fragmentDefines_;
    mutable std::mutex reloadMutex_;
    mutable std::unordered_map<std::string, GLint> uniformCache_;
    mutable std::unordered_map<UniformID, GLint> uniformCacheById_;
    mutable std::unordered_map<UniformID, std::string> uniformNamesById_;
    mutable std::unordered_set<std::string> missingUniformWarnings_;
    mutable std::unordered_set<UniformID> missingUniformIdWarnings_;
};

}
#endif
