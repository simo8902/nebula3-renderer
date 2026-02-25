// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_MDLVIEWER_H
#define NDEVC_MDLVIEWER_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>
#include <memory>
#include <filesystem>
#include "Parser.h"
#include "Model/Model.h"
#include "Camera.h"
#include "glm.hpp"

class MdlViewer {
public:
    MdlViewer();
    ~MdlViewer();

    bool initialize(const std::string& n3Path);
    void run();

private:
    GLFWwindow* window = nullptr;
    int width = 1280;
    int height = 800;

    std::unique_ptr<Model> loadedModel;
    std::string loadedModelPath;

    Camera viewCamera;

    struct ModelStats {
        glm::vec3 boundsMin = glm::vec3(0.0f);
        glm::vec3 boundsMax = glm::vec3(0.0f);
        int nodeCount = 0;
        int meshCount = 0;
        bool loaded = false;
    } modelStats;

    float rotationX = 0.0f;
    float rotationY = 0.0f;
    float zoomDistance = 5.0f;
    bool autoRotate = false;
    float autoRotateSpeed = 0.5f;

    void processInput(double deltaTime);
    void render();
    void renderImGui();
    void computeModelBounds();
    void validateMeshFiles();
    bool initializeImGui();
    void shutdownImGui();
    void cleanup();
};
#endif