// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_INTERNAL_VISIBILITY_STAGE_H
#define NDEVC_INTERNAL_VISIBILITY_STAGE_H

#include "Rendering/DrawCmd.h"
#include "Rendering/Visibility/VisibilityGrid.h"

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <unordered_set>
#include <vector>

struct VisibilityQueryInput {
    const VisibilityGrid* grid = nullptr;
    const std::vector<DrawCmd>* draws = nullptr;
};

class InternalVisibilityChecker {
public:
    void ClearVisibilityLinks();
    void PerformVisibilityQuery(const std::vector<int>& visibleCellIndices,
                                const VisibilityGrid& grid,
                                const std::vector<DrawCmd>& draws);
    bool HasCameraLink(const void* owner) const;
    void Reset();

private:
    std::unordered_set<const void*> cameraLinkedOwners_;
};

class InternalVisibilityStage {
public:
    void OnCullBefore(std::uint64_t frameIndex);
    void UpdateCameraLinks(const std::vector<int>& visibleCellIndices,
                           std::initializer_list<VisibilityQueryInput> sources);
    bool ResolveDrawVisibility(std::vector<DrawCmd>& draws) const;
    void Reset();

    InternalVisibilityChecker& GetVisibilityChecker();
    const InternalVisibilityChecker& GetVisibilityChecker() const;

private:
    std::uint64_t curFrameIndex_ = std::numeric_limits<std::uint64_t>::max();
    InternalVisibilityChecker visibilityChecker_;
};

#endif
