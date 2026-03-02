// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/DeferredRenderer.h"

void DeferredRenderer::rebuildAnimatedDrawLists() {
    animatedDraws.clear();
    auto collectAnimated = [this](std::vector<DrawCmd>& draws) {
        for (auto& dc : draws) {
            if (dc.hasPotentialTransformAnimation || dc.hasShaderVarAnimations) {
                animatedDraws.push_back(&dc);
            }
        }
    };
    collectAnimated(solidDraws);
    collectAnimated(alphaTestDraws);
    collectAnimated(decalDraws);
    collectAnimated(environmentDraws);
    collectAnimated(environmentAlphaDraws);
    collectAnimated(simpleLayerDraws);
    collectAnimated(refractionDraws);
    collectAnimated(postAlphaUnlitDraws);
    collectAnimated(waterDraws);
    collectAnimated(particleDraws);

    solidShaderVarAnimatedIndicesDirty_ = true;
}
