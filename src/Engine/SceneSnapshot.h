// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_SCENE_SNAPSHOT_H
#define NDEVC_SCENE_SNAPSHOT_H

#include <cstdint>
#include "glm.hpp"

// ── RenderProxy flags ─────────────────────────────────────────────────────
// Maps to the draw-list categories in SceneManager::PrepareDrawLists().
// Phase 3 will populate these from DrawCmd classifications.
enum RenderProxyFlags : uint32_t {
    kProxyAlphaTest   = 1u << 0,
    kProxyDecal       = 1u << 1,
    kProxyStatic      = 1u << 2,
    kProxyDisabled    = 1u << 3,
    kProxyEnvironment = 1u << 4,
    kProxyRefraction  = 1u << 5,
    kProxyWater       = 1u << 6,
    kProxySimpleLayer = 1u << 7,
};

// ── RenderProxy ───────────────────────────────────────────────────────────
// Per-draw POD unit written into the FrameArena by SceneManager.
// Raw pointers (Mesh*, ITexture*) live in DrawCmd until Phase 3 migrates
// them to handles resolved by ResourceServer.
struct RenderProxy {
    glm::mat4 worldMatrix;       // model-to-world transform
    uint32_t  modelId;           // ResourceServer handle (UINT32_MAX = invalid)
    uint32_t  materialId;        // ResourceServer handle (UINT32_MAX = invalid)
    uint32_t  megaVertexOffset;  // byte offset into the MegaBuffer vertex region
    uint32_t  megaIndexOffset;   // byte offset into the MegaBuffer index region
    uint32_t  flags;             // RenderProxyFlags bitmask
};
static_assert(sizeof(RenderProxy) == 84, "RenderProxy layout changed");

// ── SceneSnapshot ─────────────────────────────────────────────────────────
// Non-owning view into a FrameArena allocation produced at the end of
// SceneManager::Tick(). Consumed by the render thread the same frame
// (Phase 5) or immediately (current single-threaded path).
// `proxies` points into the producing FrameArena — the arena must outlive
// any consumer of this snapshot.
struct SceneSnapshot {
    const RenderProxy* proxies;   // arena-allocated span (non-owning)
    uint32_t           count;
    uint32_t           _pad;
    glm::vec3          cameraPos;
    float              _pad2;
    glm::mat4          view;
    glm::mat4          projection;
};

#endif
