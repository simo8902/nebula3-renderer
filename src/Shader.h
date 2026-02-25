// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_SHADER_H
#define NDEVC_SHADER_H

#include "NDEVcHeaders.h"

#include "glad/glad.h"
#include "GLFW/glfw3.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm.hpp"
#include "gtc/type_ptr.hpp"
#include "gtx/string_cast.hpp"
#include <mutex>
#include <string_view>
#include <cstdint>

namespace NDEVC::Graphics {
    class Shader {
    public:
        using UniformID = uint32_t;

        static constexpr UniformID MakeUniformID(std::string_view name) {
            uint32_t hash = 2166136261u; // FNV-1a 32-bit
            for (char c : name) {
                hash ^= static_cast<uint8_t>(c);
                hash *= 16777619u;
            }
            return hash;
        }

        struct Paths {
            std::string vertex;
            std::string fragment;
        };

        Shader(const char* vertexPath, const char* fragmentPath);
        ~Shader();

        GLuint getProgramID() const { return shaderProgram; }

        void Use() const;
        void reload();
        void reloadFromPath(const std::string& path);

        // Uniform setters
        void setMat4(const std::string& name, const glm::mat4& matrix) const;
        void setMat4(UniformID id, const glm::mat4& matrix) const;
        void setInt(const std::string& name, int value) const;
        void setInt(UniformID id, int value) const;
        void setVec2(const std::string& name, const glm::vec2& value) const;
        void setVec2(UniformID id, const glm::vec2& value) const;
        void setVec3(const std::string& name, const glm::vec3& value) const;
        void setVec3(UniformID id, const glm::vec3& value) const;
        void setVec4(const std::string& name, const glm::vec4& value) const;
        void setVec4(UniformID id, const glm::vec4& value) const;
        void setFloat(const std::string& name, float value) const;
        void setFloat(UniformID id, float value) const;
        void setUint(const std::string& name, uint32_t value) const;
        void setUint(UniformID id, uint32_t value) const;

        void PrecacheUniform(UniformID id, const char* name) const;

        void setMat4Array(const std::string& name, const glm::mat4* values, uint32_t count) const {
            if (!values || count == 0) return;
            GLint location = getCachedUniformLocation(name);
            if (location != -1)
                glUniformMatrix4fv(location, static_cast<GLsizei>(count), GL_FALSE, glm::value_ptr(values[0]));
        }

        void setMat4Array(UniformID id, const glm::mat4* values, uint32_t count) const {
            if (!values || count == 0) return;
            GLint location = getCachedUniformLocation(id);
            if (location != -1)
                glUniformMatrix4fv(location, static_cast<GLsizei>(count), GL_FALSE, glm::value_ptr(values[0]));
        }

        // State management
        bool isValid() const { return shaderProgram != 0; }
        GLuint getProgram() const { return shaderProgram; }
        std::string getLastModified() const;
        const Paths& getPaths() const { return currentPaths; }

        GLuint shaderProgram = 0;

    private:
        GLint getCachedUniformLocation(const std::string& name) const;
        GLint getCachedUniformLocation(UniformID id) const;
        bool isCombined;
        Paths currentPaths;
        mutable std::mutex reloadMutex;
        // Render-thread hot path is lock-free. Uniform caches are expected to be
        // populated and accessed from the render thread.
        mutable std::unordered_map<std::string, GLint> uniformCache;
        mutable std::unordered_map<UniformID, GLint> uniformCacheById;
        mutable std::unordered_map<UniformID, std::string> uniformNamesById;

        void compileAndLink(const std::string& vertexCode, const std::string& fragmentCode);
        GLuint compileShader(const std::string& source, GLenum type);
        std::pair<std::string, std::string> parseCombinedShader(const std::string& path);
        std::string readFile(const std::string& path);
        void checkCompileErrors(GLuint shader, const std::string& type);
        bool checkCompileStatus(GLuint shader, const std::string& type);
        void loadSeparateShaders(const char* vertexPath, const char* fragmentPath);
        void loadCombinedShader(const char* path);
        void checkLinkStatus(GLuint program);
    };
}

#endif //NDEVC_SHADER_H
