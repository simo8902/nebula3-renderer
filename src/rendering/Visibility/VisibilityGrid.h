// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_VISIBILITY_GRID_H
#define NDEVC_VISIBILITY_GRID_H

#include "glm.hpp"
#include "Rendering/Camera.h"
#include "Rendering/DrawCmd.h"
#include "Assets/Map/MapHeader.h"

#include <vector>
#include <cstdint>

struct VisibilityCell {
    glm::vec2 minXZ{ 0.0f };
    glm::vec2 maxXZ{ 0.0f };
    float minY =  1e9f;
    float maxY = -1e9f;
    std::vector<uint32_t> drawIndices;
};

class VisibilityGrid {
public:
    VisibilityGrid() = default;

    void Build(const std::vector<DrawCmd>& draws, const MapInfo& info);

    void Clear();

    void QueryVisibleCells(const glm::vec3& camPos,
                           float visRange,
                           std::vector<int>& outCellIndices) const;

    bool UpdateVisibility(std::vector<DrawCmd>& draws,
                          const std::vector<int>& visibleCellIndices);

    bool IsBuilt() const { return isBuilt_; }
    int GetTotalCellCount() const { return gridW_ * gridH_; }
    int GetLastVisibleCellCount() const { return lastVisibleCellCount_; }
    int GetLastVisibleDrawCount() const { return lastVisibleDrawCount_; }
    float GetCellSizeX() const { return cellSizeX_; }
    float GetCellSizeZ() const { return cellSizeZ_; }
    const std::vector<VisibilityCell>& GetCells() const { return cells_; }

private:
    bool IsCellVisible(const VisibilityCell& cell,
                       const glm::vec3& camPos,
                       float visRange) const;

    std::vector<VisibilityCell> cells_;
    glm::vec2 gridOriginXZ_{ 0.0f };
    float cellSizeX_ = 32.0f;
    float cellSizeZ_ = 32.0f;
    int gridW_ = 0;
    int gridH_ = 0;
    bool isBuilt_ = false;

    mutable int lastVisibleCellCount_ = 0;
    mutable int lastVisibleDrawCount_ = 0;
};
#endif