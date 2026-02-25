// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_GLSTATEDEBUG_H
#define NDEVC_GLSTATEDEBUG_H

#include "glad/glad.h"
#include <iostream>
#include <iomanip>

inline void DumpGLState(const char* label) {
    std::cout << "\n========== GL STATE: " << label << " ==========\n";

    GLint iFBO, iProgram, iVAO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &iFBO);
    glGetIntegerv(GL_CURRENT_PROGRAM, &iProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &iVAO);

    GLboolean bDepth, bBlend, bCull, bStencil;
    glGetBooleanv(GL_DEPTH_TEST, &bDepth);
    glGetBooleanv(GL_BLEND, &bBlend);
    glGetBooleanv(GL_CULL_FACE, &bCull);
    glGetBooleanv(GL_STENCIL_TEST, &bStencil);

    GLint depthFunc, cullFace, blendSrc, blendDst;
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    glGetIntegerv(GL_CULL_FACE_MODE, &cullFace);
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDst);

    GLboolean depthMask, colorMask[4];
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    std::cout << "  FBO=" << iFBO << " Program=" << iProgram << " VAO=" << iVAO << "\n";
    std::cout << "  Viewport: [" << viewport[0] << "," << viewport[1] << "," << viewport[2] << "," << viewport[3] << "]\n";
    std::cout << "  Tests: Depth=" << (int)bDepth << " Blend=" << (int)bBlend
              << " Cull=" << (int)bCull << " Stencil=" << (int)bStencil << "\n";
    std::cout << "  DepthFunc=0x" << std::hex << depthFunc
              << " CullFace=0x" << cullFace << std::dec << "\n";
    std::cout << "  Blend: Src=0x" << std::hex << blendSrc
              << " Dst=0x" << blendDst << std::dec << "\n";
    std::cout << "  Masks: Depth=" << (int)depthMask
              << " Color=[" << (int)colorMask[0] << (int)colorMask[1]
              << (int)colorMask[2] << (int)colorMask[3] << "]\n";

    GLint boundTex[8];
    for (int i = 0; i < 8; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTex[i]);
    }
    std::cout << "  Textures: ";
    for (int i = 0; i < 8; i++) {
        if (boundTex[i]) std::cout << i << "=" << boundTex[i] << " ";
    }
    std::cout << "\n";

    std::cout << "========================================\n";
}
#endif