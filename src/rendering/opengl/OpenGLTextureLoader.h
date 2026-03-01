// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_GL_TEXTURE_LOADER_H
#define NDEVC_GL_TEXTURE_LOADER_H

#include "Rendering/Interfaces/ITextureLoader.h"

namespace NDEVC::Graphics::OpenGL {

class OpenGLTextureLoader : public ITextureLoader {
public:
    OpenGLTextureLoader();
    ~OpenGLTextureLoader();

    std::shared_ptr<ITexture> LoadDDS(const std::string& path) override;
    std::shared_ptr<ITexture> CreateWhiteTexture() override;
    std::shared_ptr<ITexture> CreateBlackTexture() override;
    std::shared_ptr<ITexture> CreateNormalTexture() override;

private:
    std::shared_ptr<ITexture> CreateFallbackTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
};

}
#endif