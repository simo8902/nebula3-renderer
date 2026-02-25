// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_TEXTURESERVER_H
#define NDEVC_TEXTURESERVER_H

#include "../NDEVcHeaders.h"
#include "glad/glad.h"

class TextureServer {
public:
    static TextureServer& instance() {
        static TextureServer srv;
        return srv;
    }

    GLuint loadTexture(const std::string& texResId) {
        auto it = gTexCache.find(texResId);
        if (it != gTexCache.end()) {
            if (auto alphaIt = gTexHasTransparency.find(texResId); alphaIt == gTexHasTransparency.end()) {
                std::string actualPath = texResId;
                if (actualPath.starts_with("tex:")) {
                    actualPath = actualPath.substr(4);
                }
                actualPath = TEXTURES_ROOT + actualPath + ".dds";
                auto itPathAlpha = gTexHasTransparency.find(actualPath);
                if (itPathAlpha != gTexHasTransparency.end()) {
                    gTexHasTransparency[texResId] = itPathAlpha->second;
                }
            }
            return it->second;
        }

        std::string actualPath = texResId;
        if (actualPath.starts_with("tex:")) {
            actualPath = actualPath.substr(4);
        }
        actualPath = TEXTURES_ROOT + actualPath + ".dds";

        GLuint tex = LoadDDS(actualPath);
        if (tex == 0) {
            std::cerr << "[TEXTURE][ERROR] Failed to load '" << actualPath << "' (id='" << texResId << "')\n";
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

        std::string actualPath = texResId;
        if (actualPath.starts_with("tex:")) {
            actualPath = actualPath.substr(4);
        }
        actualPath = TEXTURES_ROOT + actualPath + ".dds";

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
            for (auto& [_, tex] : gTexCache) {
                glDeleteTextures(1, &tex);
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

    GLuint LoadDDS(const std::string& path);
};

#endif //NDEVC_TEXTURESERVER_H
