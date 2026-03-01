// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_MESHVIEWER_H
#define NDEVC_MESHVIEWER_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include "Rendering/Mesh.h"
#include "glm.hpp"

class MeshViewer {
public:
    MeshViewer();
    ~MeshViewer();

    bool initialize(const std::string& meshPath);
    void run();

private:
    GLFWwindow* window = nullptr;
    int width = 1280;
    int height = 800;

    GLuint whiteTex = 0;

    Mesh loadedMesh;
    std::vector<ObjVertex> originalVerts;
    std::string loadedMeshPath;

    struct MeshStats {
        glm::vec3 boundsMin = glm::vec3(0.0f);
        glm::vec3 boundsMax = glm::vec3(0.0f);
        glm::vec2 uv0Min = glm::vec2(0.0f);
        glm::vec2 uv0Max = glm::vec2(0.0f);
        glm::vec2 uv1Min = glm::vec2(0.0f);
        glm::vec2 uv1Max = glm::vec2(0.0f);
        bool hasVertices = false;
    } meshStats;

    int selectedGroup = 0;
    int selectedVertex = 0;
    bool showUvGrid = true;
    float uvGridScale = 8.0f;
    float uvGridThickness = 0.06f;

    struct UvEditState {
        glm::vec2 sourceUv0Min = glm::vec2(0.0f);
        glm::vec2 sourceUv0Max = glm::vec2(1.0f);
        glm::vec2 sourceUv1Min = glm::vec2(0.0f);
        glm::vec2 sourceUv1Max = glm::vec2(1.0f);
        glm::vec2 targetUv0Min = glm::vec2(0.0f);
        glm::vec2 targetUv0Max = glm::vec2(1.0f);
        glm::vec2 targetUv1Min = glm::vec2(0.0f);
        glm::vec2 targetUv1Max = glm::vec2(1.0f);
        bool initialized = false;
    } uvEdit;

    void processInput(double deltaTime);
    void render();
    void renderImGui();
    void rebuildMeshStats();
    void applyUvRangeEdits();
    void resetUvRangeEdits();
    bool initializeImGui();
    void shutdownImGui();
    void cleanup();
};
#endif