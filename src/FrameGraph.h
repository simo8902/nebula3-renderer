// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_FRAMEGRAPH_H
#define NDEVC_FRAMEGRAPH_H

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <glad/glad.h>

#include "glm.hpp"
#include "rendering/abstract/IGraphicsDevice.h"

namespace NDEVC::Graphics {

struct RenderTargetDesc {
    std::string name;
    GLenum format;
    int width;
    int height;
    bool isDepth;
    bool isTransient;
};

struct RenderTarget {
    std::shared_ptr<ITexture> texture;
    GLenum format;
    int width;
    int height;
    bool isDepth;
    bool isTransient;
};

class RenderPass {
public:
    std::string name;
    std::vector<std::string> reads;
    std::vector<std::string> writes;
    std::function<void()> execute;

    bool depthTest{true};
    bool depthWrite{true};
    bool blend{false};
    GLenum blendSrc{GL_ONE};
    GLenum blendDst{GL_ZERO};
    bool cullFace{true};
    GLenum cullMode{GL_BACK};
    bool stencilTest{false};
    glm::vec4 clearColor{0,0,0,0};
    bool clearColorBuffer{false};
    bool clearDepth{false};
    bool clearStencil{false};
    bool externalFBO{false};
    std::function<void()> bindExternalFBO;
};

class FrameGraph {
public:
    FrameGraph(IGraphicsDevice* device, int width, int height);
    ~FrameGraph();

    void setDimensions(int width, int height);

    void declareResource(const std::string& name, GLenum format, bool isDepth = false, bool isTransient = false);

    RenderPass& addPass(const std::string& name);

    void compile();
    void execute();

    GLuint getTexture(const std::string& name) const;
    std::shared_ptr<ITexture> getTextureInterface(const std::string& name) const;

private:
    IGraphicsDevice* device_;
    int width_;
    int height_;

    std::unordered_map<std::string, RenderTargetDesc> resourceDescs_;
    std::unordered_map<std::string, RenderTarget> resources_;
    std::vector<std::unique_ptr<RenderPass>> passes_;

    std::unordered_map<std::string, std::shared_ptr<IFramebuffer>> fbos_;

    void createResources();
    void buildFBOs();
    void applyState(const RenderPass& pass);
    BlendFactor glBlendToAbstract(GLenum glBlend);
};

}
#endif