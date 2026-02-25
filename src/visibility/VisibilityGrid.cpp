// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "VisibilityGrid.h"

#include <algorithm>
#include <cmath>
#include <limits>

void VisibilityGrid::Build(const std::vector<DrawCmd>& draws, const MapInfo& info) {
    Clear();

    gridW_ = info.map_size_x > 0 ? info.map_size_x : 32;
    gridH_ = info.map_size_z > 0 ? info.map_size_z : 32;
    cellSizeX_ = info.grid_size.x > 0.0f ? info.grid_size.x : 32.0f;
    cellSizeZ_ = info.grid_size.z > 0.0f ? info.grid_size.z : 32.0f;

    gridOriginXZ_ = glm::vec2(info.center.x - info.extents.x,
                              info.center.z - info.extents.z);

    cells_.resize(static_cast<size_t>(gridW_) * gridH_);
    for (int z = 0; z < gridH_; ++z) {
        for (int x = 0; x < gridW_; ++x) {
            VisibilityCell& c = cells_[z * gridW_ + x];
            c.minXZ = gridOriginXZ_ + glm::vec2(x * cellSizeX_, z * cellSizeZ_);
            c.maxXZ = c.minXZ + glm::vec2(cellSizeX_, cellSizeZ_);
            c.minY =  1e9f;
            c.maxY = -1e9f;
        }
    }

    for (uint32_t i = 0; i < static_cast<uint32_t>(draws.size()); ++i) {
        const DrawCmd& dc = draws[i];

        const glm::vec3 fallbackPos(dc.worldMatrix[3]);
        glm::vec3 worldMin = fallbackPos;
        glm::vec3 worldMax = fallbackPos;

        const glm::vec3 localMin(dc.localBoxMin);
        const glm::vec3 localMax(dc.localBoxMax);
        const glm::vec3 localExtent = localMax - localMin;
        const bool hasLocalBounds =
            std::isfinite(localMin.x) && std::isfinite(localMin.y) && std::isfinite(localMin.z) &&
            std::isfinite(localMax.x) && std::isfinite(localMax.y) && std::isfinite(localMax.z) &&
            (std::abs(localExtent.x) > 1e-5f || std::abs(localExtent.y) > 1e-5f || std::abs(localExtent.z) > 1e-5f);

        if (hasLocalBounds) {
            const glm::vec3 corners[8] = {
                {localMin.x, localMin.y, localMin.z},
                {localMax.x, localMin.y, localMin.z},
                {localMin.x, localMax.y, localMin.z},
                {localMax.x, localMax.y, localMin.z},
                {localMin.x, localMin.y, localMax.z},
                {localMax.x, localMin.y, localMax.z},
                {localMin.x, localMax.y, localMax.z},
                {localMax.x, localMax.y, localMax.z}
            };

            worldMin = glm::vec3(std::numeric_limits<float>::max());
            worldMax = glm::vec3(std::numeric_limits<float>::lowest());
            for (const glm::vec3& c : corners) {
                const glm::vec3 w = glm::vec3(dc.worldMatrix * glm::vec4(c, 1.0f));
                worldMin = glm::min(worldMin, w);
                worldMax = glm::max(worldMax, w);
            }

            const bool boundsFinite =
                std::isfinite(worldMin.x) && std::isfinite(worldMin.y) && std::isfinite(worldMin.z) &&
                std::isfinite(worldMax.x) && std::isfinite(worldMax.y) && std::isfinite(worldMax.z);
            if (!boundsFinite) {
                worldMin = fallbackPos;
                worldMax = fallbackPos;
            }
        }

        int cxMin = static_cast<int>(std::floor((worldMin.x - gridOriginXZ_.x) / cellSizeX_));
        int cxMax = static_cast<int>(std::floor((worldMax.x - gridOriginXZ_.x) / cellSizeX_));
        int czMin = static_cast<int>(std::floor((worldMin.z - gridOriginXZ_.y) / cellSizeZ_));
        int czMax = static_cast<int>(std::floor((worldMax.z - gridOriginXZ_.y) / cellSizeZ_));

        cxMin = std::clamp(cxMin, 0, gridW_ - 1);
        cxMax = std::clamp(cxMax, 0, gridW_ - 1);
        czMin = std::clamp(czMin, 0, gridH_ - 1);
        czMax = std::clamp(czMax, 0, gridH_ - 1);

        for (int cz = czMin; cz <= czMax; ++cz) {
            for (int cx = cxMin; cx <= cxMax; ++cx) {
                VisibilityCell& cell = cells_[cz * gridW_ + cx];
                cell.drawIndices.push_back(i);
                cell.minY = std::min(cell.minY, worldMin.y);
                cell.maxY = std::max(cell.maxY, worldMax.y);
            }
        }
    }

    isBuilt_ = true;
}

void VisibilityGrid::Clear() {
    cells_.clear();
    gridW_ = gridH_ = 0;
    isBuilt_ = false;
    lastVisibleCellCount_ = 0;
    lastVisibleDrawCount_ = 0;
}

bool VisibilityGrid::IsCellVisible(const VisibilityCell& cell,
                                   const glm::vec3& camPos,
                                   const Camera::Frustum& frustum,
                                   float visRange) const {
    if (cell.drawIndices.empty()) return false;

    const float yMin = (cell.minY <=  1e8f) ? cell.minY : -1.0f;
    const float yMax = (cell.maxY >= -1e8f) ? cell.maxY :  1.0f;

    const glm::vec3 boxMin(cell.minXZ.x, yMin, cell.minXZ.y);
    const glm::vec3 boxMax(cell.maxXZ.x, yMax, cell.maxXZ.y);
    const glm::vec3 boxCenter = (boxMin + boxMax) * 0.5f;

    if (visRange > 0.0f) {
        const float dx = boxCenter.x - camPos.x;
        const float dz = boxCenter.z - camPos.z;
        const float halfDiag = glm::length(glm::vec2(boxMax.x - boxMin.x,
                                                      boxMax.z - boxMin.z)) * 0.5f;
        if (std::sqrt(dx * dx + dz * dz) > visRange + halfDiag) return false;
    }

    for (int i = 0; i < 6; ++i) {
        const Camera::Plane& plane = frustum.planes[i];
        const glm::vec3 pv(
            plane.normal.x >= 0.0f ? boxMax.x : boxMin.x,
            plane.normal.y >= 0.0f ? boxMax.y : boxMin.y,
            plane.normal.z >= 0.0f ? boxMax.z : boxMin.z
        );
        if (glm::dot(plane.normal, pv) + plane.d < 0.0f) return false;
    }

    return true;
}

void VisibilityGrid::QueryVisibleCells(const glm::vec3& camPos,
                                       const Camera::Frustum& frustum,
                                       float visRange,
                                       std::vector<int>& outCellIndices) const {
    outCellIndices.clear();
    if (!isBuilt_) return;

    int xMin = 0, xMax = gridW_ - 1;
    int zMin = 0, zMax = gridH_ - 1;

    if (visRange > 0.0f) {
        const int camCellX = static_cast<int>((camPos.x - gridOriginXZ_.x) / cellSizeX_);
        const int camCellZ = static_cast<int>((camPos.z - gridOriginXZ_.y) / cellSizeZ_);
        const int rangeCellsX = static_cast<int>(std::ceil(visRange / cellSizeX_)) + 1;
        const int rangeCellsZ = static_cast<int>(std::ceil(visRange / cellSizeZ_)) + 1;
        xMin = std::max(0,         camCellX - rangeCellsX);
        xMax = std::min(gridW_ - 1, camCellX + rangeCellsX);
        zMin = std::max(0,         camCellZ - rangeCellsZ);
        zMax = std::min(gridH_ - 1, camCellZ + rangeCellsZ);
    }

    for (int z = zMin; z <= zMax; ++z) {
        for (int x = xMin; x <= xMax; ++x) {
            const int idx = z * gridW_ + x;
            if (IsCellVisible(cells_[idx], camPos, frustum, visRange)) {
                outCellIndices.push_back(idx);
            }
        }
    }

    std::sort(outCellIndices.begin(), outCellIndices.end());

    lastVisibleCellCount_ = static_cast<int>(outCellIndices.size());
}

bool VisibilityGrid::UpdateVisibility(std::vector<DrawCmd>& draws,
                                      const std::vector<int>& visibleCellIndices) {
    if (draws.empty()) return false;

    // Build a per-draw visibility bitset (true = should be enabled).
    std::vector<bool> newVisible(draws.size(), false);
    for (int ci : visibleCellIndices) {
        if (ci < 0 || ci >= static_cast<int>(cells_.size())) continue;
        for (uint32_t idx : cells_[ci].drawIndices) {
            if (idx < static_cast<uint32_t>(draws.size())) {
                newVisible[idx] = true;
            }
        }
    }

    bool changed = false;
    int visCount = 0;
    for (size_t i = 0; i < draws.size(); ++i) {
        const bool shouldBeEnabled = newVisible[i];
        const bool currentlyEnabled = !draws[i].disabled;
        if (shouldBeEnabled != currentlyEnabled) {
            draws[i].disabled = !shouldBeEnabled;
            changed = true;
        }
        if (shouldBeEnabled) ++visCount;
    }

    lastVisibleDrawCount_ = visCount;
    return changed;
}