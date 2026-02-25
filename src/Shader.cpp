// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Shader.h"

namespace NDEVC::Graphics {
    Shader::Shader(const char* vertexPath, const char* fragmentPath) : isCombined(false) {
        // std::cerr << "PAIRED_SHADER()" << std::endl;

        if (!std::filesystem::exists(vertexPath)) {
            throw std::runtime_error("Vertex shader file does not exist: " + std::string(vertexPath));
        }
        if (!std::filesystem::exists(fragmentPath)) {
            throw std::runtime_error("Fragment shader file does not exist: " + std::string(fragmentPath));
        }

        compileAndLink(readFile(vertexPath), readFile(fragmentPath));

        currentPaths = {vertexPath, fragmentPath};
    }
    Shader::~Shader() {
        if(shaderProgram != 0) {
            glDeleteProgram(shaderProgram);
        }
    }
    void Shader::loadCombinedShader(const char* path) {
        auto [vertexCode, fragmentCode] = parseCombinedShader(path);
        compileAndLink(vertexCode, fragmentCode);
        currentPaths = {path, path};
        isCombined = true;
    }

    void Shader::loadSeparateShaders(const char* vertexPath, const char* fragmentPath) {
        std::string vertexCode = readFile(vertexPath);
        std::string fragmentCode = readFile(fragmentPath);
        compileAndLink(vertexCode, fragmentCode);
        currentPaths = {vertexPath, fragmentPath};
        isCombined = false;
    }

    std::pair<std::string, std::string> Shader::parseCombinedShader(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filePath);
        }

        std::string line;
        std::string vertexCode;
        std::string fragmentCode;
        bool isVertex = false;
        bool isFragment = false;

        while (std::getline(file, line)) {
            if (line.find("#type vertex") != std::string::npos) {
                isVertex = true;
                isFragment = false;
                continue; // Skip this line
            }
            if (line.find("#type fragment") != std::string::npos) {
                isFragment = true;
                isVertex = false;
                continue; // Skip this line
            }

            if (isVertex) {
                vertexCode += line + "\n";
            } else if (isFragment) {
                fragmentCode += line + "\n";
            }
        }

        return {vertexCode, fragmentCode};

    }


    void Shader::reload() {
        std::scoped_lock lk(reloadMutex);
        if(isCombined) loadCombinedShader(currentPaths.vertex.c_str());
        else loadSeparateShaders(currentPaths.vertex.c_str(), currentPaths.fragment.c_str());
    }

    void Shader::reloadFromPath(const std::string& path) {
        std::scoped_lock lk(reloadMutex);
        if(path.ends_with(".glsl")) loadCombinedShader(path.c_str());
        else if(path.ends_with(".vert")) {
            currentPaths.vertex = path;
            loadSeparateShaders(currentPaths.vertex.c_str(), currentPaths.fragment.c_str());
        }
        else if(path.ends_with(".frag")) {
            currentPaths.fragment = path;
            loadSeparateShaders(currentPaths.vertex.c_str(), currentPaths.fragment.c_str());
        }
    }

    void Shader::compileAndLink(const std::string& vertexCode, const std::string& fragmentCode) {
        GLuint vertexShader = compileShader(vertexCode, GL_VERTEX_SHADER);
        GLuint fragmentShader = compileShader(fragmentCode, GL_FRAGMENT_SHADER);

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
            std::cerr << "ERROR::PROGRAM_LINKING_ERROR\n" << infoLog << "\n";
            glDeleteProgram(newProgram);
            throw std::runtime_error("Shader program linking failed.");
        }

        GLuint old = shaderProgram;
        shaderProgram = newProgram;
        uniformCache.clear();
        uniformCacheById.clear();
        uniformNamesById.clear();
        if (old) glDeleteProgram(old);
    }

    GLuint Shader::compileShader(const std::string& source, GLenum type) {
        if (source.empty()) {
            throw std::runtime_error("Empty shader source");
        }

        const char* src = source.c_str();
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

        if (!success) {
            GLchar infoLog[1024];
            glGetShaderInfoLog(shader, 1024, nullptr, infoLog);

            std::cerr << "ERROR::SHADER_COMPILATION_ERROR of type: "
                      << (type == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT")
                      << "\n" << infoLog << "\n";

            glDeleteShader(shader);
            throw std::runtime_error("Shader compilation failed.");
        } else {
            /*
            std::cout << "Successfully compiled "
                      << (type == GL_VERTEX_SHADER ? "vertex" : "fragment")
                      << " shader.\n";*/
        }

        return shader;
    }


    void Shader::checkCompileErrors(GLuint shader, const std::string& type) {
        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if(!success) {
            GLchar infoLog[1024];
            glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "SHADER COMPILATION ERROR (" << type << "):\n" << infoLog << std::endl;
            glDeleteShader(shader);
        }
    }
    bool Shader::checkCompileStatus(GLuint shader, const std::string& type) {
        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if(!success) {
            GLchar infoLog[512];
            glGetShaderInfoLog(shader, 512, nullptr, infoLog);
            std::cerr << "SHADER COMPILATION ERROR (" << type << "):\n" << infoLog << std::endl;
            return false;
        }
        return true;
    }

    std::string Shader::getLastModified() const {
        std::filesystem::path vertexPath(currentPaths.vertex);
        std::filesystem::path fragmentPath(currentPaths.fragment);

        std::string lastModified;

        try {
            auto vertexTime = std::filesystem::last_write_time(vertexPath);
            auto fragmentTime = std::filesystem::last_write_time(fragmentPath);

            auto systemVertexTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                vertexTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());

            auto systemFragmentTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                fragmentTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());

            auto cftimeVertex = std::chrono::system_clock::to_time_t(systemVertexTime);
            auto cftimeFragment = std::chrono::system_clock::to_time_t(systemFragmentTime);

            std::ostringstream oss;
            oss << "Vertex: " << std::put_time(std::localtime(&cftimeVertex), "%Y-%m-%d %H:%M:%S")
                << ", Fragment: " << std::put_time(std::localtime(&cftimeFragment), "%Y-%m-%d %H:%M:%S");

            lastModified = oss.str();
        } catch (const std::exception& e) {
            lastModified = "Error retrieving time: " + std::string(e.what());
        }

        return lastModified;

    }

    void Shader::checkLinkStatus(GLuint program) {
        GLint success;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if(!success) {
            GLchar infoLog[512];
            glGetProgramInfoLog(program, 512, nullptr, infoLog);
            std::cerr << "SHADER LINKING ERROR:\n" << infoLog << std::endl;
            glDeleteProgram(program);
            throw std::runtime_error("Shader program linking failed");
        }
    }


    std::string Shader::readFile(const std::string& path) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if(!file.is_open()) throw std::runtime_error("Failed to open file: " + path);

        size_t fileSize = file.tellg();
        std::string buffer(fileSize, ' ');
        file.seekg(0);
        file.read(&buffer[0], fileSize);
        file.close();
        return buffer;
    }


    void Shader::Use() const {
        glUseProgram(shaderProgram);
    }

    GLint Shader::getCachedUniformLocation(const std::string& name) const {
        auto it = uniformCache.find(name);
        if (it != uniformCache.end()) {
            return it->second;
        }
        GLint location = glGetUniformLocation(shaderProgram, name.c_str());
        uniformCache[name] = location;
        return location;
    }

    GLint Shader::getCachedUniformLocation(UniformID id) const {
        auto it = uniformCacheById.find(id);
        if (it != uniformCacheById.end()) {
            return it->second;
        }

        auto nameIt = uniformNamesById.find(id);
        if (nameIt == uniformNamesById.end()) {
            return -1;
        }

        GLint location = getCachedUniformLocation(nameIt->second);
        uniformCacheById[id] = location;
        return location;
    }

    void Shader::PrecacheUniform(UniformID id, const char* name) const {
        if (!name || !name[0]) return;

        if (uniformCacheById.find(id) != uniformCacheById.end()) {
            return;
        }

        auto nameIt = uniformNamesById.find(id);
        if (nameIt == uniformNamesById.end()) {
            uniformNamesById[id] = name;
        }

        GLint location = getCachedUniformLocation(uniformNamesById[id]);
        uniformCacheById[id] = location;
    }

    void Shader::setMat4(const std::string& name, const glm::mat4& matrix) const {
        GLint location = getCachedUniformLocation(name);
        if (location != -1) {
            glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
        }
    }

    void Shader::setMat4(UniformID id, const glm::mat4& matrix) const {
        GLint location = getCachedUniformLocation(id);
        if (location != -1) {
            glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
        }
    }

    void Shader::setInt(const std::string& name, int value) const {
        GLint location = getCachedUniformLocation(name);
        if (location != -1) {
            glUniform1i(location, value);
        }
    }

    void Shader::setInt(UniformID id, int value) const {
        GLint location = getCachedUniformLocation(id);
        if (location != -1) {
            glUniform1i(location, value);
        }
    }

    void Shader::setFloat(const std::string& name, float value) const {
        GLint location = getCachedUniformLocation(name);
        if (location != -1) {
            glUniform1f(location, value);
        }
    }

    void Shader::setFloat(UniformID id, float value) const {
        GLint location = getCachedUniformLocation(id);
        if (location != -1) {
            glUniform1f(location, value);
        }
    }

    void Shader::setUint(const std::string& name, uint32_t value) const {
        GLint location = getCachedUniformLocation(name);
        if (location != -1) {
            glUniform1ui(location, value);
        }
    }

    void Shader::setUint(UniformID id, uint32_t value) const {
        GLint location = getCachedUniformLocation(id);
        if (location != -1) {
            glUniform1ui(location, value);
        }
    }

    void Shader::setVec3(const std::string& name, const glm::vec3& value) const {
        GLint location = getCachedUniformLocation(name);
        if (location != -1) {
            glUniform3fv(location, 1, glm::value_ptr(value));
        }
    }

    void Shader::setVec3(UniformID id, const glm::vec3& value) const {
        GLint location = getCachedUniformLocation(id);
        if (location != -1) {
            glUniform3fv(location, 1, glm::value_ptr(value));
        }
    }

    void Shader::setVec4(const std::string& name, const glm::vec4& value) const {
        GLint location = getCachedUniformLocation(name);
        if (location != -1) {
            glUniform4fv(location, 1, glm::value_ptr(value));
        }
    }

    void Shader::setVec4(UniformID id, const glm::vec4& value) const {
        GLint location = getCachedUniformLocation(id);
        if (location != -1) {
            glUniform4fv(location, 1, glm::value_ptr(value));
        }
    }

    void Shader::setVec2(const std::string& name, const glm::vec2& value) const {
        GLint location = getCachedUniformLocation(name);
        if (location != -1) {
            glUniform2fv(location, 1, glm::value_ptr(value));
        }
    }

    void Shader::setVec2(UniformID id, const glm::vec2& value) const {
        GLint location = getCachedUniformLocation(id);
        if (location != -1) {
            glUniform2fv(location, 1, glm::value_ptr(value));
        }
    }
}
