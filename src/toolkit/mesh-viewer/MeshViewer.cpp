// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "MeshViewer.h"
#include "Rendering/Mesh.h"
#include "Rendering/Shader.h"
#include "Rendering/Camera.h"
#include "Assets/NDEVcStructure.h"
#include "Rendering/ValidationLayer.h"
#include "Core/GlobalState.h"
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <iostream>
#include <memory>
#include <limits>
#include <algorithm>
#include <cfloat>
#include <cmath>

static std::unique_ptr<NDEVC::Graphics::Shader> gMeshViewerShader;
static std::unique_ptr<NDEVC::Graphics::Shader> gMeshViewerPointShader;

static Camera meshCamera(
    "MeshCamera",
    glm::vec3(0.0f, 0.0f, 5.0f),
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
    static bool first = true;
    static bool wasRmbDown = false;

    if (!rmb) {
        wasRmbDown = false;
        first = true;
        return;
    }

    if (!wasRmbDown) {
        lastX = (float)x;
        lastY = (float)y;
        wasRmbDown = true;
        first = false;
        return;
    }

    if (first) {
        lastX = (float)x;
        lastY = (float)y;
        first = false;
        return;
    }

    float dx = (float)x - lastX;
    float dy = lastY - (float)y;
    lastX = (float)x;
    lastY = (float)y;

    meshCamera.processMouseMovement(dx, dy);
}

static void scroll_callback(GLFWwindow* w, double xoff, double yoff) {
    meshCamera.processMouseScroll((float)yoff);
}

static void framebuffer_size_callback(GLFWwindow* w, int width, int height) {
    auto* viewer = reinterpret_cast<MeshViewer*>(glfwGetWindowUserPointer(w));
    if (!viewer || width <= 0 || height <= 0) return;
    glViewport(0, 0, width, height);
}

MeshViewer::MeshViewer() {}

MeshViewer::~MeshViewer() {
    cleanup();
}

bool MeshViewer::initialize(const std::string& meshPath) {
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(width, height, "NDEVC Mesh Viewer", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
    glfwSetWindowUserPointer(window, this);
    glfwSetCursorPosCallback(window, cursor_callback);
    glfwSetScrollCallback(window, scroll_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to init GLAD\n";
        return false;
    }

    if (!initializeImGui()) {
        std::cerr << "Failed to initialize ImGui\n";
        return false;
    }

    EnableAnsiColors();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    gMeshViewerShader = std::make_unique<NDEVC::Graphics::Shader>(
        SOURCE_DIR "/toolkit/mesh-viewer/shaders/mesh_viewer.vert",
        SOURCE_DIR "/toolkit/mesh-viewer/shaders/mesh_viewer.frag"
    );
    if (!gMeshViewerShader || !gMeshViewerShader->isValid()) {
        std::cerr << "Failed to create mesh viewer shader\n";
        return false;
    }
    gMeshViewerPointShader = std::make_unique<NDEVC::Graphics::Shader>(
        SOURCE_DIR "/toolkit/mesh-viewer/shaders/mesh_viewer_point.vert",
        SOURCE_DIR "/toolkit/mesh-viewer/shaders/mesh_viewer_point.frag"
    );
    if (!gMeshViewerPointShader || !gMeshViewerPointShader->isValid()) {
        std::cerr << "Failed to create mesh viewer point shader\n";
        return false;
    }

    unsigned char grey[] = {180, 180, 180, 255};
    glGenTextures(1, &whiteTex);
    glBindTexture(GL_TEXTURE_2D, whiteTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, grey);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    loadedMeshPath = meshPath;
    if (!Mesh::LoadNVX2(meshPath, loadedMesh)) {
        std::cerr << "Failed to load mesh\n";
        return false;
    }
    originalVerts = loadedMesh.verts;

    Mesh::SetupVAO(loadedMesh);
    rebuildMeshStats();
    resetUvRangeEdits();

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);
    width = fbw;
    height = fbh;

    std::cout << "Mesh loaded: " << loadedMesh.groups.size() << " groups, " << loadedMesh.verts.size() << " vertices\n";

    return true;
}

void MeshViewer::processInput(double deltaTime) {
    if (!ImGui::GetIO().WantCaptureKeyboard) {
        meshCamera.handleInput(window, (float)deltaTime);
    }

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

void MeshViewer::render() {
    if (!gMeshViewerShader || !gMeshViewerShader->isValid()) {
        std::cerr << "Shader not found\n";
        return;
    }

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    width = (fbw > 0) ? fbw : width;
    height = (fbh > 0) ? fbh : height;

    gMeshViewerShader->Use();
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 view = meshCamera.getViewMatrix();
    glm::mat4 projection = glm::perspective(glm::radians(meshCamera.getFov()), (float)width / (float)height, 0.1f, 1000.0f);

    gMeshViewerShader->setMat4("model", model);
    gMeshViewerShader->setMat4("view", view);
    gMeshViewerShader->setMat4("projection", projection);
    gMeshViewerShader->setVec3("cameraPos", meshCamera.getPosition());
    gMeshViewerShader->setVec3("baseColor", glm::vec3(0.7f, 0.7f, 0.7f));
    gMeshViewerShader->setInt("showUvGrid", showUvGrid ? 1 : 0);
    gMeshViewerShader->setFloat("uvGridScale", uvGridScale);
    gMeshViewerShader->setFloat("uvGridThickness", uvGridThickness);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, whiteTex);
    gMeshViewerShader->setInt("diffuseTex", 0);

    glBindVertexArray(loadedMesh.vao);
    glDrawElements(GL_TRIANGLES, (GLsizei)loadedMesh.idx.size(), GL_UNSIGNED_INT, 0);

    if (!loadedMesh.verts.empty()) {
        selectedVertex = std::clamp(selectedVertex, 0, (int)loadedMesh.verts.size() - 1);
        gMeshViewerPointShader->Use();
        gMeshViewerPointShader->setMat4("model", model);
        gMeshViewerPointShader->setMat4("view", view);
        gMeshViewerPointShader->setMat4("projection", projection);
        gMeshViewerPointShader->setVec3("pointColor", glm::vec3(1.0f, 0.08f, 0.08f));
        gMeshViewerPointShader->setFloat("pointSize", 13.0f);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glDisable(GL_CULL_FACE);
        glDrawArrays(GL_POINTS, selectedVertex, 1);
        glEnable(GL_CULL_FACE);
    }
    glBindVertexArray(0);

    renderImGui();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void MeshViewer::run() {
    double lastFrame = 0.0;

    while (!glfwWindowShouldClose(window)) {
        double currentFrame = glfwGetTime();
        double deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(deltaTime);
        render();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }
}

void MeshViewer::cleanup() {
    if (loadedMesh.vao) glDeleteVertexArrays(1, &loadedMesh.vao);
    if (loadedMesh.vbo) glDeleteBuffers(1, &loadedMesh.vbo);
    if (loadedMesh.ebo) glDeleteBuffers(1, &loadedMesh.ebo);
    if (loadedMesh.indirect_buffer) glDeleteBuffers(1, &loadedMesh.indirect_buffer);
    if (whiteTex) glDeleteTextures(1, &whiteTex);
    shutdownImGui();
    gMeshViewerShader.reset();
    gMeshViewerPointShader.reset();

    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
        glfwTerminate();
    }
}

bool MeshViewer::initializeImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    g_ImGuiContext = ImGui::GetCurrentContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    imguieffects();

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) return false;
    if (!ImGui_ImplOpenGL3_Init("#version 460")) return false;
    return true;
}

void MeshViewer::shutdownImGui() {
    if (!ImGui::GetCurrentContext()) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    g_ImGuiContext = nullptr;
}

void MeshViewer::rebuildMeshStats() {
    if (loadedMesh.verts.empty()) {
        meshStats.hasVertices = false;
        return;
    }

    meshStats.hasVertices = true;
    meshStats.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    meshStats.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    meshStats.uv0Min = glm::vec2(std::numeric_limits<float>::max());
    meshStats.uv0Max = glm::vec2(std::numeric_limits<float>::lowest());
    meshStats.uv1Min = glm::vec2(std::numeric_limits<float>::max());
    meshStats.uv1Max = glm::vec2(std::numeric_limits<float>::lowest());

    for (const auto& v : loadedMesh.verts) {
        meshStats.boundsMin.x = std::min(meshStats.boundsMin.x, v.px);
        meshStats.boundsMin.y = std::min(meshStats.boundsMin.y, v.py);
        meshStats.boundsMin.z = std::min(meshStats.boundsMin.z, v.pz);
        meshStats.boundsMax.x = std::max(meshStats.boundsMax.x, v.px);
        meshStats.boundsMax.y = std::max(meshStats.boundsMax.y, v.py);
        meshStats.boundsMax.z = std::max(meshStats.boundsMax.z, v.pz);

        meshStats.uv0Min.x = std::min(meshStats.uv0Min.x, v.u0);
        meshStats.uv0Min.y = std::min(meshStats.uv0Min.y, v.v0);
        meshStats.uv0Max.x = std::max(meshStats.uv0Max.x, v.u0);
        meshStats.uv0Max.y = std::max(meshStats.uv0Max.y, v.v0);

        meshStats.uv1Min.x = std::min(meshStats.uv1Min.x, v.u1);
        meshStats.uv1Min.y = std::min(meshStats.uv1Min.y, v.v1);
        meshStats.uv1Max.x = std::max(meshStats.uv1Max.x, v.u1);
        meshStats.uv1Max.y = std::max(meshStats.uv1Max.y, v.v1);
    }
}

static float remapValue(float v, float srcMin, float srcMax, float dstMin, float dstMax) {
    const float srcRange = srcMax - srcMin;
    if (std::fabs(srcRange) < 1e-8f) return dstMin;
    const float t = (v - srcMin) / srcRange;
    return dstMin + t * (dstMax - dstMin);
}

void MeshViewer::applyUvRangeEdits() {
    if (loadedMesh.verts.empty() || originalVerts.size() != loadedMesh.verts.size()) return;

    for (size_t i = 0; i < loadedMesh.verts.size(); ++i) {
        const ObjVertex& src = originalVerts[i];
        ObjVertex& dst = loadedMesh.verts[i];

        dst.u0 = remapValue(src.u0, uvEdit.sourceUv0Min.x, uvEdit.sourceUv0Max.x, uvEdit.targetUv0Min.x, uvEdit.targetUv0Max.x);
        dst.v0 = remapValue(src.v0, uvEdit.sourceUv0Min.y, uvEdit.sourceUv0Max.y, uvEdit.targetUv0Min.y, uvEdit.targetUv0Max.y);
        dst.u1 = remapValue(src.u1, uvEdit.sourceUv1Min.x, uvEdit.sourceUv1Max.x, uvEdit.targetUv1Min.x, uvEdit.targetUv1Max.x);
        dst.v1 = remapValue(src.v1, uvEdit.sourceUv1Min.y, uvEdit.sourceUv1Max.y, uvEdit.targetUv1Min.y, uvEdit.targetUv1Max.y);
    }

    glBindBuffer(GL_ARRAY_BUFFER, loadedMesh.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, loadedMesh.verts.size() * sizeof(ObjVertex), loadedMesh.verts.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    rebuildMeshStats();
}

void MeshViewer::resetUvRangeEdits() {
    if (!meshStats.hasVertices) return;

    uvEdit.sourceUv0Min = meshStats.uv0Min;
    uvEdit.sourceUv0Max = meshStats.uv0Max;
    uvEdit.sourceUv1Min = meshStats.uv1Min;
    uvEdit.sourceUv1Max = meshStats.uv1Max;

    uvEdit.targetUv0Min = uvEdit.sourceUv0Min;
    uvEdit.targetUv0Max = uvEdit.sourceUv0Max;
    uvEdit.targetUv1Min = uvEdit.sourceUv1Min;
    uvEdit.targetUv1Max = uvEdit.sourceUv1Max;
    uvEdit.initialized = true;

    if (originalVerts.size() == loadedMesh.verts.size() && !originalVerts.empty()) {
        loadedMesh.verts = originalVerts;
        glBindBuffer(GL_ARRAY_BUFFER, loadedMesh.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, loadedMesh.verts.size() * sizeof(ObjVertex), loadedMesh.verts.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        rebuildMeshStats();
    }
}

void MeshViewer::renderImGui() {
    ImGui::SetNextWindowSize(ImVec2(470.0f, 740.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Mesh Inspector")) {
        ImGui::End();
        return;
    }

    ImGui::Text("File: %s", loadedMeshPath.c_str());
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    ImGui::Separator();
    ImGui::PopStyleColor();

    const size_t vertexCount = loadedMesh.verts.size();
    const size_t indexCount = loadedMesh.idx.size();
    const size_t triangleCount = indexCount / 3;
    const size_t groupCount = loadedMesh.groups.size();

    ImGui::Text("Vertices: %zu", vertexCount);
    ImGui::Text("Indices: %zu", indexCount);
    ImGui::Text("Triangles: %zu", triangleCount);
    ImGui::Text("Groups: %zu", groupCount);
    ImGui::Text("Vertex Stride: %zu bytes", sizeof(ObjVertex));
    ImGui::Text("Vertex Buffer: %.2f KB", (vertexCount * sizeof(ObjVertex)) / 1024.0f);
    ImGui::Text("Index Buffer: %.2f KB", (indexCount * sizeof(uint32_t)) / 1024.0f);

    if (meshStats.hasVertices) {
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        ImGui::SeparatorText("BOUNDS");
        ImGui::PopStyleColor();
        ImGui::Text("Min: (%.4f, %.4f, %.4f)", meshStats.boundsMin.x, meshStats.boundsMin.y, meshStats.boundsMin.z);
        ImGui::Text("Max: (%.4f, %.4f, %.4f)", meshStats.boundsMax.x, meshStats.boundsMax.y, meshStats.boundsMax.z);

        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        ImGui::SeparatorText("UV RANGES");
        ImGui::PopStyleColor();
        ImGui::Text("UV0 Min: (%.4f, %.4f)", meshStats.uv0Min.x, meshStats.uv0Min.y);
        ImGui::Text("UV0 Max: (%.4f, %.4f)", meshStats.uv0Max.x, meshStats.uv0Max.y);
        ImGui::Text("UV1 Min: (%.4f, %.4f)", meshStats.uv1Min.x, meshStats.uv1Min.y);
        ImGui::Text("UV1 Max: (%.4f, %.4f)", meshStats.uv1Max.x, meshStats.uv1Max.y);
    }

    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    ImGui::SeparatorText("UV RANGE EDIT");
    ImGui::PopStyleColor();

    bool uvChanged = false;
    uvChanged |= ImGui::SliderFloat2("UV0 Min", &uvEdit.targetUv0Min.x, -10.0f, 10.0f, "%.4f");
    uvChanged |= ImGui::SliderFloat2("UV0 Max", &uvEdit.targetUv0Max.x, -10.0f, 10.0f, "%.4f");
    uvChanged |= ImGui::SliderFloat2("UV1 Min", &uvEdit.targetUv1Min.x, -10.0f, 10.0f, "%.4f");
    uvChanged |= ImGui::SliderFloat2("UV1 Max", &uvEdit.targetUv1Max.x, -10.0f, 10.0f, "%.4f");

    ImGui::Checkbox("Show UV Grid Overlay", &showUvGrid);
    ImGui::SliderFloat("UV Grid Scale", &uvGridScale, 1.0f, 64.0f, "%.2f");
    ImGui::SliderFloat("UV Grid Thickness", &uvGridThickness, 0.005f, 0.20f, "%.3f");

    if (ImGui::Button("RESET")) {
        resetUvRangeEdits();
        selectedGroup = 0;
        selectedVertex = 0;
        showUvGrid = true;
        uvGridScale = 8.0f;
        uvGridThickness = 0.06f;
        meshCamera.setPosition(glm::vec3(0.0f, 0.0f, 5.0f));
        meshCamera.lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
        meshCamera.setFov(45.0f);
    }
    if (uvChanged) {
        applyUvRangeEdits();
    }

    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    ImGui::SeparatorText("VERTEX PREVIEW");
    ImGui::PopStyleColor();
    ImGui::BeginChild("VertexPreviewChild", ImVec2(-FLT_MIN, 240.0f), true);
    if (!loadedMesh.verts.empty()) {
        selectedVertex = std::clamp(selectedVertex, 0, (int)loadedMesh.verts.size() - 1);
        ImGui::SliderInt("Vertex Index", &selectedVertex, 0, (int)loadedMesh.verts.size() - 1);
        const ObjVertex& v = loadedMesh.verts[selectedVertex];
        ImGui::Text("P: (%.4f, %.4f, %.4f)", v.px, v.py, v.pz);
        ImGui::Text("N: (%.4f, %.4f, %.4f)", v.nx, v.ny, v.nz);
        ImGui::Text("T: (%.4f, %.4f, %.4f)", v.tx, v.ty, v.tz);
        ImGui::Text("B: (%.4f, %.4f, %.4f)", v.bx, v.by, v.bz);
        ImGui::Text("UV0: (%.4f, %.4f)", v.u0, v.v0);
        ImGui::Text("UV1: (%.4f, %.4f)", v.u1, v.v1);
        ImGui::Text("UV2: (%.4f, %.4f)", v.u2, v.v2);
        ImGui::Text("UV3: (%.4f, %.4f)", v.u3, v.v3);
        ImGui::Text("Color: (%.4f, %.4f, %.4f, %.4f)", v.cr, v.cg, v.cb, v.ca);
        ImGui::Text("Weights: (%.4f, %.4f, %.4f, %.4f)", v.w0, v.w1, v.w2, v.w3);
        ImGui::Text("Joints: (%u, %u, %u, %u)", v.j0, v.j1, v.j2, v.j3);
    }
    ImGui::EndChild();

    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    ImGui::SeparatorText("GROUPS");
    ImGui::PopStyleColor();
    if (!loadedMesh.groups.empty()) {
        selectedGroup = std::clamp(selectedGroup, 0, (int)loadedMesh.groups.size() - 1);
        if (ImGui::BeginListBox("##groups", ImVec2(-FLT_MIN, 130.0f))) {
            for (int i = 0; i < (int)loadedMesh.groups.size(); ++i) {
                const bool isSelected = (selectedGroup == i);
                if (ImGui::Selectable(("Group " + std::to_string(i)).c_str(), isSelected)) {
                    selectedGroup = i;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }

        const Nvx2Group& g = loadedMesh.groups[selectedGroup];
        ImGui::Text("firstVertex: %u", g.firstVertex);
        ImGui::Text("numVertices: %u", g.numVertices);
        ImGui::Text("firstTriangle: %u", g.firstTriangle);
        ImGui::Text("numTriangles: %u", g.numTriangles);
        ImGui::Text("firstEdge: %u", g.firstEdge);
        ImGui::Text("numEdges: %u", g.numEdges);
        ImGui::Text("firstIndex: %u", g.firstIndex());
        ImGui::Text("indexCount: %u", g.indexCount());
    }

    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    ImGui::SeparatorText("CAMERA");
    ImGui::PopStyleColor();
    ImGui::Text("Position: (%.3f, %.3f, %.3f)",
        meshCamera.getPosition().x, meshCamera.getPosition().y, meshCamera.getPosition().z);
    ImGui::Text("Yaw/Pitch: (%.2f, %.2f)", meshCamera.getYaw(), meshCamera.getPitch());
    ImGui::Text("FOV: %.2f", meshCamera.getFov());

    ImGui::End();
}
