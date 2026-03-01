// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/DeferredRenderer.h"

void DeferredRenderer::rebuildAnimatedDrawLists() {
    animatedDraws.clear();
    solidShaderVarAnimatedIndices.clear();
    // TODO: animated draw list ownership moved into SceneManager::PrepareDrawLists.
}
