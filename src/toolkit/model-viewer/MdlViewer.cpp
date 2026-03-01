// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "MdlViewer.h"
#include "Logger.h"
#include "Assets/Parser.h"
#include "Rendering/Shader.h"
#include "Assets/NDEVcStructure.h"
#include "Rendering/ValidationLayer.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "gtc/matrix_transform.hpp"
#include <iostream>
#include <algorithm>
#include <limits>
#include <set>

static MdlViewer* g_viewer = nullptr;
static Camera g_camera(
    "ModelCamera",
    glm::vec3(0.0f, 1.0f, 5.0f),
    glm::vec3(0.0f, 0.0f, -1.0f),
    glm::vec3(0.0f, 1.0f, 0.0f),
    0.0f, 0.0f, 35.0f, 0.1f, 45.0f, 0.1f, 1000.0f
);

static void cursor_callback(GLFWwindow* w, double x, double y) {
    if (ImGui::GetCurrentContext()) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) return;
    }

    const bool rmb = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    static float lastX = (float)x, lastY = (float)y;
    static bool wasRmbDown = false;

    if (!rmb) {
        wasRmbDown = false;
        return;
    }

    if (!wasRmbDown) {
        lastX = (float)x;
        lastY = (float)y;
        wasRmbDown = true;
        return;
    }

    float dx = (float)x - lastX;
    float dy = lastY - (float)y;
    lastX = (float)x;
    lastY = (float)y;

    g_camera.processMouseMovement(dx, dy);
}

static void scroll_callback(GLFWwindow* w, double xoff, double yoff) {
    g_camera.processMouseScroll((float)yoff);
}

static void framebuffer_size_callback(GLFWwindow* w, int width, int height) {
    if (width <= 0 || height <= 0) return;
    glViewport(0, 0, width, height);
}

MdlViewer::MdlViewer()
    : viewCamera(
        "ModelViewerCamera",
        glm::vec3(0.0f, 1.0f, 5.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        0.0f, 0.0f, 35.0f, 0.1f, 45.0f, 0.1f, 1000.0f
    ) {
    g_viewer = this;
}

MdlViewer::~MdlViewer() {
    cleanup();
}

bool MdlViewer::initialize(const std::string& n3Path) {
    Logger::Info("Initializing model viewer");

    if (!glfwInit()) {
        Logger::Error("Failed to initialize GLFW");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(width, height, "NDEVC Model Viewer", nullptr, nullptr);
    if (!window) {
        Logger::Error("Failed to create GLFW window");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
    glfwSetWindowUserPointer(window, this);
    glfwSetCursorPosCallback(window, cursor_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        Logger::Error("Failed to load OpenGL functions with GLAD");
        glfwDestroyWindow(window);
        glfwTerminate();
        return false;
    }

    Logger::Info("OpenGL version: " + std::string((const char*)glGetString(GL_VERSION)));

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

    if (!initializeImGui()) {
        Logger::Error("Failed to initialize ImGui");
        cleanup();
        return false;
    }

    Logger::Info("Loading N3 model: " + n3Path);
    loadedModelPath = n3Path;

    try {
        Reporter reporter;
        reporter.currentFile = n3Path;
        Options options;
        options.n3filepath = n3Path;

        loadedModel = std::make_unique<Model>();
        if (!loadedModel->loadFromFile(n3Path, reporter, options)) {
            Logger::Error("Failed to load N3 file");
            cleanup();
            return false;
        }

        validateMeshFiles();
        computeModelBounds();
        modelStats.loaded = true;

        Logger::Info("Model loaded successfully");
        Logger::Info("Bounds min: (" + std::to_string(modelStats.boundsMin.x) + ", " +
                     std::to_string(modelStats.boundsMin.y) + ", " +
                     std::to_string(modelStats.boundsMin.z) + ")");
        Logger::Info("Bounds max: (" + std::to_string(modelStats.boundsMax.x) + ", " +
                     std::to_string(modelStats.boundsMax.y) + ", " +
                     std::to_string(modelStats.boundsMax.z) + ")");
    } catch (const std::exception& e) {
        Logger::Error("Exception loading model: " + std::string(e.what()));
        cleanup();
        return false;
    }

    return true;
}

void MdlViewer::run() {
    Logger::Info("Starting render loop");
    double lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        double deltaTime = currentTime - lastTime;
        lastTime = currentTime;

        processInput(deltaTime);
        render();
        renderImGui();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    Logger::Info("Exiting render loop");
}

void MdlViewer::processInput(double deltaTime) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        g_camera.handleInput(window, (float)deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        g_camera.handleInput(window, (float)deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        g_camera.handleInput(window, (float)deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        g_camera.handleInput(window, (float)deltaTime);
}

void MdlViewer::render() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (autoRotate) {
        rotationY += autoRotateSpeed * 0.016f;
    }
}

void MdlViewer::renderImGui() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Model Viewer", nullptr)) {
        ImGui::Text("Model: %s", loadedModelPath.c_str());
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Model Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Loaded: %s", modelStats.loaded ? "Yes" : "No");
            ImGui::Text("Node Count: %d", modelStats.nodeCount);
            ImGui::Text("Mesh Count: %d", modelStats.meshCount);
            ImGui::Text("Bounds Min: (%.2f, %.2f, %.2f)",
                       modelStats.boundsMin.x, modelStats.boundsMin.y, modelStats.boundsMin.z);
            ImGui::Text("Bounds Max: (%.2f, %.2f, %.2f)",
                       modelStats.boundsMax.x, modelStats.boundsMax.y, modelStats.boundsMax.z);
        }

        if (ImGui::CollapsingHeader("View Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Zoom Distance", &zoomDistance, 0.1f, 50.0f);
            ImGui::SliderFloat("Rotation X", &rotationX, -3.14f, 3.14f);
            ImGui::SliderFloat("Rotation Y", &rotationY, -3.14f, 3.14f);
            ImGui::Checkbox("Auto Rotate", &autoRotate);
            if (autoRotate) {
                ImGui::SliderFloat("Rotation Speed", &autoRotateSpeed, 0.1f, 2.0f);
            }
        }

        if (ImGui::CollapsingHeader("Camera Info")) {
            glm::vec3 pos = g_camera.getPosition();
            glm::vec3 front = g_camera.getForward();
            ImGui::Text("Position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
            ImGui::Text("Forward: (%.2f, %.2f, %.2f)", front.x, front.y, front.z);
        }

        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void MdlViewer::validateMeshFiles() {
    if (!loadedModel) return;

    std::filesystem::path n3FilePath(loadedModelPath);
    std::filesystem::path modelDir = n3FilePath.parent_path();

    std::set<std::string> requiredMeshes;
    auto nodes = loadedModel->getNodes();

    for (const auto* node : nodes) {
        if (!node->mesh_ressource_id.empty()) {
            requiredMeshes.insert(node->mesh_ressource_id);
        }
    }

    for (const auto& meshId : requiredMeshes) {
        std::filesystem::path meshPath = modelDir / meshId;
        if (!std::filesystem::exists(meshPath)) {
            Logger::Error("Missing mesh file: " + meshPath.string());
        }
    }

    if (!requiredMeshes.empty()) {
        Logger::Info("Validated " + std::to_string(requiredMeshes.size()) + " required mesh file(s)");
    }
}

void MdlViewer::computeModelBounds() {
    if (!loadedModel) return;

    modelStats.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    modelStats.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    modelStats.nodeCount = 0;
    modelStats.meshCount = 0;

    Logger::Info("Computing model bounds");
}

bool MdlViewer::initializeImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO();

    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        Logger::Error("Failed to initialize ImGui GLFW backend");
        return false;
    }

    const char* glsl_version = "#version 460 core";
    if (!ImGui_ImplOpenGL3_Init(glsl_version)) {
        Logger::Error("Failed to initialize ImGui OpenGL backend");
        return false;
    }

    Logger::Info("ImGui initialized successfully");
    return true;
}

void MdlViewer::shutdownImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void MdlViewer::cleanup() {
    Logger::Info("Cleaning up");

    shutdownImGui();

    if (window) {
        glfwDestroyWindow(window);
    }

    glfwTerminate();
}
