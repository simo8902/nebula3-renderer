// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_INPUT_SNAPSHOT_H
#define NDEVC_INPUT_SNAPSHOT_H

struct InputSnapshot {
    bool moveForward   = false;
    bool moveBackward  = false;
    bool moveLeft      = false;
    bool moveRight     = false;
    bool moveUp        = false;
    bool moveDown      = false;
    float mouseRotateX = 0.0f;
    float mouseRotateY = 0.0f;
    float scrollDelta  = 0.0f;
};

#endif
