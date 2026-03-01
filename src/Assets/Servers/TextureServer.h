// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_TEXTURESERVER_H
#define NDEVC_TEXTURESERVER_H

#include "Core/Logger.h"
#include "Platform/NDEVcHeaders.h"
#include "glad/glad.h"
#include <unordered_set>

class TextureServer {
public:
    static TextureServer& instance() {
        static TextureServer srv;
        return srv;
    }

    ~TextureServer() { clearCache(true); }

    GLuint loadTexture(const std::string& texResId) {
        auto it = gTexCache.find(texResId);
        if (it != gTexCache.end()) {
            if (auto alphaIt = gTexHasTransparency.find(texResId); alphaIt == gTexHasTransparency.end()) {
                const std::string actualPath = ResolveTexturePath(texResId);
                auto itPathAlpha = gTexHasTransparency.find(actualPath);
                if (itPathAlpha != gTexHasTransparency.end()) {
                    gTexHasTransparency[texResId] = itPathAlpha->second;
                }
            }
            return it->second;
        }

        const std::string actualPath = ResolveTexturePath(texResId);

        GLuint tex = LoadDDS(actualPath);
        if (tex == 0) {
            NC::LOGGING::Error("[TEX] load failed path=", actualPath, " id=", texResId);
            return 0;
        }

        gTexCache[texResId] = tex;
        if (auto alphaIt = gTexHasTransparency.find(actualPath); alphaIt != gTexHasTransparency.end()) {
            gTexHasTransparency[texResId] = alphaIt->second;
        }
        return tex;
    }

    bool hasTransparentPixels(const std::string& texResId) {
        if (auto it = gTexHasTransparency.find(texResId); it != gTexHasTransparency.end()) {
            return it->second;
        }

        const std::string actualPath = ResolveTexturePath(texResId);

        if (auto it = gTexHasTransparency.find(actualPath); it != gTexHasTransparency.end()) {
            gTexHasTransparency[texResId] = it->second;
            return it->second;
        }

        if (loadTexture(texResId) == 0) {
            return false;
        }

        if (auto it = gTexHasTransparency.find(texResId); it != gTexHasTransparency.end()) {
            return it->second;
        }
        if (auto it = gTexHasTransparency.find(actualPath); it != gTexHasTransparency.end()) {
            gTexHasTransparency[texResId] = it->second;
            return it->second;
        }
        return false;
    }

    void clearCache(bool releaseGLObjects = true) {
        if (releaseGLObjects) {
            std::unordered_set<GLuint> released;
            released.reserve(gTexCache.size());
            for (auto& [_, tex] : gTexCache) {
                if (tex == 0) continue;
                if (released.insert(tex).second) {
                    glDeleteTextures(1, &tex);
                }
            }
        }
        gTexCache.clear();
        gTexHasTransparency.clear();
    }

    bool hasCachedTextures() const { return !gTexCache.empty(); }

private:
    TextureServer() = default;
    std::unordered_map<std::string, GLuint> gTexCache;
    std::unordered_map<std::string, bool> gTexHasTransparency;

    static std::string ResolveTexturePath(const std::string& texResId) {
        namespace fs = std::filesystem;
        std::string token = texResId;
        if (token.starts_with("tex:")) {
            token = token.substr(4);
        }
        std::replace(token.begin(), token.end(), '\\', '/');
        fs::path requested(token);

        auto normalizeExtension = [](const fs::path& path) {
            std::string ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ext;
        };
        auto withDDS = [](fs::path path) {
            if (!path.has_extension()) {
                path += ".dds";
            } else {
                path.replace_extension(".dds");
            }
            return path;
        };
        auto existsFile = [](const fs::path& path) {
            std::error_code ec;
            return fs::exists(path, ec) && fs::is_regular_file(path, ec);
        };

        if (requested.is_absolute()) {
            if (!requested.has_extension()) {
                return withDDS(requested).string();
            }
            if (normalizeExtension(requested) == ".dds") {
                return requested.string();
            }
            if (existsFile(requested)) {
                return requested.string();
            }
            return withDDS(requested).string();
        }

        fs::path rootedRequested = fs::path(TEXTURES_ROOT) / requested;
        if (!requested.has_extension()) {
            return withDDS(rootedRequested).string();
        }
        if (normalizeExtension(requested) == ".dds") {
            return rootedRequested.string();
        }
        if (existsFile(rootedRequested)) {
            return rootedRequested.string();
        }

        const fs::path rootedDDS = withDDS(rootedRequested);
        if (existsFile(rootedDDS)) {
            return rootedDDS.string();
        }
        return rootedDDS.string();
    }

    GLuint LoadDDS(const std::string& path);
};

#endif //NDEVC_TEXTURESERVER_H
