// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/OpenGL/OpenGLShader.h"
#include "Core/Errors.h"
#include "Core/Logger.h"
#include "Core/VFS.h"
#include "gtc/type_ptr.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace NDEVC::Graphics::OpenGL {

OpenGLShader::OpenGLShader(const char* vertexPath, const char* fragmentPath)
    : OpenGLShader(vertexPath, fragmentPath, std::string(), std::string()) {}

OpenGLShader::OpenGLShader(const char* vertexPath, const char* fragmentPath,
                           const std::string& vertexDefines, const std::string& fragmentDefines)
    : vertexDefines_(vertexDefines), fragmentDefines_(fragmentDefines) {
    const std::string vertexPathStr = vertexPath ? std::string(vertexPath) : std::string();
    const std::string fragmentPathStr = fragmentPath ? std::string(fragmentPath) : std::string();
    NC::LOGGING::Log("[SHADER] Create request V=", vertexPathStr,
                     " F=", fragmentPathStr,
                     " VDef=", vertexDefines_.size(),
                     " FDef=", fragmentDefines_.size());
    const bool combinedPath = !vertexPathStr.empty() &&
                              vertexPathStr == fragmentPathStr &&
                              std::filesystem::path(vertexPathStr).extension() == ".glsl";

    if (combinedPath) {
        if (!std::filesystem::exists(vertexPathStr) && !NC::VFS::Instance().Exists(vertexPathStr)) {
            throw NC::Errors::LoggedRuntimeError("Combined shader file does not exist: " + vertexPathStr);
        }
        NC::LOGGING::Log("[SHADER] Using combined file ", vertexPathStr);
        auto [vertexCode, fragmentCode] = ParseCombinedShader(vertexPathStr);
        CompileAndLink(
            InjectDefines(vertexCode, vertexDefines_),
            InjectDefines(fragmentCode, fragmentDefines_));
        currentPaths_ = {vertexPathStr, fragmentPathStr};
        isCombined_ = true;
        NC::LOGGING::Log("[SHADER] Create completed combined Program=", program_.id);
        return;
    }

    if (!std::filesystem::exists(vertexPathStr) && !NC::VFS::Instance().Exists(vertexPathStr)) {
        throw NC::Errors::LoggedRuntimeError("Vertex shader file does not exist: " + vertexPathStr);
    }
    if (!std::filesystem::exists(fragmentPathStr) && !NC::VFS::Instance().Exists(fragmentPathStr)) {
        throw NC::Errors::LoggedRuntimeError("Fragment shader file does not exist: " + fragmentPathStr);
    }

    NC::LOGGING::Log("[SHADER] Using separate files V=", vertexPathStr, " F=", fragmentPathStr);
    CompileAndLink(
        InjectDefines(ReadFile(vertexPathStr), vertexDefines_),
        InjectDefines(ReadFile(fragmentPathStr), fragmentDefines_));
    currentPaths_ = {vertexPathStr, fragmentPathStr};
    NC::LOGGING::Log("[SHADER] Create completed separate Program=", program_.id);
}

void OpenGLShader::LoadCombinedShader(const char* path) {
    NC::LOGGING::Log("[SHADER] Reload combined ", (path ? path : "<null>"));
    auto [vertexCode, fragmentCode] = ParseCombinedShader(path);
    CompileAndLink(vertexCode, fragmentCode);
    currentPaths_ = {path, path};
    isCombined_ = true;
}

void OpenGLShader::LoadSeparateShaders(const char* vertexPath, const char* fragmentPath) {
    NC::LOGGING::Log("[SHADER] Reload separate V=", (vertexPath ? vertexPath : "<null>"),
                     " F=", (fragmentPath ? fragmentPath : "<null>"));
    std::string vertexCode = ReadFile(vertexPath);
    std::string fragmentCode = ReadFile(fragmentPath);
    CompileAndLink(
        InjectDefines(vertexCode, vertexDefines_),
        InjectDefines(fragmentCode, fragmentDefines_));
    currentPaths_ = {vertexPath, fragmentPath};
    isCombined_ = false;
}

std::pair<std::string, std::string> OpenGLShader::ParseCombinedShader(const std::string& filePath) {
    NC::LOGGING::Log("[SHADER] Parse combined file ", filePath);

    std::istringstream vfsStream;
    std::ifstream diskFile;

    const NC::VFS::View vfsView = NC::VFS::Instance().Read(filePath);
    if (vfsView.valid()) {
        vfsStream = std::istringstream(
            std::string(reinterpret_cast<const char*>(vfsView.data), vfsView.size));
    } else if (NC::VFS::Instance().IsMounted()) {
        throw NC::Errors::LoggedRuntimeError("Combined shader not found in package: " + filePath);
    } else {
        diskFile.open(filePath);
        if (!diskFile.is_open()) {
            throw NC::Errors::LoggedRuntimeError("Failed to open file: " + filePath);
        }
    }

    std::istream& stream = vfsView.valid()
        ? static_cast<std::istream&>(vfsStream)
        : static_cast<std::istream&>(diskFile);

    std::string line;
    std::string vertexCode;
    std::string fragmentCode;
    bool isVertex = false;
    bool isFragment = false;

    while (std::getline(stream, line)) {
        if (line.find("#type vertex") != std::string::npos) {
            isVertex = true;
            isFragment = false;
            NC::LOGGING::Log("[SHADER] Parse marker #type vertex");
            continue;
        }
        if (line.find("#type fragment") != std::string::npos) {
            isFragment = true;
            isVertex = false;
            NC::LOGGING::Log("[SHADER] Parse marker #type fragment");
            continue;
        }

        if (isVertex) {
            vertexCode += line + "\n";
        } else if (isFragment) {
            fragmentCode += line + "\n";
        }
    }

    NC::LOGGING::Log("[SHADER] Parsed combined sections V=", vertexCode.size(), " bytes F=", fragmentCode.size(), " bytes");
    return {vertexCode, fragmentCode};
}

void OpenGLShader::Reload() {
    NC::LOGGING::Log("[SHADER] Reload begin V=", currentPaths_.vertex, " F=", currentPaths_.fragment, " combined=", isCombined_);
    std::scoped_lock lk(reloadMutex_);
    if (isCombined_) {
        LoadCombinedShader(currentPaths_.vertex.c_str());
    } else {
        LoadSeparateShaders(currentPaths_.vertex.c_str(), currentPaths_.fragment.c_str());
    }
    NC::LOGGING::Log("[SHADER] Reload end Program=", program_.id);
}

void OpenGLShader::ReloadFromPath(const std::string& path) {
    NC::LOGGING::Log("[SHADER] ReloadFromPath ", path);
    std::scoped_lock lk(reloadMutex_);
    if (path.ends_with(".glsl")) {
        LoadCombinedShader(path.c_str());
    } else if (path.ends_with(".vert")) {
        currentPaths_.vertex = path;
        LoadSeparateShaders(currentPaths_.vertex.c_str(), currentPaths_.fragment.c_str());
    } else if (path.ends_with(".frag")) {
        currentPaths_.fragment = path;
        LoadSeparateShaders(currentPaths_.vertex.c_str(), currentPaths_.fragment.c_str());
    } else {
        NC::LOGGING::Warning("[SHADER] ReloadFromPath ignored unsupported extension path=", path);
    }
    NC::LOGGING::Log("[SHADER] ReloadFromPath end Program=", program_.id, " V=", currentPaths_.vertex, " F=", currentPaths_.fragment);
}

void OpenGLShader::CompileAndLink(const std::string& vertexCode, const std::string& fragmentCode) {
    NC::LOGGING::Log("[SHADER] CompileAndLink begin V=", vertexCode.size(), " bytes F=", fragmentCode.size(), " bytes");
    GLuint vertexShader = CompileShader(vertexCode, GL_VERTEX_SHADER);
    GLuint fragmentShader = CompileShader(fragmentCode, GL_FRAGMENT_SHADER);

    GLuint newProgram = glCreateProgram();
    glAttachShader(newProgram, vertexShader);
    glAttachShader(newProgram, fragmentShader);
    glLinkProgram(newProgram);

    GLint success = 0;
    glGetProgramiv(newProgram, GL_LINK_STATUS, &success);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (!success) {
        GLchar infoLog[512];
        glGetProgramInfoLog(newProgram, 512, nullptr, infoLog);
        NC::LOGGING::Error("[SHADER] Program link failed: ", infoLog);
        glDeleteProgram(newProgram);
        throw NC::Errors::LoggedRuntimeError("Shader program linking failed.");
    }

    program_.reset();
    program_.id = newProgram;
    uniformCache_.clear();
    uniformCacheById_.clear();
    uniformNamesById_.clear();
    missingUniformWarnings_.clear();
    missingUniformIdWarnings_.clear();
    NC::LOGGING::Log("[SHADER] Link success Program=", program_.id);
    ValidateActiveUniforms();
}

GLuint OpenGLShader::CompileShader(const std::string& source, GLenum type) {
    if (source.empty()) {
        throw NC::Errors::LoggedRuntimeError("Empty shader source");
    }

    NC::LOGGING::Log("[SHADER] Compile stage ", (type == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT"),
                     " size=", source.size(), " bytes");
    const char* src = source.c_str();
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success) {
        GLchar infoLog[1024];
        glGetShaderInfoLog(shader, 1024, nullptr, infoLog);

        NC::LOGGING::Error("[SHADER] Compile failed stage=",
                           (type == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT"),
                           " error=", infoLog);

        glDeleteShader(shader);
        throw NC::Errors::LoggedRuntimeError("Shader compilation failed.");
    }

    NC::LOGGING::Log("[SHADER] Compile success stage=", (type == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT"),
                     " id=", shader);
    return shader;
}

std::string OpenGLShader::InjectDefines(const std::string& source, const std::string& defines) {
    if (defines.empty()) {
        NC::LOGGING::Log("[SHADER] InjectDefines skipped (empty defines)");
        return source;
    }

    const size_t versionPos = source.find("#version");
    if (versionPos == std::string::npos) {
        NC::LOGGING::Log("[SHADER] InjectDefines prepend (no #version) defineBytes=", defines.size(), " sourceBytes=", source.size());
        return defines + "\n" + source;
    }

    const size_t lineEnd = source.find('\n', versionPos);
    if (lineEnd == std::string::npos) {
        NC::LOGGING::Log("[SHADER] InjectDefines append (single-line #version) defineBytes=", defines.size(), " sourceBytes=", source.size());
        return source + "\n" + defines + "\n";
    }

    std::string out;
    out.reserve(source.size() + defines.size() + 2);
    out.append(source, 0, lineEnd + 1);
    out.append(defines);
    if (!defines.empty() && defines.back() != '\n') {
        out.push_back('\n');
    }
    out.append(source, lineEnd + 1, std::string::npos);
    NC::LOGGING::Log("[SHADER] InjectDefines inserted after #version defineBytes=", defines.size(), " resultBytes=", out.size());
    return out;
}

std::string OpenGLShader::ReadFile(const std::string& path) {
    NC::LOGGING::Log("[SHADER] ReadFile ", path);
    // VFS first — zero disk I/O in standalone/packed mode.
    const NC::VFS::View vfsView = NC::VFS::Instance().Read(path);
    if (vfsView.valid()) {
        NC::LOGGING::Log("[SHADER] ReadFile from VFS bytes=", vfsView.size, " path=", path);
        return std::string(reinterpret_cast<const char*>(vfsView.data), vfsView.size);
    }
    // When a package is mounted, shaders must come from VFS — no disk fallback.
    if (NC::VFS::Instance().IsMounted()) {
        throw NC::Errors::LoggedRuntimeError("Shader not found in package: " + path);
    }
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw NC::Errors::LoggedRuntimeError("Failed to open file: " + path);
    }

    const size_t fileSize = static_cast<size_t>(file.tellg());
    std::string buffer(fileSize, ' ');
    file.seekg(0);
    file.read(&buffer[0], static_cast<std::streamsize>(fileSize));
    file.close();
    NC::LOGGING::Log("[SHADER] ReadFile bytes=", fileSize, " path=", path);
    return buffer;
}

void OpenGLShader::Use() const {
    glUseProgram(program_);
}

bool OpenGLShader::IsValid() const {
    return program_.valid();
}

void* OpenGLShader::GetNativeHandle() const {
    return (void*)&program_.id;
}

GLint OpenGLShader::GetCachedUniformLocation(const std::string& name) const {
    auto it = uniformCache_.find(name);
    if (it != uniformCache_.end()) {
        return it->second;
    }
    GLint location = glGetUniformLocation(program_, name.c_str());
    uniformCache_[name] = location;
    if (location == -1 && missingUniformWarnings_.insert(name).second) {
        NC::LOGGING::Warning("[SHADER][UNIFORM] Missing uniform Program=", program_.id, " name=", name);
    }
    return location;
}

GLint OpenGLShader::GetCachedUniformLocation(UniformID id) const {
    auto it = uniformCacheById_.find(id);
    if (it != uniformCacheById_.end()) {
        return it->second;
    }

    auto nameIt = uniformNamesById_.find(id);
    if (nameIt == uniformNamesById_.end()) {
        if (missingUniformIdWarnings_.insert(id).second) {
            NC::LOGGING::Warning("[SHADER][UNIFORM] UniformID unresolved Program=", program_.id, " id=", static_cast<int>(id));
        }
        return -1;
    }

    GLint location = GetCachedUniformLocation(nameIt->second);
    uniformCacheById_[id] = location;
    return location;
}

void OpenGLShader::PrecacheUniform(UniformID id, const char* name) const {
    if (!name || !name[0]) {
        NC::LOGGING::Warning("[SHADER] PrecacheUniform ignored empty name id=", static_cast<int>(id));
        return;
    }

    if (uniformCacheById_.find(id) != uniformCacheById_.end()) {
        return;
    }

    auto nameIt = uniformNamesById_.find(id);
    if (nameIt == uniformNamesById_.end()) {
        uniformNamesById_[id] = name;
    }

    GLint location = GetCachedUniformLocation(uniformNamesById_[id]);
    uniformCacheById_[id] = location;
}

void OpenGLShader::ValidateActiveUniforms() const {
    if (!program_.id) return;
    GLint activeCount = 0;
    glGetProgramiv(program_.id, GL_ACTIVE_UNIFORMS, &activeCount);

    std::unordered_set<std::string> activeNames;
    activeNames.reserve(static_cast<size_t>(activeCount));
    char nameBuf[256];
    for (GLint i = 0; i < activeCount; ++i) {
        GLsizei length = 0;
        GLint   size   = 0;
        GLenum  type   = 0;
        glGetActiveUniform(program_.id, static_cast<GLuint>(i), sizeof(nameBuf) - 1,
                           &length, &size, &type, nameBuf);
        nameBuf[length] = '\0';
        // Strip array suffix (e.g. "lights[0]" → "lights")
        char* bracket = std::strchr(nameBuf, '[');
        if (bracket) *bracket = '\0';
        activeNames.emplace(nameBuf);
    }

    // Warn for every string-keyed cached uniform that the GPU doesn't expose
    for (const auto& [name, loc] : uniformCache_) {
        if (activeNames.find(name) == activeNames.end()) {
            NC::LOGGING::Warning("[SHADER][VALIDATE] Uniform cached but not active Program=",
                                 program_.id, " name=", name);
        }
    }
}

void OpenGLShader::SetMat4(const std::string& name, const glm::mat4& matrix) const {
    GLint location = GetCachedUniformLocation(name);
    if (location != -1) {
        glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
    }
}

void OpenGLShader::SetMat4(UniformID id, const glm::mat4& matrix) const {
    GLint location = GetCachedUniformLocation(id);
    if (location != -1) {
        glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
    }
}

void OpenGLShader::SetMat4Array(const std::string& name, const glm::mat4* values, uint32_t count) const {
    if (!values || count == 0) return;
    GLint location = GetCachedUniformLocation(name);
    if (location != -1) {
        glUniformMatrix4fv(location, static_cast<GLsizei>(count), GL_FALSE, glm::value_ptr(values[0]));
    }
}

void OpenGLShader::SetMat4Array(UniformID id, const glm::mat4* values, uint32_t count) const {
    if (!values || count == 0) return;
    GLint location = GetCachedUniformLocation(id);
    if (location != -1) {
        glUniformMatrix4fv(location, static_cast<GLsizei>(count), GL_FALSE, glm::value_ptr(values[0]));
    }
}

void OpenGLShader::SetInt(const std::string& name, int value) const {
    GLint location = GetCachedUniformLocation(name);
    if (location != -1) {
        glUniform1i(location, value);
    }
}

void OpenGLShader::SetInt(UniformID id, int value) const {
    GLint location = GetCachedUniformLocation(id);
    if (location != -1) {
        glUniform1i(location, value);
    }
}

void OpenGLShader::SetFloat(const std::string& name, float value) const {
    GLint location = GetCachedUniformLocation(name);
    if (location != -1) {
        glUniform1f(location, value);
    }
}

void OpenGLShader::SetFloat(UniformID id, float value) const {
    GLint location = GetCachedUniformLocation(id);
    if (location != -1) {
        glUniform1f(location, value);
    }
}

void OpenGLShader::SetUint(const std::string& name, uint32_t value) const {
    GLint location = GetCachedUniformLocation(name);
    if (location != -1) {
        glUniform1ui(location, value);
    }
}

void OpenGLShader::SetUint(UniformID id, uint32_t value) const {
    GLint location = GetCachedUniformLocation(id);
    if (location != -1) {
        glUniform1ui(location, value);
    }
}

void OpenGLShader::SetVec3(const std::string& name, const glm::vec3& value) const {
    GLint location = GetCachedUniformLocation(name);
    if (location != -1) {
        glUniform3fv(location, 1, glm::value_ptr(value));
    }
}

void OpenGLShader::SetVec3(UniformID id, const glm::vec3& value) const {
    GLint location = GetCachedUniformLocation(id);
    if (location != -1) {
        glUniform3fv(location, 1, glm::value_ptr(value));
    }
}

void OpenGLShader::SetVec4(const std::string& name, const glm::vec4& value) const {
    GLint location = GetCachedUniformLocation(name);
    if (location != -1) {
        glUniform4fv(location, 1, glm::value_ptr(value));
    }
}

void OpenGLShader::SetVec4(UniformID id, const glm::vec4& value) const {
    GLint location = GetCachedUniformLocation(id);
    if (location != -1) {
        glUniform4fv(location, 1, glm::value_ptr(value));
    }
}

void OpenGLShader::SetVec2(const std::string& name, const glm::vec2& value) const {
    GLint location = GetCachedUniformLocation(name);
    if (location != -1) {
        glUniform2fv(location, 1, glm::value_ptr(value));
    }
}

void OpenGLShader::SetVec2(UniformID id, const glm::vec2& value) const {
    GLint location = GetCachedUniformLocation(id);
    if (location != -1) {
        glUniform2fv(location, 1, glm::value_ptr(value));
    }
}

void OpenGLShader::SetInt64(const std::string& name, int64_t value) const {
    if (!glUniform1i64ARB) {
        if (missingUniformWarnings_.insert("__ext_int64__").second)
            NC::LOGGING::Warning("[SHADER] SetInt64 called but GL_ARB_gpu_shader_int64 is not available Program=", program_.id);
        return;
    }
    GLint location = GetCachedUniformLocation(name);
    if (location != -1) {
        glUniform1i64ARB(location, value);
    }
}

void OpenGLShader::SetInt64(UniformID id, int64_t value) const {
    if (!glUniform1i64ARB) return;
    GLint location = GetCachedUniformLocation(id);
    if (location != -1) {
        glUniform1i64ARB(location, value);
    }
}

void OpenGLShader::SetUint64(const std::string& name, uint64_t value) const {
    if (!glUniform1ui64ARB) {
        if (missingUniformWarnings_.insert("__ext_uint64__").second)
            NC::LOGGING::Warning("[SHADER] SetUint64 called but GL_ARB_gpu_shader_int64 is not available Program=", program_.id);
        return;
    }
    GLint location = GetCachedUniformLocation(name);
    if (location != -1) {
        glUniform1ui64ARB(location, value);
    }
}

void OpenGLShader::SetUint64(UniformID id, uint64_t value) const {
    if (!glUniform1ui64ARB) return;
    GLint location = GetCachedUniformLocation(id);
    if (location != -1) {
        glUniform1ui64ARB(location, value);
    }
}

}
