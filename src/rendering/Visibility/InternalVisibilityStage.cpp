// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/Visibility/InternalVisibilityStage.h"

void InternalVisibilityChecker::ClearVisibilityLinks() {
    cameraLinkedOwners_.clear();
}

void InternalVisibilityChecker::PerformVisibilityQuery(const std::vector<int>& visibleCellIndices,
                                                       const VisibilityGrid& grid,
                                                       const std::vector<DrawCmd>& draws) {
    if (!grid.IsBuilt()) return;

    const auto& cells = grid.GetCells();
    for (int cellIndex : visibleCellIndices) {
        if (cellIndex < 0 || cellIndex >= static_cast<int>(cells.size())) continue;
        const VisibilityCell& cell = cells[cellIndex];
        for (uint32_t drawIndex : cell.drawIndices) {
            if (drawIndex >= static_cast<uint32_t>(draws.size())) continue;
            const DrawCmd& draw = draws[drawIndex];
            if (draw.instance) {
                cameraLinkedOwners_.insert(draw.instance);
            }
        }
    }
}

bool InternalVisibilityChecker::HasCameraLink(const void* owner) const {
    if (!owner) return false;
    return cameraLinkedOwners_.contains(owner);
}

void InternalVisibilityChecker::Reset() {
    cameraLinkedOwners_.clear();
}

void InternalVisibilityStage::OnCullBefore(std::uint64_t frameIndex) {
    if (curFrameIndex_ == frameIndex) return;
    curFrameIndex_ = frameIndex;
}

void InternalVisibilityStage::UpdateCameraLinks(const std::vector<int>& visibleCellIndices,
                                                std::initializer_list<VisibilityQueryInput> sources) {
    visibilityChecker_.ClearVisibilityLinks();
    for (const VisibilityQueryInput& source : sources) {
        if (!source.grid || !source.draws) continue;
        visibilityChecker_.PerformVisibilityQuery(visibleCellIndices, *source.grid, *source.draws);
    }
}

bool InternalVisibilityStage::ResolveDrawVisibility(std::vector<DrawCmd>& draws) const {
    bool changed = false;
    for (DrawCmd& draw : draws) {
        bool visibleByCameraLink = true;
        if (draw.isStatic && draw.instance) {
            visibleByCameraLink = visibilityChecker_.HasCameraLink(draw.instance);
        }

        const bool shouldEnable = visibleByCameraLink && !draw.userDisabled;
        const bool shouldDisable = !shouldEnable;
        if (draw.disabled != shouldDisable) {
            draw.disabled = shouldDisable;
            changed = true;
        }
        if (shouldDisable) {
            // Placeholder if anything else needs to react to visibility changes
        }
    }
    return changed;
}

void InternalVisibilityStage::Reset() {
    curFrameIndex_ = std::numeric_limits<std::uint64_t>::max();
    visibilityChecker_.Reset();
}

InternalVisibilityChecker& InternalVisibilityStage::GetVisibilityChecker() {
    return visibilityChecker_;
}

const InternalVisibilityChecker& InternalVisibilityStage::GetVisibilityChecker() const {
    return visibilityChecker_;
}

