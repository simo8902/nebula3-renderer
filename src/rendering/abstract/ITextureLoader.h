// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_ITEXTURELOADER_H
#define NDEVC_ITEXTURELOADER_H

#include <string>
#include <memory>
#include "ITexture.h"

namespace NDEVC::Graphics {

class ITextureLoader {
public:
    virtual ~ITextureLoader() = default;
    virtual std::shared_ptr<ITexture> LoadDDS(const std::string& path) = 0;
    virtual std::shared_ptr<ITexture> CreateWhiteTexture() = 0;
    virtual std::shared_ptr<ITexture> CreateBlackTexture() = 0;
    virtual std::shared_ptr<ITexture> CreateNormalTexture() = 0;
};

}
#endif