// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/DeferredRenderer.h"
#include "Rendering/DeferredRendererAnimation.h"
#include "Rendering/SelectionRaycaster.h"
#include "Assets/Parser.h"
#include "Rendering/GLStateDebug.h"
#include "Animation/AnimationSystem.h"

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "Rendering/OpenGL/OpenGLDevice.h"
#include "Platform/GLFWPlatform.h"
#include "Rendering/OpenGL/OpenGLShaderManager.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include "Rendering/DrawBatchSystem.h"
#include "Rendering/MegaBuffer.h"
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "gtc/quaternion.hpp"
#include "gtx/string_cast.hpp"
#include "Assets/Map/MapHeader.h"
#include "Assets/Particles/ParticleServer.h"
#include "Core/Logger.h"
#include "gtx/norm.hpp"
#include "Export/ExportGraph.h"
#include "Export/Cooker.h"
#include "Export/StandaloneBuilder.h"

namespace {
bool ReadEditorEnvToggle(const char* name, bool defaultValue = false) {
    if (!name || !name[0]) return defaultValue;
#if defined(_WIN32)
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) {
        return defaultValue;
    }
    const bool enabled = value[0] != '\0' && value[0] != '0';
    std::free(value);
    return enabled;
#else
    const char* value = std::getenv(name);
    if (!value) return defaultValue;
    return value[0] != '\0' && value[0] != '0';
#endif
}

void ApplyEditorTheme(ImGuiIO& io) {
    ImFont* editorFont = nullptr;
#if defined(_WIN32)
    const char* fontCandidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/seguisb.ttf",
        "C:/Windows/Fonts/tahoma.ttf"
    };
    for (const char* path : fontCandidates) {
        editorFont = io.Fonts->AddFontFromFileTTF(path, 17.0f);
        if (editorFont != nullptr) {
            break;
        }
    }
#endif
    if (!editorFont) {
        editorFont = io.Fonts->AddFontDefault();
    }
    io.FontDefault = editorFont;

    // --- Graphite + Rose Theme ---
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(7.0f, 6.0f);
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 11.0f;
    style.GrabMinSize = 10.0f;

    style.WindowRounding = 6.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    style.WindowTitleAlign = ImVec2(0.02f, 0.50f);
    style.ButtonTextAlign = ImVec2(0.50f, 0.50f);
    style.SelectableTextAlign = ImVec2(0.00f, 0.50f);

    ImVec4* colors = style.Colors;
    // Graphite + Rose — dark graphite base, rose/pink accents, warm light text
    colors[ImGuiCol_Text]             = ImVec4(0.929f, 0.878f, 0.902f, 1.00f);  // #EDE0E6
    colors[ImGuiCol_TextDisabled]     = ImVec4(0.490f, 0.420f, 0.455f, 1.00f);
    colors[ImGuiCol_WindowBg]         = ImVec4(0.055f, 0.047f, 0.051f, 1.00f);  // #0E0C0D
    colors[ImGuiCol_ChildBg]          = ImVec4(0.075f, 0.067f, 0.078f, 0.96f);  // #131114
    colors[ImGuiCol_PopupBg]          = ImVec4(0.094f, 0.082f, 0.094f, 0.98f);  // #181518
    colors[ImGuiCol_Border]           = ImVec4(0.280f, 0.180f, 0.240f, 0.22f);
    colors[ImGuiCol_BorderShadow]     = ImVec4(0.00f,  0.00f,  0.00f,  0.00f);

    colors[ImGuiCol_FrameBg]          = ImVec4(0.090f, 0.078f, 0.086f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.140f, 0.118f, 0.130f, 1.00f);
    colors[ImGuiCol_FrameBgActive]    = ImVec4(0.200f, 0.165f, 0.185f, 1.00f);

    colors[ImGuiCol_TitleBg]          = ImVec4(0.055f, 0.047f, 0.051f, 1.00f);
    colors[ImGuiCol_TitleBgActive]    = ImVec4(0.160f, 0.098f, 0.130f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.055f, 0.047f, 0.051f, 1.00f);
    colors[ImGuiCol_MenuBarBg]        = ImVec4(0.068f, 0.058f, 0.063f, 1.00f);

    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.055f, 0.047f, 0.051f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.300f, 0.160f, 0.220f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.440f, 0.220f, 0.320f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.580f, 0.280f, 0.410f, 1.00f);

    colors[ImGuiCol_CheckMark]        = ImVec4(0.910f, 0.478f, 0.627f, 1.00f);  // #E87AA0
    colors[ImGuiCol_SliderGrab]       = ImVec4(0.784f, 0.282f, 0.471f, 1.00f);  // #C84878
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.910f, 0.380f, 0.580f, 1.00f);

    colors[ImGuiCol_Button]           = ImVec4(0.160f, 0.110f, 0.140f, 1.00f);
    colors[ImGuiCol_ButtonHovered]    = ImVec4(0.260f, 0.173f, 0.220f, 1.00f);
    colors[ImGuiCol_ButtonActive]     = ImVec4(0.360f, 0.220f, 0.298f, 1.00f);

    colors[ImGuiCol_Header]           = ImVec4(0.200f, 0.130f, 0.165f, 0.96f);
    colors[ImGuiCol_HeaderHovered]    = ImVec4(0.300f, 0.190f, 0.245f, 1.00f);
    colors[ImGuiCol_HeaderActive]     = ImVec4(0.400f, 0.250f, 0.330f, 1.00f);

    colors[ImGuiCol_Separator]        = ImVec4(0.340f, 0.180f, 0.260f, 0.38f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.600f, 0.300f, 0.450f, 0.80f);
    colors[ImGuiCol_SeparatorActive]  = ImVec4(0.784f, 0.380f, 0.580f, 1.00f);

    colors[ImGuiCol_ResizeGrip]        = ImVec4(0.500f, 0.240f, 0.380f, 0.30f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.680f, 0.320f, 0.500f, 0.75f);
    colors[ImGuiCol_ResizeGripActive]  = ImVec4(0.860f, 0.400f, 0.620f, 0.95f);

    colors[ImGuiCol_Tab]               = ImVec4(0.090f, 0.070f, 0.082f, 1.00f);
    colors[ImGuiCol_TabHovered]        = ImVec4(0.300f, 0.175f, 0.240f, 1.00f);
    colors[ImGuiCol_TabActive]         = ImVec4(0.240f, 0.145f, 0.196f, 1.00f);
    colors[ImGuiCol_TabUnfocused]      = ImVec4(0.075f, 0.060f, 0.070f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]= ImVec4(0.160f, 0.100f, 0.135f, 1.00f);

    colors[ImGuiCol_DockingPreview]   = ImVec4(0.784f, 0.360f, 0.537f, 0.60f);
    colors[ImGuiCol_DockingEmptyBg]   = ImVec4(0.039f, 0.031f, 0.035f, 1.00f);

    colors[ImGuiCol_PlotLines]            = ImVec4(0.850f, 0.450f, 0.647f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]     = ImVec4(1.000f, 0.600f, 0.780f, 1.00f);
    colors[ImGuiCol_PlotHistogram]        = ImVec4(0.784f, 0.353f, 0.537f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.960f, 0.480f, 0.700f, 1.00f);

    colors[ImGuiCol_TableHeaderBg]     = ImVec4(0.120f, 0.090f, 0.108f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.300f, 0.170f, 0.240f, 0.30f);
    colors[ImGuiCol_TableBorderLight]  = ImVec4(0.220f, 0.130f, 0.180f, 0.20f);
    colors[ImGuiCol_TableRowBg]        = ImVec4(0.00f,  0.00f,  0.00f,  0.00f);
    colors[ImGuiCol_TableRowBgAlt]     = ImVec4(0.110f, 0.080f, 0.098f, 0.55f);

    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.520f, 0.235f, 0.376f, 0.40f);
    colors[ImGuiCol_NavHighlight]          = ImVec4(0.820f, 0.420f, 0.600f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.910f, 0.700f, 0.820f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.040f, 0.025f, 0.035f, 0.60f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.030f, 0.018f, 0.025f, 0.60f);
}
}

bool DeferredRenderer::InitializeImGui() {
    if (imguiInitialized) return true;
    GLFWwindow* glfwWin = static_cast<GLFWwindow*>(window_ ? window_->GetNativeHandle() : nullptr);
    if (!glfwWin) return false;

    editorModeEnabled_ = ReadEditorEnvToggle("NDEVC_EDITOR_MODE");
    editorViewportInputRouting_ = ReadEditorEnvToggle("NDEVC_EDITOR_ROUTE_INPUT", true);
    imguiViewportOnly_ = ReadEditorEnvToggle("NDEVC_IMGUI_VIEWPORT_ONLY", false);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (imguiViewportOnly_) {
        io.IniFilename = nullptr;
        io.FontDefault = io.Fonts->AddFontDefault();
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(0.0f, 0.0f);
        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize = 0.0f;
        style.FrameBorderSize = 0.0f;
        style.AntiAliasedLines = false;
        style.AntiAliasedLinesUseTex = false;
        style.AntiAliasedFill = false;
    } else if (editorModeEnabled_) {
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename = "n3_editor_layout.ini";
        ApplyEditorTheme(io);
    } else {
        // Standalone play mode: no editor chrome, no ini persistence.
        io.IniFilename = nullptr;
        ApplyEditorTheme(io);
    }

    if (!ImGui_ImplGlfw_InitForOpenGL(glfwWin, true)) {
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 460 core")) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    imguiInitialized = true;
    imguiDockingAvailable_ = editorModeEnabled_ && !imguiViewportOnly_;
    return true;
}

void DeferredRenderer::BuildEditorDockLayout(unsigned int dockspaceId) {
    const ImGuiID rootDockspace = static_cast<ImGuiID>(dockspaceId);
    ImGui::DockBuilderRemoveNode(rootDockspace);
    ImGui::DockBuilderAddNode(rootDockspace, ImGuiDockNodeFlags_DockSpace);
    const ImVec2 workSize = ImGui::GetMainViewport()->WorkSize;
    // Exclude the 22px status bar so layout proportions match the actual dockspace area.
    const ImVec2 dockSize(workSize.x, workSize.y - 22.0f);
    ImGui::DockBuilderSetNodeSize(rootDockspace, dockSize);

    ImGuiID mainDock = rootDockspace;

    const ImGuiID topDock    = ImGui::DockBuilderSplitNode(mainDock, ImGuiDir_Up,    0.07f, nullptr, &mainDock);
    const ImGuiID bottomDock = ImGui::DockBuilderSplitNode(mainDock, ImGuiDir_Down,  0.24f, nullptr, &mainDock);
    const ImGuiID leftDock   = ImGui::DockBuilderSplitNode(mainDock, ImGuiDir_Left,  0.20f, nullptr, &mainDock);
    const ImGuiID rightDock  = ImGui::DockBuilderSplitNode(mainDock, ImGuiDir_Right, 0.23f, nullptr, &mainDock);

    ImGui::DockBuilderDockWindow("ActionStrip",    topDock);
    ImGui::DockBuilderDockWindow("Viewport",       mainDock);
    ImGui::DockBuilderDockWindow("Hierarchy",      leftDock);
    ImGui::DockBuilderDockWindow("Properties",     rightDock);
    ImGui::DockBuilderDockWindow("Asset Explorer", bottomDock);
    ImGui::DockBuilderFinish(rootDockspace);
}

void DeferredRenderer::RenderEditorShell() {
    // True after the user clicks View → Reset Layout.
    // Causes BuildEditorDockLayout to run even when ini data is present.
    static bool layoutResetPending = false;

    sceneViewportValid_ = false;
    sceneViewportHovered_ = false;
    sceneViewportFocused_ = false;

    const int solidTotal = static_cast<int>(solidDraws.size());
    const int solidVisible = uiCachedSolidVisible_;
    const int alphaTotal = static_cast<int>(alphaTestDraws.size());
    const int alphaVisible = uiCachedAlphaVisible_;
    const int envTotal = static_cast<int>(environmentDraws.size());
    const int envVisible = uiCachedEnvVisible_;
    const int envAlphaTotal    = static_cast<int>(environmentAlphaDraws.size());
    const int simpleLayerTotal = static_cast<int>(simpleLayerDraws.size());
    const int decalTotal       = static_cast<int>(decalDraws.size());
    const int waterTotal       = static_cast<int>(waterDraws.size());
    const int refrTotal        = static_cast<int>(refractionDraws.size());
    const int postAlphaTotal   = static_cast<int>(postAlphaUnlitDraws.size());

    const int sceneTotal   = solidTotal + alphaTotal + envTotal + envAlphaTotal
                           + simpleLayerTotal + decalTotal + waterTotal
                           + refrTotal + postAlphaTotal;
    const int sceneVisible = solidVisible + alphaVisible + envVisible;

    // Rose accent colors (reused throughout)
    const ImVec4 roseAccent   = ImVec4(0.784f, 0.282f, 0.471f, 1.00f); // #C84878
    const ImVec4 roseHighlight= ImVec4(0.910f, 0.478f, 0.627f, 1.00f); // #E87AA0

    // ── Menu Bar ──────────────────────────────────────────────────────────────
    static bool exportModalOpen = false;
    static bool workSceneCaptureRequested = false;
    static bool createEmptySceneRequested = false;
    static bool openScenePopupRequested = false;
    static bool saveSceneAsPopupRequested = false;
    static SceneManager::SceneEntityId selectedSceneEntityId = 0;
    static char openScenePathBuf[512] = {};
    static char saveScenePathBuf[512] = {};
    static bool hasWorkSceneAsset = false;
    static uint64_t workSceneRevision = 0;
    static std::string workSceneAssetName = "WorkScene";
    static NDEVC::Export::SceneDesc workSceneAsset;
    // Dirty scene protection: deferred actions pending user confirmation
    enum class PendingSceneAction : int { None, NewScene, OpenScene, ImportMap };
    static PendingSceneAction pendingSceneAction = PendingSceneAction::None;
    static std::string pendingActionPath;
    static bool unsavedChangesModalRequested = false;

    auto RequestSceneAction = [&](PendingSceneAction action, const std::string& actionPath = {}) {
        if (scene_.IsSceneDirty()) {
            pendingSceneAction = action;
            pendingActionPath = actionPath;
            unsavedChangesModalRequested = true;
        } else {
            if (action == PendingSceneAction::NewScene)      { createEmptySceneRequested = true; }
            else if (action == PendingSceneAction::OpenScene){ openScenePopupRequested = true; }
            else if (action == PendingSceneAction::ImportMap){ QueueDroppedMapLoad(actionPath); }
        }
    };

    const SceneManager::SceneAssetInfo activeSceneInfo = scene_.GetActiveSceneInfo();
    if (!activeSceneInfo.name.empty()) {
        workSceneAssetName = activeSceneInfo.name;
    }
    const SceneManager::ExportSceneSnapshot liveSceneSnapshot = scene_.BuildExportSceneSnapshot();
    const bool hasLiveSceneContent =
        !liveSceneSnapshot.mapPath.empty() ||
        !liveSceneSnapshot.entities.empty() ||
        !liveSceneSnapshot.loadedModelPaths.empty() ||
        !liveSceneSnapshot.loadedMeshPaths.empty() ||
        !liveSceneSnapshot.loadedAnimPaths.empty() ||
        !liveSceneSnapshot.loadedTexturePaths.empty();
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                RequestSceneAction(PendingSceneAction::NewScene);
            }
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                if (!activeSceneInfo.sourcePath.empty()) {
                    std::strncpy(openScenePathBuf, activeSceneInfo.sourcePath.c_str(), sizeof(openScenePathBuf) - 1);
                    openScenePathBuf[sizeof(openScenePathBuf) - 1] = '\0';
                }
                RequestSceneAction(PendingSceneAction::OpenScene);
            }
            if (ImGui::MenuItem("Save", "Ctrl+S", false, scene_.HasActiveScenePath())) {
                if (!scene_.SaveScene()) {
                    saveSceneAsPopupRequested = true;
                }
            }
            if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
                if (!activeSceneInfo.sourcePath.empty()) {
                    std::strncpy(saveScenePathBuf, activeSceneInfo.sourcePath.c_str(), sizeof(saveScenePathBuf) - 1);
                } else {
                    std::strncpy(saveScenePathBuf, "scene.ndscene", sizeof(saveScenePathBuf) - 1);
                }
                saveScenePathBuf[sizeof(saveScenePathBuf) - 1] = '\0';
                saveSceneAsPopupRequested = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Export...", nullptr, false, hasLiveSceneContent))
                exportModalOpen = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
            ImGui::MenuItem("Redo", "Ctrl+Y", false, false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset Layout")) {
                layoutResetPending   = true;
                editorDockInitialized_ = false;
            }
            ImGui::MenuItem("Route Input To Viewport", nullptr, &editorViewportInputRouting_);
            ImGui::Separator();
            ImGui::Checkbox("Visibility Culling", &enableVisibilityGrid_);
            ImGui::Checkbox("Debug Cells", &debugShowVisibilityCells_);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Scene")) {
            if (ImGui::MenuItem("New Empty Scene")) {
                createEmptySceneRequested = true;
            }
            ImGui::Separator();
            const bool canCaptureScene = (renderMode_ == RenderMode::Work) && hasLiveSceneContent;
            if (ImGui::MenuItem("Create/Update Work Scene", nullptr, false, canCaptureScene)) {
                workSceneCaptureRequested = true;
            }
            ImGui::Separator();
            ImGui::TextDisabled("Active: %s%s",
                                activeSceneInfo.name.empty() ? "Untitled Scene" : activeSceneInfo.name.c_str(),
                                activeSceneInfo.dirty ? "*" : "");
            ImGui::TextDisabled("Path: %s",
                                activeSceneInfo.sourcePath.empty() ? "<unsaved>" : activeSceneInfo.sourcePath.c_str());
            if (hasWorkSceneAsset) {
                ImGui::Separator();
                ImGui::TextDisabled("Name: %s", workSceneAssetName.c_str());
                ImGui::TextDisabled("Map: %s", workSceneAsset.mapPath.c_str());
                ImGui::TextDisabled("Revision: %llu", static_cast<unsigned long long>(workSceneRevision));
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Mode")) {
            const bool isWork = (renderMode_ == RenderMode::Work);
            if (ImGui::MenuItem("Work Mode", "F1", isWork)) {
                if (!isWork) SetRenderMode(RenderMode::Work);
            }
            if (ImGui::MenuItem("Play Mode", "F1", !isWork)) {
                if (isWork) SetRenderMode(RenderMode::Play);
            }
            ImGui::Separator();
            ImGui::MenuItem("Dirty Rendering", nullptr, &activePolicy_.dirtyRenderingEnabled);
            ImGui::MenuItem("Budget Governor", nullptr, &activePolicy_.budgetGovernorEnabled);
            ImGui::Separator();
            if (activePolicy_.targetFps <= 0) {
                ImGui::TextDisabled("FPS: uncapped");
            } else {
                ImGui::Text("FPS: %d (capped)", activePolicy_.targetFps);
            }
            const char* tierNames[] = { "Full", "Reduced", "Minimum" };
            int tierIdx = static_cast<int>(budgetGovernor_.tier);
            ImGui::Text("Quality: %s", tierNames[tierIdx]);
            if (budgetGovernor_.lodBiasAdjust > 0.0f || budgetGovernor_.alphaAggressiveness > 0.0f) {
                ImGui::TextDisabled("  lodAdj=%.1f alphaAggr=%.0f%%",
                    budgetGovernor_.lodBiasAdjust,
                    budgetGovernor_.alphaAggressiveness * 100.0f);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    auto ResetEditorRuntimeState = [&](bool resetCamera) {
        ClearDisabledDraws();
        solidDraws.clear();
        alphaTestDraws.clear();
        decalDraws.clear();
        particleDraws.clear();
        environmentDraws.clear();
        environmentAlphaDraws.clear();
        simpleLayerDraws.clear();
        refractionDraws.clear();
        postAlphaUnlitDraws.clear();
        waterDraws.clear();
        animatedDraws.clear();
        selectedObject = nullptr;
        selectedIndex = -1;
        selectedSceneEntityId = 0;
        cachedObj = DrawCmd{};
        cachedIndex = -1;
        visibleCells_.clear();
        lastVisibleCells_.clear();
        visibilityStage_.Reset();
        visibilityStageFrameIndex_ = 0;
        visFrustumCacheValid_ = false;
        scenePrepared = false;
        if (resetCamera) {
            camera_.setPosition(glm::vec3(0.0f, 0.0f, 5.0f));
            camera_.lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
        }
    };

    if (openScenePopupRequested) {
        ImGui::OpenPopup("Open Scene");
        openScenePopupRequested = false;
    }
    if (saveSceneAsPopupRequested) {
        ImGui::OpenPopup("Save Scene As");
        saveSceneAsPopupRequested = false;
    }
    if (unsavedChangesModalRequested) {
        ImGui::OpenPopup("Unsaved Changes");
        unsavedChangesModalRequested = false;
    }

    // ── Unsaved Changes modal ──────────────────────────────────────────────
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("The scene '%s' has unsaved changes.",
                    activeSceneInfo.name.empty() ? "Untitled Scene" : activeSceneInfo.name.c_str());
        ImGui::Text("Do you want to save before continuing?");
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(100.0f, 0.0f))) {
            if (scene_.HasActiveScenePath()) {
                scene_.SaveScene();
            } else {
                std::strncpy(saveScenePathBuf, "scene.ndscene", sizeof(saveScenePathBuf) - 1);
                saveSceneAsPopupRequested = true;
            }
            // Execute pending action (OpenScene with known path opens directly)
            if (pendingSceneAction == PendingSceneAction::NewScene) {
                createEmptySceneRequested = true;
            } else if (pendingSceneAction == PendingSceneAction::OpenScene) {
                if (!pendingActionPath.empty()) {
                    if (scene_.OpenScene(pendingActionPath)) {
                        ResetEditorRuntimeState(false); hasWorkSceneAsset = false; ++workSceneRevision; MarkDirty();
                    }
                } else { openScenePopupRequested = true; }
            } else if (pendingSceneAction == PendingSceneAction::ImportMap) {
                QueueDroppedMapLoad(pendingActionPath);
            }
            pendingSceneAction = PendingSceneAction::None;
            pendingActionPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(100.0f, 0.0f))) {
            if (pendingSceneAction == PendingSceneAction::NewScene) {
                createEmptySceneRequested = true;
            } else if (pendingSceneAction == PendingSceneAction::OpenScene) {
                if (!pendingActionPath.empty()) {
                    if (scene_.OpenScene(pendingActionPath)) {
                        ResetEditorRuntimeState(false); hasWorkSceneAsset = false; ++workSceneRevision; MarkDirty();
                    }
                } else { openScenePopupRequested = true; }
            } else if (pendingSceneAction == PendingSceneAction::ImportMap) {
                QueueDroppedMapLoad(pendingActionPath);
            }
            pendingSceneAction = PendingSceneAction::None;
            pendingActionPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            pendingSceneAction = PendingSceneAction::None;
            pendingActionPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(580.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Scene Path", openScenePathBuf, sizeof(openScenePathBuf));
        if (ImGui::Button("Open", ImVec2(100.0f, 0.0f))) {
            if (scene_.OpenScene(openScenePathBuf)) {
                ResetEditorRuntimeState(false);
                workSceneAssetName = scene_.GetActiveSceneInfo().name;
                hasWorkSceneAsset = false;
                ++workSceneRevision;
                MarkDirty();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(580.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Scene Path", saveScenePathBuf, sizeof(saveScenePathBuf));
        if (ImGui::Button("Save", ImVec2(100.0f, 0.0f))) {
            if (scene_.SaveSceneAs(saveScenePathBuf)) {
                MarkDirty();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    auto BuildSceneDescFromWorkState = [&](bool includeUnloadedDependencies = false) -> NDEVC::Export::SceneDesc {
        namespace Ex = NDEVC::Export;
        Ex::SceneDesc sceneDesc;
        const SceneManager::ExportSceneSnapshot snapshot =
            scene_.BuildExportSceneSnapshot(includeUnloadedDependencies);
        sceneDesc.sceneGuid = snapshot.sceneGuid;
        sceneDesc.sceneName = snapshot.sceneName;
        sceneDesc.mapPath = snapshot.mapPath;
        sceneDesc.mapName = snapshot.mapName;
        sceneDesc.mapInfo = snapshot.mapInfo;

        sceneDesc.entities.reserve(snapshot.entities.size());
        for (const SceneManager::ExportSceneEntity& src : snapshot.entities) {
            Ex::EntityDesc dst;
            dst.name = src.name;
            dst.templateName = src.templateName;
            dst.position = src.position;
            dst.rotation = src.rotation;
            dst.scale = src.scale;
            dst.isStatic = src.isStatic;
            dst.isAlpha = src.isAlpha;
            dst.isDecal = src.isDecal;
            sceneDesc.entities.push_back(std::move(dst));
        }

        sceneDesc.requiredModelPaths = snapshot.loadedModelPaths;
        sceneDesc.requiredMeshPaths = snapshot.loadedMeshPaths;
        sceneDesc.requiredAnimPaths = snapshot.loadedAnimPaths;
        sceneDesc.requiredTexturePaths = snapshot.loadedTexturePaths;
        sceneDesc.requiredShaderNames = snapshot.loadedShaderNames;
        return sceneDesc;
    };

    if (createEmptySceneRequested) {
        createEmptySceneRequested = false;
        scene_.CreateScene("Untitled Scene");
        ResetEditorRuntimeState(true);
        saveScenePathBuf[0] = '\0';
        openScenePathBuf[0] = '\0';
        workSceneAssetName = scene_.GetActiveSceneInfo().name;
        workSceneAsset = BuildSceneDescFromWorkState();
        hasWorkSceneAsset = true;
        ++workSceneRevision;
        MarkDirty();
        NC::LOGGING::Log("[WORK_SCENE] Created empty scene with default camera");
    }

    if (!hasWorkSceneAsset &&
        renderMode_ == RenderMode::Work &&
        hasLiveSceneContent) {
        workSceneAsset = BuildSceneDescFromWorkState();
        hasWorkSceneAsset = true;
        ++workSceneRevision;
        NC::LOGGING::Log("[WORK_SCENE] Auto-created from Work Mode map=", workSceneAsset.mapPath,
                         " revision=", static_cast<unsigned long long>(workSceneRevision));
    }
    if (hasWorkSceneAsset &&
        renderMode_ == RenderMode::Work &&
        hasLiveSceneContent &&
        (workSceneAsset.mapPath != liveSceneSnapshot.mapPath || scene_.IsDrawListsDirty())) {
        workSceneAsset = BuildSceneDescFromWorkState();
        ++workSceneRevision;
    }

    if (workSceneCaptureRequested) {
        workSceneCaptureRequested = false;
        if (renderMode_ == RenderMode::Work && hasLiveSceneContent) {
            workSceneAsset = BuildSceneDescFromWorkState();
            hasWorkSceneAsset = true;
            ++workSceneRevision;
            NC::LOGGING::Log("[WORK_SCENE] Captured scene name=", workSceneAssetName,
                             " map=", workSceneAsset.mapPath,
                             " entities=", workSceneAsset.entities.size(),
                             " models=", workSceneAsset.requiredModelPaths.size(),
                             " meshes=", workSceneAsset.requiredMeshPaths.size(),
                             " anims=", workSceneAsset.requiredAnimPaths.size(),
                             " textures=", workSceneAsset.requiredTexturePaths.size(),
                             " shaders=", workSceneAsset.requiredShaderNames.size(),
                             " revision=", static_cast<unsigned long long>(workSceneRevision));
        }
    }

    // ── Export Modal ──────────────────────────────────────────────────────────
    if (exportModalOpen) {
        ImGui::OpenPopup("Export Scene");
        exportModalOpen = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Export Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        namespace Ex = NDEVC::Export;

        static Ex::ExportProfile exportProfile = Ex::ExportProfile::Work;
        static char exportOutDir[512]          = "C:/export";
        static Ex::ExportReport lastReport     = {};
        static bool hasLastResult              = false;
        static bool exportRunning              = false;
        // Standalone build state
        static char standaloneExeName[128]             = "NDEVC";
        static Ex::StandaloneBuildResult buildResult   = {};
        static bool hasBuildResult                     = false;
        static std::string lastNdpkPath;
        static std::string lastStartupMapPath;
        static std::vector<std::string> lastSourceAssets;

        // Profile selector
        const char* profileNames[] = { "Work", "Playtest", "Shipping" };
        int profileIdx = static_cast<int>(exportProfile);
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::Combo("Profile", &profileIdx, profileNames, 3))
            exportProfile = static_cast<Ex::ExportProfile>(profileIdx);

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("Output Dir", exportOutDir, sizeof(exportOutDir));
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("Exe Name", standaloneExeName, sizeof(standaloneExeName));
        ImGui::Spacing();

        const bool canCaptureScene = hasLiveSceneContent;
        ImGui::BeginDisabled(!canCaptureScene);
        if (ImGui::Button("Create/Update Scene", ImVec2(150.0f, 24.0f))) {
            workSceneAsset = BuildSceneDescFromWorkState(true);
            hasWorkSceneAsset = true;
            ++workSceneRevision;
            NC::LOGGING::Log("[WORK_SCENE] Captured scene name=", workSceneAssetName,
                             " map=", workSceneAsset.mapPath,
                             " entities=", workSceneAsset.entities.size(),
                             " models=", workSceneAsset.requiredModelPaths.size(),
                             " meshes=", workSceneAsset.requiredMeshPaths.size(),
                             " anims=", workSceneAsset.requiredAnimPaths.size(),
                             " textures=", workSceneAsset.requiredTexturePaths.size(),
                             " shaders=", workSceneAsset.requiredShaderNames.size(),
                             " revision=", static_cast<unsigned long long>(workSceneRevision));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (hasWorkSceneAsset) {
            ImGui::TextDisabled("scene: %s (rev %llu)", workSceneAssetName.c_str(),
                                static_cast<unsigned long long>(workSceneRevision));
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.30f, 1.0f), "Create scene first (Work Mode).");
        }
        ImGui::TextDisabled("Loaded now: entities=%d models=%d meshes=%d anims=%d textures=%d shaders=%d",
            static_cast<int>(liveSceneSnapshot.entities.size()),
            static_cast<int>(liveSceneSnapshot.loadedModelPaths.size()),
            static_cast<int>(liveSceneSnapshot.loadedMeshPaths.size()),
            static_cast<int>(liveSceneSnapshot.loadedAnimPaths.size()),
            static_cast<int>(liveSceneSnapshot.loadedTexturePaths.size()),
            static_cast<int>(liveSceneSnapshot.loadedShaderNames.size()));
        if (hasWorkSceneAsset) {
            ImGui::TextDisabled("Captured scene: entities=%d models=%d meshes=%d anims=%d textures=%d shaders=%d",
                static_cast<int>(workSceneAsset.entities.size()),
                static_cast<int>(workSceneAsset.requiredModelPaths.size()),
                static_cast<int>(workSceneAsset.requiredMeshPaths.size()),
                static_cast<int>(workSceneAsset.requiredAnimPaths.size()),
                static_cast<int>(workSceneAsset.requiredTexturePaths.size()),
                static_cast<int>(workSceneAsset.requiredShaderNames.size()));
        }
        ImGui::Spacing();

        const bool canExportScene = hasWorkSceneAsset || hasLiveSceneContent;
        ImGui::BeginDisabled(exportRunning || !canExportScene);
        const bool doExport = ImGui::Button("Export", ImVec2(120.0f, 26.0f));
        ImGui::EndDisabled();
        ImGui::SameLine();
        // Build Standalone always performs a fresh export first.
        ImGui::BeginDisabled(exportRunning || !canExportScene);
        const bool doBuildStandalone = ImGui::Button("Build Standalone", ImVec2(150.0f, 26.0f));
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(80.0f, 26.0f)))
            ImGui::CloseCurrentPopup();

        if (doExport) {
            exportRunning = true;

            if (hasLiveSceneContent) {
                workSceneAsset = BuildSceneDescFromWorkState(true);
                hasWorkSceneAsset = true;
                ++workSceneRevision;
            } else if (!hasWorkSceneAsset) {
                workSceneAsset = BuildSceneDescFromWorkState();
                hasWorkSceneAsset = true;
                ++workSceneRevision;
            }
            Ex::SceneDesc sceneDesc = workSceneAsset;
            lastStartupMapPath = sceneDesc.mapPath;
            lastSourceAssets.clear();
            lastSourceAssets.insert(lastSourceAssets.end(),
                                    sceneDesc.requiredModelPaths.begin(),
                                    sceneDesc.requiredModelPaths.end());
            lastSourceAssets.insert(lastSourceAssets.end(),
                                    sceneDesc.requiredMeshPaths.begin(),
                                    sceneDesc.requiredMeshPaths.end());
            lastSourceAssets.insert(lastSourceAssets.end(),
                                    sceneDesc.requiredAnimPaths.begin(),
                                    sceneDesc.requiredAnimPaths.end());
            lastSourceAssets.insert(lastSourceAssets.end(),
                                    sceneDesc.requiredTexturePaths.begin(),
                                    sceneDesc.requiredTexturePaths.end());

            Ex::ExportContext ctx;
            ctx.profile     = exportProfile;
            ctx.settings    = Ex::Cooker::BuildSettings(exportProfile);
            ctx.outputDir   = exportOutDir;
            ctx.sourceScene = std::move(sceneDesc);

            Ex::ExportGraph graph = Ex::BuildStandardGraph();
            graph.Execute(ctx);

            lastReport    = ctx.report;
            lastNdpkPath  = ctx.outputPackagePath;
            hasLastResult = true;
            exportRunning = false;
        }

        // ── Build Standalone ──────────────────────────────────────────────
        if (doBuildStandalone) {
            // Always export first so standalone reflects the current scene state.
            exportRunning = true;
            if (hasLiveSceneContent) {
                workSceneAsset = BuildSceneDescFromWorkState(true);
                hasWorkSceneAsset = true;
                ++workSceneRevision;
            } else if (!hasWorkSceneAsset) {
                workSceneAsset = BuildSceneDescFromWorkState();
                hasWorkSceneAsset = true;
                ++workSceneRevision;
            }
            Ex::SceneDesc autoSceneDesc = workSceneAsset;
            lastStartupMapPath = autoSceneDesc.mapPath;
            lastSourceAssets.clear();
            lastSourceAssets.insert(lastSourceAssets.end(),
                                    autoSceneDesc.requiredModelPaths.begin(),
                                    autoSceneDesc.requiredModelPaths.end());
            lastSourceAssets.insert(lastSourceAssets.end(),
                                    autoSceneDesc.requiredMeshPaths.begin(),
                                    autoSceneDesc.requiredMeshPaths.end());
            lastSourceAssets.insert(lastSourceAssets.end(),
                                    autoSceneDesc.requiredAnimPaths.begin(),
                                    autoSceneDesc.requiredAnimPaths.end());
            lastSourceAssets.insert(lastSourceAssets.end(),
                                    autoSceneDesc.requiredTexturePaths.begin(),
                                    autoSceneDesc.requiredTexturePaths.end());
            Ex::ExportContext autoCtx;
            autoCtx.profile     = exportProfile;
            autoCtx.settings    = Ex::Cooker::BuildSettings(exportProfile);
            autoCtx.outputDir   = exportOutDir;
            autoCtx.sourceScene = std::move(autoSceneDesc);
            Ex::ExportGraph autoGraph = Ex::BuildStandardGraph();
            autoGraph.Execute(autoCtx);
            lastReport    = autoCtx.report;
            lastNdpkPath  = autoCtx.outputPackagePath;
            hasLastResult = true;
            exportRunning = false;

            if (!lastNdpkPath.empty() && std::filesystem::exists(lastNdpkPath)) {
                Ex::StandaloneBuildConfig sbCfg;
                sbCfg.outputDir       = std::string(exportOutDir) + "/standalone";
                sbCfg.executableName  = standaloneExeName[0] ? standaloneExeName : "NDEVC";
                sbCfg.sourceDir       = SOURCE_DIR;
                sbCfg.ndpkPath        = lastNdpkPath;
                sbCfg.startupMapPath  = lastStartupMapPath;
                sbCfg.assetPaths      = lastSourceAssets;
                buildResult    = Ex::BuildStandalone(sbCfg);
                hasBuildResult = true;
            } else {
                buildResult = {};
                buildResult.errorMsg = "Standalone build skipped: export did not produce a package.";
                hasBuildResult = true;
            }
        }

        if (hasLastResult) {
            ImGui::Spacing();
            ImGui::Separator();
            if (lastReport.success) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.85f, 0.40f, 1.0f));
                ImGui::TextUnformatted("\xe2\x9c\x94  Export succeeded");
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.30f, 0.30f, 1.0f));
                ImGui::TextUnformatted("\xe2\x9c\x98  Export failed");
            }
            ImGui::PopStyleColor();
            ImGui::Text("VRAM estimate: %.1f MB",
                static_cast<double>(lastReport.totalVRAMEstimateBytes) / (1024.0 * 1024.0));
            ImGui::Text("Draw calls:    %d",    lastReport.totalDrawCalls);
            ImGui::Text("Frame cost:    %.2f ms", lastReport.frameCostEstimate);

            if (!lastReport.issues.empty()) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.05f, 0.07f, 1.0f));
                ImGui::BeginChild("##ExIssues", ImVec2(-1.0f, 120.0f), true);
                ImGui::PopStyleColor();
                for (const Ex::ExportIssue& iss : lastReport.issues) {
                    const bool isErr = (iss.severity == Ex::ExportIssue::Severity::Error);
                    ImGui::PushStyleColor(ImGuiCol_Text, isErr
                        ? ImVec4(0.90f, 0.30f, 0.30f, 1.0f)
                        : ImVec4(0.90f, 0.75f, 0.30f, 1.0f));
                    ImGui::TextWrapped("[%s] %s: %s",
                        isErr ? "ERR" : "WRN",
                        iss.assetPath.c_str(), iss.message.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::EndChild();
            }
            if (!lastReport.topOffenders.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("Top VRAM:");
                for (const auto& [name, vram] : lastReport.topOffenders) {
                    ImGui::Text("  %.1f MB  %s",
                        static_cast<double>(vram) / (1024.0 * 1024.0), name.c_str());
                }
            }
        }

        // ── Standalone Build Result ───────────────────────────────────────
        if (hasBuildResult) {
            ImGui::Spacing();
            ImGui::SeparatorText("Standalone Build");
            if (buildResult.success) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.85f, 0.40f, 1.0f));
                ImGui::TextUnformatted("\xe2\x9c\x94  Build succeeded");
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.30f, 0.30f, 1.0f));
                ImGui::TextUnformatted("\xe2\x9c\x98  Build failed");
                ImGui::PopStyleColor();
                ImGui::TextWrapped("%s", buildResult.errorMsg.c_str());
            }
            if (!buildResult.runtimeExeFound.empty())
                ImGui::TextDisabled("Runtime: %s", buildResult.runtimeExeFound.c_str());
            if (!buildResult.copiedFiles.empty()) {
                ImGui::TextDisabled("%d file(s) copied:", static_cast<int>(buildResult.copiedFiles.size()));
                ImGui::BeginChild("##SBFiles", ImVec2(-1.0f, 90.0f), true);
                for (const auto& f : buildResult.copiedFiles)
                    ImGui::TextDisabled("  %s", f.c_str());
                ImGui::EndChild();
            }
        }

        ImGui::EndPopup();
    }

    // ── Dockspace ─────────────────────────────────────────────────────────────
    // Manual host window instead of DockSpaceOverViewport so we can:
    //   1. Reserve 22px at the bottom for the status bar (no panel extends behind it).
    //   2. Avoid PassthruCentralNode — the scene is shown via ImGui::Image so the flag
    //      is not needed, and it was causing the dockspace host to render with no
    //      background which made the dock-node separator between panels transparent.
    const ImGuiViewport* mainVP       = ImGui::GetMainViewport();
    const float          statusBarH   = 22.0f;
    ImGui::SetNextWindowPos(mainVP->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(mainVP->WorkSize.x, mainVP->WorkSize.y - statusBarH));
    ImGui::SetNextWindowViewport(mainVP->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.0f, 0.0f));
    ImGui::Begin("##DockSpaceHost", nullptr,
        ImGuiWindowFlags_NoTitleBar            | ImGuiWindowFlags_NoCollapse  |
        ImGuiWindowFlags_NoResize              | ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus  |
        ImGuiWindowFlags_NoScrollbar           | ImGuiWindowFlags_NoDocking   |
        ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);
    const ImGuiID dockspaceId = ImGui::GetID("N3ForgeDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_NoWindowMenuButton);
    ImGui::End();

    // If a dock node that previously existed has disappeared, trigger re-init.
    if (editorDockInitialized_ && ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        editorDockInitialized_ = false;
    }
    if (!editorDockInitialized_) {
        // Only build the default layout when:
        //   a) No saved state exists in the ini file for this dockspace (first launch), OR
        //   b) The user explicitly requested a reset via View → Reset Layout.
        // On every other startup ImGui restores panel positions from n3_editor_layout.ini
        // automatically — we must NOT call BuildEditorDockLayout in that case or we
        // would overwrite the user's saved arrangement.
        if (layoutResetPending || ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
            BuildEditorDockLayout(static_cast<unsigned int>(dockspaceId));
            layoutResetPending = false;
        }
        editorDockInitialized_ = true;
    }

    // ── ActionStrip ───────────────────────────────────────────────────────────
    {
        const ImGuiWindowFlags toolbarFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoResize;
        ImGuiWindowClass toolbarClass;
        toolbarClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
        ImGui::SetNextWindowClass(&toolbarClass);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(7.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(5.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7.0f, 5.0f));
        static bool addModelPopupRequested = false;
        static char addModelPathBuf[512] = {};
        if (ImGui::Begin("ActionStrip", nullptr, toolbarFlags)) {
            // Transform buttons
            if (ImGui::Button("Select"))    {}  ImGui::SameLine();
            if (ImGui::Button("Translate")) {}  ImGui::SameLine();
            if (ImGui::Button("Rotate"))    {}  ImGui::SameLine();
            if (ImGui::Button("Scale"))     {}  ImGui::SameLine();

            // Vertical separator
            ImGui::TextDisabled("|");  ImGui::SameLine();

            if (ImGui::Button("Add Empty")) {
                selectedSceneEntityId = scene_.CreateEntity("GameObject");
                MarkDirty();
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Model")) {
                addModelPopupRequested = true;
            }
            ImGui::SameLine();

            // Vertical separator
            ImGui::TextDisabled("|");  ImGui::SameLine();

            // Pick checkboxes
            ImGui::Checkbox("Pick",        &clickPickEnabled);       ImGui::SameLine();
            ImGui::Checkbox("Transparent", &pickIncludeTransparent); ImGui::SameLine();
            ImGui::Checkbox("Decals",      &pickIncludeDecals);      ImGui::SameLine();

            // Vertical separator
            ImGui::TextDisabled("|");  ImGui::SameLine();

            // Centered mode-aware playback controls
            const bool isWork = (renderMode_ == RenderMode::Work);
            const float windowWidth = ImGui::GetWindowWidth();
            const float buttonWidth = 72.0f;
            if (isWork) {
                const float groupW  = buttonWidth;
                const float centerX = (windowWidth - groupW) * 0.5f;
                if (centerX > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(centerX);
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.50f, 0.26f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.24f, 0.62f, 0.32f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.14f, 0.42f, 0.20f, 1.0f));
                if (ImGui::Button("\xe2\x96\xb6 Play", ImVec2(buttonWidth, 0.0f))) {
                    SetRenderMode(RenderMode::Play);
                }
                ImGui::PopStyleColor(3);
            } else {
                const float groupW  = buttonWidth * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
                const float centerX = (windowWidth - groupW) * 0.5f;
                if (centerX > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(centerX);
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.62f, 0.18f, 0.18f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.75f, 0.24f, 0.24f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.50f, 0.14f, 0.14f, 1.0f));
                if (ImGui::Button("\xe2\x96\xa0 Stop", ImVec2(buttonWidth, 0.0f))) {
                    SetRenderMode(RenderMode::Work);
                }
                ImGui::PopStyleColor(3);
                ImGui::SameLine();
                if (ImGui::Button("Pause", ImVec2(buttonWidth, 0.0f))) {}
                ImGui::SameLine();
                if (ImGui::Button("Step",  ImVec2(buttonWidth, 0.0f))) {}
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);

        if (addModelPopupRequested) {
            ImGui::OpenPopup("Add Model Entity");
            addModelPopupRequested = false;
        }
        ImGui::SetNextWindowSize(ImVec2(580.0f, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Add Model Entity", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Model Path", addModelPathBuf, sizeof(addModelPathBuf));
            if (ImGui::Button("Create", ImVec2(100.0f, 0.0f))) {
                if (addModelPathBuf[0] != '\0') {
                    selectedSceneEntityId = scene_.CreateModelEntity(
                        addModelPathBuf,
                        glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                        glm::vec3(1.0f));
                    MarkDirty();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // ── Viewport ──────────────────────────────────────────────────────────────
    if (ImGui::Begin("Viewport")) {
        sceneViewportFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        if (viewportSize.x < 1.0f) viewportSize.x = 1.0f;
        if (viewportSize.y < 1.0f) viewportSize.y = 1.0f;

        GLuint finalTexture = 0;
        if (lightingGraph) {
            auto finalTextureInterface = lightingGraph->getTextureInterface("sceneColor");
            if (finalTextureInterface) {
                finalTexture = *(GLuint*)finalTextureInterface->GetNativeHandle();
            }
        }

        if (finalTexture != 0) {
            ImGui::Image(reinterpret_cast<void*>(static_cast<intptr_t>(finalTexture)),
                         viewportSize,
                         ImVec2(0.0f, 1.0f),
                         ImVec2(1.0f, 0.0f));
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            sceneViewportX_ = itemMin.x;
            sceneViewportY_ = itemMin.y;
            sceneViewportW_ = std::max(0.0f, itemMax.x - itemMin.x);
            sceneViewportH_ = std::max(0.0f, itemMax.y - itemMin.y);
            sceneViewportValid_   = sceneViewportW_ > 1.0f && sceneViewportH_ > 1.0f;
            sceneViewportHovered_ = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

            ImDrawList* drawList = ImGui::GetWindowDrawList();

            // Play Mode: Unity-style colored border to signal game is running
            if (renderMode_ == RenderMode::Play) {
                constexpr float kBorderThickness = 3.0f;
                const ImU32 playBorderColor = IM_COL32(65, 195, 80, 230);  // green
                drawList->AddRect(itemMin, itemMax, playBorderColor, 0.0f, 0, kBorderThickness);
                // "PLAY" badge top-centre
                const char* playLabel = "\xe2\x96\xb6  PLAY MODE";
                const ImVec2 textSize = ImGui::CalcTextSize(playLabel);
                const ImVec2 badgeMin(itemMin.x + (sceneViewportW_ - textSize.x - 16.0f) * 0.5f, itemMin.y + 6.0f);
                const ImVec2 badgeMax(badgeMin.x + textSize.x + 16.0f, badgeMin.y + textSize.y + 6.0f);
                drawList->AddRectFilled(badgeMin, badgeMax, IM_COL32(14, 80, 20, 200), 4.0f);
                drawList->AddText(ImVec2(badgeMin.x + 8.0f, badgeMin.y + 3.0f),
                                  IM_COL32(65, 230, 80, 255), playLabel);
            }
            const ImVec2 cardMin(itemMin.x + 12.0f, itemMin.y + 12.0f);
            const ImVec2 cardMax(cardMin.x + 318.0f, cardMin.y + 58.0f);
            drawList->AddRectFilled(cardMin, cardMax, IM_COL32(14, 10, 13, 220), 6.0f);
            drawList->AddRect(cardMin, cardMax, IM_COL32(150, 60, 100, 190), 6.0f);
            // Top accent line
            drawList->AddLine(ImVec2(cardMin.x + 6.0f, cardMin.y + 1.0f),
                              ImVec2(cardMax.x - 6.0f, cardMin.y + 1.0f),
                              IM_COL32(200, 80, 130, 200), 2.0f);
            drawList->AddText(ImVec2(cardMin.x + 10.0f, cardMin.y + 9.0f),
                              IM_COL32(220, 140, 175, 255),
                              "N3 ENGINE  \xc2\xb7  DEFERRED");
            char overlayLine[256];
            const char* selName = selectedObject ? selectedObject->nodeName.c_str() : "<none>";
            snprintf(overlayLine, sizeof(overlayLine), "%d/%d objects  \xc2\xb7  %s",
                     sceneVisible, sceneTotal, selName);
            drawList->AddText(ImVec2(cardMin.x + 10.0f, cardMin.y + 33.0f),
                              IM_COL32(170, 110, 148, 255),
                              overlayLine);
        } else {
            ImGui::TextUnformatted("Scene texture is unavailable.");
        }
    }
    ImGui::End();

    // ── Hierarchy (left — Unity-style scene object list) ─────────────────────
    if (ImGui::Begin("Hierarchy")) {
        auto findDrawByOwner = [&](void* owner) -> DrawCmd* {
            if (!owner) return nullptr;
            auto scan = [&](std::vector<DrawCmd>& draws) -> DrawCmd* {
                for (DrawCmd& dc : draws) {
                    if (dc.instance == owner) return &dc;
                }
                return nullptr;
            };
            if (DrawCmd* dc = scan(solidDraws)) return dc;
            if (DrawCmd* dc = scan(alphaTestDraws)) return dc;
            if (DrawCmd* dc = scan(environmentDraws)) return dc;
            if (DrawCmd* dc = scan(environmentAlphaDraws)) return dc;
            if (DrawCmd* dc = scan(simpleLayerDraws)) return dc;
            if (DrawCmd* dc = scan(decalDraws)) return dc;
            if (DrawCmd* dc = scan(waterDraws)) return dc;
            if (DrawCmd* dc = scan(refractionDraws)) return dc;
            if (DrawCmd* dc = scan(postAlphaUnlitDraws)) return dc;
            return scan(particleDraws);
        };

        // Sync selectedSceneEntityId from picking result (unify selection)
        {
            static DrawCmd* lastSyncedSelectedObject = nullptr;
            if (selectedObject != lastSyncedSelectedObject) {
                lastSyncedSelectedObject = selectedObject;
                if (selectedObject) {
                    const auto eid = scene_.FindEntityIdByRuntimeOwner(selectedObject->instance);
                    selectedSceneEntityId = eid; // 0 if not found (unmanaged draw)
                } else {
                    selectedSceneEntityId = 0;
                }
            }
        }

        const auto& authoredEntities = scene_.GetAuthoredEntities();
        ImGui::SeparatorText("Objects");
        if (authoredEntities.empty()) {
            ImGui::TextDisabled("Scene has no authored objects.");
        } else {
            // Build depth map for indent display (parent-child from parentId)
            std::unordered_map<SceneManager::SceneEntityId, int> entityDepth;
            entityDepth.reserve(authoredEntities.size());
            for (const auto& entity : authoredEntities) {
                entityDepth[entity.id] = 0; // initialized below
            }
            // Compute depth via parent walk (capped at 16 to avoid cycles)
            for (const auto& entity : authoredEntities) {
                if (entity.parentId == 0) {
                    entityDepth[entity.id] = 0;
                    continue;
                }
                int depth = 0;
                SceneManager::SceneEntityId cur = entity.parentId;
                while (cur != 0 && depth < 16) {
                    ++depth;
                    const auto* parent = [&]() -> const SceneManager::AuthoredEntity* {
                        for (const auto& e : authoredEntities)
                            if (e.id == cur) return &e;
                        return nullptr;
                    }();
                    cur = parent ? parent->parentId : 0;
                }
                entityDepth[entity.id] = depth;
            }

            for (const auto& entity : authoredEntities) {
                const int depth = entityDepth.count(entity.id) ? entityDepth[entity.id] : 0;
                if (depth > 0) ImGui::Indent(static_cast<float>(depth) * 12.0f);
                const bool isSelected = (selectedSceneEntityId == entity.id);
                const std::string label = entity.name + "##ent_" + std::to_string(entity.id);
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    selectedSceneEntityId = entity.id;
                    DrawCmd* sceneDraw = findDrawByOwner(entity.runtimeOwner);
                    if (sceneDraw) {
                        selectedObject = sceneDraw;
                    } else {
                        selectedObject = nullptr;
                        selectedIndex = -1;
                    }
                }
                if (depth > 0) ImGui::Unindent(static_cast<float>(depth) * 12.0f);
            }
        }
        if (selectedSceneEntityId != 0 && ImGui::Button("Delete Selected Object", ImVec2(-1.0f, 0.0f))) {
            if (scene_.DestroyEntity(selectedSceneEntityId)) {
                selectedSceneEntityId = 0;
                InvalidateSelection();
                MarkDirty();
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Render Stats");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.82f, 0.45f, 1.0f));
        ImGui::Text("Solid        %d / %d", solidVisible, solidTotal);
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.72f, 0.30f, 1.0f));
        ImGui::Text("AlphaTest    %d / %d", alphaVisible, alphaTotal);
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.42f, 0.72f, 0.96f, 1.0f));
        ImGui::Text("Environment  %d / %d", envVisible, envTotal);
        ImGui::PopStyleColor();
        ImGui::Text("Particles    %d", static_cast<int>(scene_.GetParticleNodeCount()));

        // Thin rose visibility ratio bar
        if (sceneTotal > 0) {
            const float ratio = static_cast<float>(sceneVisible) / static_cast<float>(sceneTotal);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, roseAccent);
            ImGui::ProgressBar(ratio, ImVec2(-1.0f, 4.0f), "");
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Visibility");
        if (ImGui::Checkbox("Enable Visibility Grid", &enableVisibilityGrid_)) {
            lastVisibleCells_.clear();
            if (!enableVisibilityGrid_) {
                visibleCells_.clear();
            }
        }
        if (enableVisibilityGrid_) {
            if (solidVisGrid_.IsBuilt()) {
                if (ImGui::SliderFloat("Visible Range", &visibleRange_, 0.0f, 2000.0f,
                                       visibleRange_ <= 0.0f ? "Infinite" : "%.0f units")) {
                    lastVisibleCells_.clear();
                }
                ImGui::Text("Cells: %d / %d",
                            solidVisGrid_.GetLastVisibleCellCount(),
                            solidVisGrid_.GetTotalCellCount());
            } else {
                ImGui::TextDisabled("Grid not built (load a map first)");
            }
            if (ImGui::Button("Rebuild Grid", ImVec2(-1.0f, 0.0f))) {
                BuildVisibilityGrids();
                lastVisibleCells_.clear();
            }
            ImGui::Checkbox("Debug Cells", &debugShowVisibilityCells_);
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Selection");
        if (selectedObject) {
            ImGui::PushStyleColor(ImGuiCol_Text, roseAccent);
            ImGui::TextUnformatted(selectedObject->nodeName.c_str());
            ImGui::PopStyleColor();
            if (!selectedObject->disabled) {
                if (ImGui::Button("Disable", ImVec2(-1.0f, 0.0f)))
                    SetDrawDisabled(*selectedObject, true);
            } else {
                if (ImGui::Button("Enable", ImVec2(-1.0f, 0.0f)))
                    SetDrawDisabled(*selectedObject, false);
            }
        } else {
            ImGui::TextDisabled("<none>");
        }

        ImGui::SeparatorText("Ray Cast");
        ImGui::Checkbox("Enable Click Pick",   &clickPickEnabled);
        ImGui::Checkbox("Include Transparent", &pickIncludeTransparent);
        ImGui::Checkbox("Include Decals",      &pickIncludeDecals);
        if (ImGui::Button("Clear Selection", ImVec2(-1.0f, 0.0f))) {
            InvalidateSelection();
        }
    }
    ImGui::End();

    // ── Properties (was Node Codex) ───────────────────────────────────────────
    if (ImGui::Begin("Properties")) {
        const auto& authoredEntities = scene_.GetAuthoredEntities();
        const SceneManager::AuthoredEntity* selectedEntity = nullptr;
        for (const auto& entity : authoredEntities) {
            if (entity.id == selectedSceneEntityId) {
                selectedEntity = &entity;
                break;
            }
        }

        if (selectedEntity) {
            static SceneManager::SceneEntityId cachedEntityId = 0;
            static char entityNameBuf[256] = {};
            if (cachedEntityId != selectedEntity->id) {
                cachedEntityId = selectedEntity->id;
                std::strncpy(entityNameBuf, selectedEntity->name.c_str(), sizeof(entityNameBuf) - 1);
                entityNameBuf[sizeof(entityNameBuf) - 1] = '\0';
            }
            ImGui::PushStyleColor(ImGuiCol_Text, roseAccent);
            ImGui::Text("Entity #%llu", static_cast<unsigned long long>(selectedEntity->id));
            ImGui::PopStyleColor();
            if (ImGui::InputText("Name", entityNameBuf, sizeof(entityNameBuf))) {
                scene_.SetEntityName(selectedEntity->id, entityNameBuf);
                MarkDirty();
            }
            ImGui::TextDisabled("Model: %s",
                                selectedEntity->modelPath.empty() ? "<empty>" : selectedEntity->modelPath.c_str());
            glm::vec3 pos = selectedEntity->position;
            glm::vec4 rot(selectedEntity->rotation.x, selectedEntity->rotation.y,
                          selectedEntity->rotation.z, selectedEntity->rotation.w);
            glm::vec3 scale = selectedEntity->scale;
            bool transformChanged = false;
            transformChanged |= ImGui::DragFloat3("Position", &pos.x, 0.05f);
            transformChanged |= ImGui::DragFloat4("Rotation (xyzw)", &rot.x, 0.01f);
            transformChanged |= ImGui::DragFloat3("Scale", &scale.x, 0.01f);
            if (transformChanged) {
                scene_.SetEntityTransform(selectedEntity->id, pos, glm::quat(rot.w, rot.x, rot.y, rot.z), scale);
                MarkDirty();
            }
            ImGui::Separator();
        }

        if (selectedObject) {
            // Node name in rose accent
            ImGui::PushStyleColor(ImGuiCol_Text, roseAccent);
            ImGui::TextUnformatted(selectedObject->nodeName.c_str());
            ImGui::PopStyleColor();
            // Type as disabled subtitle
            ImGui::TextDisabled("%s", selectedObject->modelNodeType.c_str());
            ImGui::Separator();
            ImGui::Text("Shader:   %s", selectedObject->shdr.c_str());
            ImGui::Text("Group:    %d", selectedObject->group);
            ImGui::Text("Index:    %d", selectedIndex);
            ImGui::Text("Disabled: %s", selectedObject->disabled ? "Yes" : "No");

            const Node* parserNode = selectedObject->sourceNode;
            if (parserNode && ImGui::TreeNode("Source Node")) {
                ImGui::Text("mesh_resource: %s", parserNode->mesh_ressource_id.c_str());
                ImGui::Text("node_type:     %s", parserNode->node_type.c_str());
                ImGui::Text("range:         %.1f – %.1f", parserNode->min_distance, parserNode->max_distance);
                ImGui::Text("string_attrs:  %d", static_cast<int>(parserNode->string_attrs.size()));
                ImGui::Text("float_params:  %d", static_cast<int>(parserNode->shader_params_float.size()));
                ImGui::Text("vec4_params:   %d", static_cast<int>(parserNode->shader_params_vec4.size()));
                ImGui::TreePop();
            }
        } else if (!selectedEntity) {
            ImGui::TextDisabled("No object selected.");
        }
    }
    ImGui::End();

    // ── Asset Explorer (bottom — Unity Project panel style) ──────────────────
    if (ImGui::Begin("Asset Explorer")) {
        if (ImGui::BeginTabBar("##AETabs")) {

            // ================================================================
            // PROJECT tab
            // ================================================================
            if (ImGui::BeginTabItem("Project")) {

                // ---- persistent state -----------------------------------------------
                // selCat: 0=root 1=Models(.n3) 2=Meshes(.nvx2) 3=Textures(.dds)
                //         4=Animations(.nac/.nax3) 5=Maps(.map)
                static char        searchBuf[128] = {};
                static int         selCat         = 0;
                static int         selItem        = -1;
                static std::string lastMapPath    = {};  // detect map reload
                static std::vector<std::string> cachedModels;
                static std::vector<std::string> cachedMeshes;
                static std::vector<std::string> cachedTextures;
                static std::vector<std::string> cachedAnims;
                static std::vector<std::string> cachedMaps;

                // ---- rebuild caches whenever the loaded map changes -----------------
                if (lastMapPath != scene_.GetCurrentMapPath()) {
                    lastMapPath = scene_.GetCurrentMapPath();
                    cachedModels.clear();
                    cachedMeshes.clear();
                    cachedTextures.clear();
                    cachedAnims.clear();
                    cachedMaps.clear();
                    selItem = -1;

                    // Models: one entry per unique model path loaded by the renderer
                    for (const auto& kv : scene_.GetLoadedModelRefCounts())
                        cachedModels.push_back(kv.first);

                    // Meshes: one entry per unique mesh path
                    for (const auto& kv : scene_.GetLoadedMeshPaths())
                        cachedMeshes.push_back(kv.first);

                    // Textures + Animations: harvest from every draw call's source node
                    std::unordered_set<std::string> texSet, animSet;
                    const auto harvest = [&](const auto& drawVec) {
                        for (const auto& d : drawVec) {
                            if (!d.sourceNode) continue;
                            // animations
                            if (!d.sourceNode->animation_resource.empty())
                                animSet.insert(d.sourceNode->animation_resource);
                            // textures (shader param map: name → path)
                            for (const auto& kv : d.sourceNode->shader_params_texture)
                                if (!kv.second.empty())
                                    texSet.insert(kv.second);
                        }
                    };
                    harvest(solidDraws);
                    harvest(alphaTestDraws);
                    harvest(environmentDraws);
                    harvest(environmentAlphaDraws);
                    harvest(simpleLayerDraws);
                    harvest(decalDraws);
                    harvest(waterDraws);
                    harvest(refractionDraws);
                    harvest(postAlphaUnlitDraws);

                    cachedTextures.assign(texSet.begin(), texSet.end());
                    cachedAnims.assign(animSet.begin(), animSet.end());

                    // Map: the currently loaded map file
                    if (!scene_.GetCurrentMapPath().empty())
                        cachedMaps.push_back(scene_.GetCurrentMapPath());

                    std::sort(cachedModels.begin(),   cachedModels.end());
                    std::sort(cachedMeshes.begin(),   cachedMeshes.end());
                    std::sort(cachedTextures.begin(), cachedTextures.end());
                    std::sort(cachedAnims.begin(),    cachedAnims.end());
                }

                // ---- helpers -------------------------------------------------------
                auto fileTail = [](const std::string& p) -> std::string {
                    const size_t s = p.find_last_of("/\\");
                    return s == std::string::npos ? p : p.substr(s + 1);
                };
                auto truncLabel = [](std::string s, int n = 13) -> std::string {
                    if (static_cast<int>(s.size()) > n) { s.resize(n - 2); s += ".."; }
                    return s;
                };
                auto matchSearch = [&](const std::string& name) -> bool {
                    if (searchBuf[0] == '\0') return true;
                    return name.find(searchBuf) != std::string::npos;
                };

                // ---- per-type icon styling (Model, Mesh, Texture, Anim, Map) ------
                // Each row: bg, bgHover, border, accentStripe, tag
                const ImU32 kBg[]     = { IM_COL32( 56, 38, 50,255), IM_COL32(20,46,60,255),
                                          IM_COL32( 56,40,16,255),   IM_COL32(36,24,58,255),
                                          IM_COL32( 20,46,28,255) };
                const ImU32 kBgH[]    = { IM_COL32( 80, 56,70,255), IM_COL32(30,64,84,255),
                                          IM_COL32( 80,58,22,255),   IM_COL32(52,36,82,255),
                                          IM_COL32( 30,66,40,255) };
                const ImU32 kBdr[]    = { IM_COL32(148, 58, 98,200), IM_COL32(40,112,156,200),
                                          IM_COL32(160,104, 26,200), IM_COL32(94, 56,160,200),
                                          IM_COL32( 42,134, 64,200) };
                const ImU32 kAccent[] = { IM_COL32(184, 68,116,180), IM_COL32(52,138,190,180),
                                          IM_COL32(194,134, 36,180), IM_COL32(120,74,194,180),
                                          IM_COL32( 54,168, 82,180) };
                const char* kTag[]    = { "N3 ", "NVX", "DDS", "NAC", "MAP" };

                // ================================================================
                // LEFT PANE — folder tree
                // ================================================================
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.046f, 0.038f, 0.043f, 1.0f));
                ImGui::BeginChild("##AETree", ImVec2(162.0f, -1.0f), false);
                ImGui::PopStyleColor();

                const auto treeLeaf = [&](const char* label, int cat) {
                    const bool active = (selCat == cat);
                    if (active) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.910f,0.478f,0.627f,1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.17f,0.09f,0.14f,1.0f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f,0.13f,0.20f,1.0f));
                    if (ImGui::Selectable(label, active, ImGuiSelectableFlags_SpanAllColumns))
                        { selCat = cat; selItem = -1; }
                    ImGui::PopStyleColor(active ? 3 : 2);
                };

                ImGui::Spacing();

                const ImGuiTreeNodeFlags kGroup =
                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

                // ★ Favorites
                if (ImGui::TreeNodeEx("##TFav", kGroup, "\xe2\x98\x85  Favorites")) {
                    treeLeaf("   All Models",    1);
                    treeLeaf("   All Meshes",    2);
                    treeLeaf("   All Textures",  3);
                    treeLeaf("   All Animations",4);
                    treeLeaf("   All Maps",      5);
                    ImGui::TreePop();
                }
                ImGui::Spacing();

                // Assets (drasa_online/work)
                if (ImGui::TreeNodeEx("##TAssets", kGroup, "Assets")) {
                    treeLeaf("   Models",     1);
                    treeLeaf("   Meshes",     2);
                    treeLeaf("   Textures",   3);
                    treeLeaf("   Animations", 4);
                    treeLeaf("   Maps",       5);
                    ImGui::TreePop();
                }
                ImGui::Spacing();

                // Packages placeholder
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.34f,0.28f,0.32f,1.0f));
                ImGui::TreeNodeEx("##TPkg",
                    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                    ImGuiTreeNodeFlags_SpanAvailWidth, "Packages");
                ImGui::PopStyleColor();

                ImGui::EndChild(); // ##AETree

                // ================================================================
                // RIGHT PANE — icon grid
                // ================================================================
                ImGui::SameLine(0.0f, 1.0f);
                ImGui::BeginChild("##AEGrid", ImVec2(-1.0f, -1.0f), false);

                // search bar
                ImGui::SetNextItemWidth(-60.0f);
                ImGui::InputTextWithHint("##AESrch", "Search...", searchBuf, sizeof(searchBuf));
                ImGui::SameLine();
                if (ImGui::SmallButton("Refresh"))
                    lastMapPath = {}; // force cache rebuild next frame
                ImGui::Separator();

                // breadcrumb + count
                const char* crumbs[] = {
                    "Assets",
                    "Assets / Models (.n3)",
                    "Assets / Meshes (.nvx2)",
                    "Assets / Textures (.dds)",
                    "Assets / Animations (.nac .nax3 .anims)",
                    "Assets / Maps (.map)"
                };
                const std::vector<std::string>* catVec[] = {
                    nullptr, &cachedModels, &cachedMeshes,
                    &cachedTextures, &cachedAnims, &cachedMaps
                };
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f,0.43f,0.51f,1.0f));
                if (selCat == 0) {
                    ImGui::TextUnformatted("Assets");
                } else {
                    ImGui::Text("%s  —  %d files",
                        crumbs[selCat],
                        static_cast<int>(catVec[selCat]->size()));
                }
                ImGui::PopStyleColor();
                if (scene_.GetCurrentMapPath().empty())
                    ImGui::TextDisabled("  — no map loaded");
                ImGui::Spacing();

                // grid geometry
                const float iconW = 64.0f, iconH = 52.0f;
                const float cellW = iconW + 14.0f;
                const float cellH = iconH + 22.0f;
                const float gapX  = 5.0f;
                const float avail = ImGui::GetContentRegionAvail().x;
                const int   cols  = std::max(1, static_cast<int>((avail + gapX) / (cellW + gapX)));

                const ImU32 cFolderN = IM_COL32(90, 83, 98,255);
                const ImU32 cFolderH = IM_COL32(114,106,126,255);
                const ImU32 cSelBg   = IM_COL32(118, 36, 74,110);
                const ImU32 cHovBg   = IM_COL32( 68, 46, 62, 70);
                const ImU32 cLabel   = IM_COL32(204,182,196,255);

                ImDrawList* dl = ImGui::GetWindowDrawList();
                int cellIdx = 0;

                // ---- drawCell(name, typeIdx)  typeIdx=-1 → folder ----
                auto drawCell = [&](const std::string& name, int typeIdx) -> bool {
                    if (cellIdx % cols != 0) ImGui::SameLine(0.0f, gapX);
                    ImGui::PushID(cellIdx);

                    const ImVec2 p   = ImGui::GetCursorScreenPos();
                    const bool   hit = ImGui::InvisibleButton("##ic", ImVec2(cellW, cellH));
                    const bool   hov = ImGui::IsItemHovered();
                    const bool   sel = (selItem == cellIdx);

                    if (sel) dl->AddRectFilled(p, ImVec2(p.x+cellW, p.y+cellH), cSelBg, 4.0f);
                    else if (hov) dl->AddRectFilled(p, ImVec2(p.x+cellW, p.y+cellH), cHovBg, 4.0f);

                    const float ix = p.x + (cellW - iconW) * 0.5f;
                    const float iy = p.y + 3.0f;

                    if (typeIdx < 0) {
                        // folder
                        const ImU32 fc = hov ? cFolderH : cFolderN;
                        dl->AddRectFilled(ImVec2(ix,            iy),
                                          ImVec2(ix+iconW*0.50f, iy+7.0f), fc, 2.0f);
                        dl->AddRectFilled(ImVec2(ix, iy+5.0f),
                                          ImVec2(ix+iconW, iy+iconH), fc, 3.0f);
                    } else {
                        // typed asset card
                        dl->AddRectFilled(ImVec2(ix,iy), ImVec2(ix+iconW,iy+iconH),
                                          hov ? kBgH[typeIdx] : kBg[typeIdx], 4.0f);
                        dl->AddRect(ImVec2(ix,iy), ImVec2(ix+iconW,iy+iconH),
                                    kBdr[typeIdx], 4.0f);
                        // accent stripe across top
                        dl->AddRectFilled(ImVec2(ix+3.0f, iy+1.5f),
                                          ImVec2(ix+iconW-3.0f, iy+4.5f),
                                          kAccent[typeIdx], 1.0f);
                        // type tag centred in card
                        const ImVec2 tpos(ix + iconW*0.5f - 9.0f, iy + iconH*0.5f - 7.0f);
                        dl->AddText(tpos, kAccent[typeIdx], kTag[typeIdx]);
                    }

                    // label below icon, centred + truncated
                    const std::string lbl = truncLabel(fileTail(name));
                    const float       lw  = ImGui::CalcTextSize(lbl.c_str()).x;
                    dl->AddText(ImVec2(p.x+(cellW-lw)*0.5f, p.y+iconH+5.0f), cLabel, lbl.c_str());

                    ImGui::PopID();
                    if (hit) selItem = cellIdx;
                    ++cellIdx;
                    return hit;
                };

                // ---- populate by category ----
                if (selCat == 0) {
                    // Assets root: category folders
                    if (drawCell("Models",     -1)) { selCat = 1; selItem = -1; }
                    if (drawCell("Meshes",     -1)) { selCat = 2; selItem = -1; }
                    if (drawCell("Textures",   -1)) { selCat = 3; selItem = -1; }
                    if (drawCell("Animations", -1)) { selCat = 4; selItem = -1; }
                    if (drawCell("Maps",       -1)) { selCat = 5; selItem = -1; }
                } else {
                    const std::vector<std::string>& items = *catVec[selCat];
                    const int typeIdx = selCat - 1; // 0=Model,1=Mesh,2=Tex,3=Anim,4=Map
                    const bool isMapCategory = (selCat == 5);
                    for (const auto& itemPath : items) {
                        if (!matchSearch(fileTail(itemPath))) continue;
                        const bool itemHit = drawCell(itemPath, typeIdx);
                        // Map items: double-click imports as editable scene
                        if (isMapCategory && ImGui::IsItemHovered() &&
                            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            RequestSceneAction(PendingSceneAction::ImportMap, itemPath);
                        }
                        (void)itemHit;
                    }
                    if (cellIdx == 0) {
                        if (scene_.GetCurrentMapPath().empty())
                            ImGui::TextDisabled("Load a map to populate assets.");
                        else
                            ImGui::TextDisabled("No resources of this type.");
                    }
                    if (isMapCategory && !items.empty()) {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Double-click a map to import as editable scene.");
                    }
                }

                // footer
                const float footH = ImGui::GetFrameHeightWithSpacing() + 4.0f;
                const float spare = ImGui::GetContentRegionAvail().y - footH;
                if (spare > 0.0f) ImGui::Dummy(ImVec2(0.0f, spare));
                ImGui::Separator();
                if (ImGui::Button("Rebuild Vis Grid")) BuildVisibilityGrids();
                ImGui::SameLine();
                if (ImGui::Button("Restore All"))      ClearDisabledDraws();

                ImGui::EndChild(); // ##AEGrid
                ImGui::EndTabItem();
            }

            // ================================================================
            // CONSOLE tab — draw history + metrics
            // ================================================================
            if (ImGui::BeginTabItem("Console")) {
                static float drawHistory[180] = {};
                static int   drawHistoryWrite = 0;
                drawHistory[drawHistoryWrite] = static_cast<float>(lastFrameDrawCalls_);
                drawHistoryWrite = (drawHistoryWrite + 1) % IM_ARRAYSIZE(drawHistory);

                float drawHistoryMax = 1.0f;
                for (float v : drawHistory)
                    if (v > drawHistoryMax) drawHistoryMax = v;

                ImGui::PlotLines("##DH", drawHistory, IM_ARRAYSIZE(drawHistory),
                                 drawHistoryWrite, nullptr, 0.0f,
                                 drawHistoryMax * 1.15f, ImVec2(-1.0f, 52.0f));
                ImGui::Spacing();

                ImGui::Text("%.1f fps", ImGui::GetIO().Framerate);
                ImGui::SameLine(); ImGui::TextDisabled("|");
                ImGui::SameLine(); ImGui::Text("%d draws", lastFrameDrawCalls_);
                ImGui::SameLine(); ImGui::TextDisabled("|");
                ImGui::SameLine(); ImGui::Text("pick %.2fms/%d", pickLastUpdateMs, pickCandidateCount);
                ImGui::SameLine(); ImGui::TextDisabled("|");
                ImGui::SameLine(); ImGui::Text("%.0fx%.0f", sceneViewportW_, sceneViewportH_);
                if (enableVisibilityGrid_ && solidVisGrid_.IsBuilt()) {
                    ImGui::SameLine(); ImGui::TextDisabled("|");
                    ImGui::SameLine(); ImGui::Text("cells %d/%d",
                        solidVisGrid_.GetLastVisibleCellCount(),
                        solidVisGrid_.GetTotalCellCount());
                }
                ImGui::EndTabItem();
            }

            // ================================================================
            // SCENES tab — project .ndscene list
            // ================================================================
            if (ImGui::BeginTabItem("Scenes")) {
                static std::vector<std::string> cachedSceneFiles;
                static bool scenesNeedRefresh = true;
                static int selSceneItem = -1;

                if (scenesNeedRefresh) {
                    cachedSceneFiles.clear();
                    std::error_code ec2;
                    const std::string scenesRoot = SCENES_ROOT;
                    if (!scenesRoot.empty() && std::filesystem::exists(scenesRoot, ec2)) {
                        for (const auto& entry : std::filesystem::directory_iterator(scenesRoot, ec2)) {
                            if (!entry.is_regular_file(ec2)) continue;
                            if (entry.path().extension() == ".ndscene") {
                                cachedSceneFiles.push_back(entry.path().string());
                            }
                        }
                    }
                    std::sort(cachedSceneFiles.begin(), cachedSceneFiles.end());
                    scenesNeedRefresh = false;
                }

                const std::string& activeScenePath = activeSceneInfo.sourcePath;
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f,0.43f,0.51f,1.0f));
                ImGui::Text("Scenes in %s  —  %d files", SCENES_ROOT, static_cast<int>(cachedSceneFiles.size()));
                ImGui::PopStyleColor();
                ImGui::Spacing();

                for (int i = 0; i < static_cast<int>(cachedSceneFiles.size()); ++i) {
                    const std::string& sp = cachedSceneFiles[i];
                    const std::string tail = [&]{ const size_t s = sp.find_last_of("/\\"); return s == std::string::npos ? sp : sp.substr(s+1); }();
                    const bool isActive = (sp == activeScenePath);
                    if (isActive) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.910f, 0.478f, 0.627f, 1.0f));
                    }
                    const std::string label = (isActive ? "\xe2\x97\x8f " : "  ") + tail + "##sc_" + std::to_string(i);
                    if (ImGui::Selectable(label.c_str(), selSceneItem == i)) {
                        selSceneItem = i;
                    }
                    if (isActive) ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        selSceneItem = i;
                        // Open directly — goes through dirty check
                        if (scene_.IsSceneDirty()) {
                            pendingSceneAction = PendingSceneAction::OpenScene;
                            pendingActionPath = sp;
                            unsavedChangesModalRequested = true;
                        } else {
                            if (scene_.OpenScene(sp)) {
                                ResetEditorRuntimeState(false);
                                hasWorkSceneAsset = false;
                                ++workSceneRevision;
                                MarkDirty();
                            }
                        }
                    }
                }
                if (cachedSceneFiles.empty()) {
                    ImGui::TextDisabled("No .ndscene files found in scenes root.");
                }
                ImGui::Spacing();
                if (ImGui::Button("Refresh")) scenesNeedRefresh = true;
                ImGui::SameLine();
                if (selSceneItem >= 0 && selSceneItem < static_cast<int>(cachedSceneFiles.size())) {
                    if (ImGui::Button("Open Selected")) {
                        const std::string& sp = cachedSceneFiles[static_cast<size_t>(selSceneItem)];
                        if (scene_.IsSceneDirty()) {
                            pendingSceneAction = PendingSceneAction::OpenScene;
                            pendingActionPath = sp;
                            unsavedChangesModalRequested = true;
                        } else {
                            if (scene_.OpenScene(sp)) {
                                ResetEditorRuntimeState(false);
                                hasWorkSceneAsset = false;
                                ++workSceneRevision;
                                MarkDirty();
                            }
                        }
                    }
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    // ── Status Bar (fixed bottom overlay) ────────────────────────────────────
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - 22.0f));
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 22.0f));
        ImGui::SetNextWindowViewport(vp->ID);
        const ImGuiWindowFlags statusFlags =
            ImGuiWindowFlags_NoTitleBar    |
            ImGuiWindowFlags_NoResize      |
            ImGuiWindowFlags_NoScrollbar   |
            ImGuiWindowFlags_NoInputs      |
            ImGuiWindowFlags_NoDocking     |
            ImGuiWindowFlags_NoNav         |
            ImGuiWindowFlags_NoMove        |
            ImGuiWindowFlags_NoSavedSettings;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.094f, 0.055f, 0.075f, 1.00f)); // #180E13
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("##StatusBar", nullptr, statusFlags);

        // Rose dot indicator
        ImGui::PushStyleColor(ImGuiCol_Text, roseAccent);
        ImGui::TextUnformatted("\xe2\x97\x8f"); // UTF-8 filled circle ●
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextUnformatted("N3 ENGINE");
        ImGui::SameLine(); ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("%s%s",
                    activeSceneInfo.name.empty() ? "Untitled Scene" : activeSceneInfo.name.c_str(),
                    activeSceneInfo.dirty ? "*" : "");
        ImGui::SameLine(); ImGui::TextDisabled("|");
        if (mapDropLoadStage_ != MapDropLoadStage::Idle) {
            ImGui::SameLine();
            ImGui::TextUnformatted(mapDropLoadStatus_.c_str());
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::ProgressBar(std::clamp(mapDropLoadProgress_, 0.0f, 1.0f), ImVec2(150.0f, 0.0f), "");
            ImGui::SameLine(); ImGui::TextDisabled("|");
        }
        ImGui::SameLine(); ImGui::Text("%.0f fps", ImGui::GetIO().Framerate);
        ImGui::SameLine(); ImGui::TextDisabled("|");
        ImGui::SameLine(); ImGui::Text("%d draws", lastFrameDrawCalls_);
        ImGui::SameLine(); ImGui::TextDisabled("|");
        // Mode badge
        {
            const bool isWork = (renderMode_ == RenderMode::Work);
            const ImVec4 badgeColor = isWork
                ? ImVec4(0.25f, 0.60f, 0.85f, 1.0f)   // blue for Work
                : ImVec4(0.30f, 0.75f, 0.35f, 1.0f);   // green for Play
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, badgeColor);
            ImGui::Text("[%s]", isWork ? "WORK" : "PLAY");
            ImGui::PopStyleColor();
            if (activePolicy_.dirtyRenderingEnabled) {
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextDisabled(dirtyFrameSkippedLast_ ? "(idle)" : "(dirty)");
            }
        }
        // Quality tier badge
        {
            const char* tierLabel[] = { "FULL", "REDUCED", "MIN" };
            const ImVec4 tierColor[] = {
                ImVec4(0.30f, 0.75f, 0.35f, 1.0f),  // green
                ImVec4(0.85f, 0.70f, 0.20f, 1.0f),  // yellow
                ImVec4(0.85f, 0.25f, 0.20f, 1.0f)    // red
            };
            int ti = static_cast<int>(budgetGovernor_.tier);
            ImGui::SameLine(); ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, tierColor[ti]);
            ImGui::Text("Q:%s", tierLabel[ti]);
            ImGui::PopStyleColor();
        }
        ImGui::SameLine(); ImGui::TextDisabled("|");
        if (scene_.GetCurrentMapPath().empty()) {
            ImGui::SameLine(); ImGui::TextDisabled("no map");
        } else {
            ImGui::SameLine(); ImGui::TextUnformatted("map loaded");
        }
        if (selectedObject) {
            ImGui::SameLine(); ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, roseHighlight);
            ImGui::TextUnformatted(selectedObject->nodeName.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
}

void DeferredRenderer::ShutdownImGui() {
    if (!imguiInitialized) return;
    imguiInitialized       = false;   // prevent re-entry before any cleanup
    editorDockInitialized_ = false;

    if (!ImGui::GetCurrentContext()) return;

    // Explicitly save layout while GL context and backends are still alive.
    // Then clear IniFilename so DestroyContext cannot attempt a second write
    // during CRT teardown, which causes STATUS_ACCESS_VIOLATION (0xC0000005).
    ImGuiIO& io = ImGui::GetIO();
    if (io.IniFilename) {
        ImGui::SaveIniSettingsToDisk(io.IniFilename);
        io.IniFilename = nullptr;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void DeferredRenderer::RenderLookAtPanel() {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Selection")) {
        ImGui::End();
        return;
    }

    // --- Scene stats ---
    ImGui::SeparatorText("Scene");

    // Count visible (non-disabled) draws per list — use pre-computed cache from ApplyDisabledDrawFlags()
    int solidTotal    = (int)solidDraws.size();
    int solidVisible  = uiCachedSolidVisible_;

    int alphaTotal   = (int)alphaTestDraws.size();
    int alphaVisible = uiCachedAlphaVisible_;

    int envTotal   = (int)environmentDraws.size();
    int envVisible = uiCachedEnvVisible_;

    int envAlphaTotal   = (int)environmentAlphaDraws.size();
    int simpleLayerTotal = (int)simpleLayerDraws.size();
    int decalTotal       = (int)decalDraws.size();
    int waterTotal       = (int)waterDraws.size();
    int refrTotal        = (int)refractionDraws.size();
    int postAlphaTotal   = (int)postAlphaUnlitDraws.size();

    const int sceneTotal = solidTotal + alphaTotal + envTotal + envAlphaTotal
                         + simpleLayerTotal + decalTotal + waterTotal
                         + refrTotal + postAlphaTotal;
    const int sceneVisible = solidVisible + alphaVisible + envVisible;

    ImGui::Text("Draw calls (last frame): %d", lastFrameDrawCalls_);
    ImGui::Text("Active batches: %d", (int)DrawBatchSystem::instance().activeBatches().size());
    ImGui::Spacing();
    ImGui::Text("Total scene objects: %d", sceneTotal);
    ImGui::Text("  Solid:       %d / %d", solidVisible, solidTotal);
    ImGui::Text("  AlphaTest:   %d / %d", alphaVisible, alphaTotal);
    ImGui::Text("  Env:         %d / %d", envVisible, envTotal);
    ImGui::Text("  EnvAlpha:    %d", envAlphaTotal);
    ImGui::Text("  SimpleLayer: %d", simpleLayerTotal);
    ImGui::Text("  Decals:      %d", decalTotal);
    ImGui::Text("  Water:       %d", waterTotal);
    ImGui::Text("  Refraction:  %d", refrTotal);
    ImGui::Text("  PostAlpha:   %d", postAlphaTotal);

    if (enableVisibilityGrid_ && solidVisGrid_.IsBuilt()) {
        ImGui::Spacing();
        ImGui::Text("Visibility grid: %d / %d cells",
                    solidVisGrid_.GetLastVisibleCellCount(),
                    solidVisGrid_.GetTotalCellCount());
    }

    // --- Selection ---
    ImGui::SeparatorText("Selection");

    ImGui::Text("Left click object in viewport to inspect.");
    ImGui::Checkbox("Enable Click Pick", &clickPickEnabled);
    ImGui::Checkbox("Include Transparent", &pickIncludeTransparent);
    ImGui::Checkbox("Include Decals", &pickIncludeDecals);
    if (ImGui::Button("Clear Selection")) {
        InvalidateSelection();
    }

    ImGui::Separator();
    ImGui::Text("Pick Cost: %.3f ms", pickLastUpdateMs);
    ImGui::Text("Candidates: %d", pickCandidateCount);

    if (selectedObject) {
        ImGui::Text("Node: %s", selectedObject->nodeName.c_str());
        ImGui::Text("Type: %s", selectedObject->modelNodeType.c_str());
        ImGui::Text("Shader: %s", selectedObject->shdr.c_str());
        ImGui::Text("Group: %d", selectedObject->group);
        ImGui::Text("Index: %d", selectedIndex);
        ImGui::Text("Disabled: %s", selectedObject->disabled ? "Yes" : "No");
        if (!selectedObject->disabled) {
            if (ImGui::Button("Disable Selected")) {
                SetDrawDisabled(*selectedObject, true);
            }
        } else {
            if (ImGui::Button("Enable Selected")) {
                SetDrawDisabled(*selectedObject, false);
            }
        }

        const Node* parserNode = selectedObject->sourceNode;
        if (parserNode) {
            ImGui::SeparatorText("Parser Core Parameters");

            if (ImGui::TreeNode("Core Node Data")) {
                ImGui::Text("node_name: %s", parserNode->node_name.c_str());
                ImGui::Text("node_type: %s", parserNode->node_type.c_str());
                ImGui::Text("model_node_type: %s", parserNode->model_node_type.c_str());
                ImGui::Text("mesh_resource: %s", parserNode->mesh_ressource_id.c_str());
                ImGui::Text("primitive_group_idx: %d", parserNode->primitive_group_idx);
                ImGui::Text("shader: %s", parserNode->shader.c_str());
                ImGui::Text("hrch_flag: %s", parserNode->hrch_flag ? "true" : "false");
                ImGui::Text("cash_flag: %s", parserNode->cash_flag ? "true" : "false");
                ImGui::Text("in_view_space: %s", parserNode->in_view_space ? "true" : "false");
                ImGui::Text("locked_to_viewer: %s", parserNode->locked_to_viewer ? "true" : "false");
                ImGui::Text("min_distance: %.3f", parserNode->min_distance);
                ImGui::Text("max_distance: %.3f", parserNode->max_distance);
                ImGui::Text("animation_resource: %s", parserNode->animation_resource.c_str());
                ImGui::Text("variations_resource: %s", parserNode->variations_resource.c_str());
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Transform")) {
                ImGui::Text("position: (%.4f, %.4f, %.4f, %.4f)",
                            parserNode->position.x, parserNode->position.y, parserNode->position.z, parserNode->position.w);
                ImGui::Text("rotation: (%.4f, %.4f, %.4f, %.4f)",
                            parserNode->rotation.x, parserNode->rotation.y, parserNode->rotation.z, parserNode->rotation.w);
                ImGui::Text("scale: (%.4f, %.4f, %.4f, %.4f)",
                            parserNode->scale.x, parserNode->scale.y, parserNode->scale.z, parserNode->scale.w);
                ImGui::Text("rotate_pivot: (%.4f, %.4f, %.4f, %.4f)",
                            parserNode->rotate_pivot.x, parserNode->rotate_pivot.y, parserNode->rotate_pivot.z, parserNode->rotate_pivot.w);
                ImGui::Text("scale_pivot: (%.4f, %.4f, %.4f, %.4f)",
                            parserNode->scale_pivot.x, parserNode->scale_pivot.y, parserNode->scale_pivot.z, parserNode->scale_pivot.w);
                ImGui::Text("local_box_min: (%.4f, %.4f, %.4f, %.4f)",
                            parserNode->local_box_min.x, parserNode->local_box_min.y, parserNode->local_box_min.z, parserNode->local_box_min.w);
                ImGui::Text("local_box_max: (%.4f, %.4f, %.4f, %.4f)",
                            parserNode->local_box_max.x, parserNode->local_box_max.y, parserNode->local_box_max.z, parserNode->local_box_max.w);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Shader Params (Texture)")) {
                if (parserNode->shader_params_texture.empty()) {
                    ImGui::TextUnformatted("<none>");
                } else {
                    for (const auto& [key, value] : parserNode->shader_params_texture) {
                        ImGui::Text("%s = %s", key.c_str(), value.c_str());
                    }
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Shader Params (Int)")) {
                if (parserNode->shader_params_int.empty()) {
                    ImGui::TextUnformatted("<none>");
                } else {
                    for (const auto& [key, value] : parserNode->shader_params_int) {
                        ImGui::Text("%s = %d", key.c_str(), value);
                    }
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Shader Params (Float)")) {
                if (parserNode->shader_params_float.empty()) {
                    ImGui::TextUnformatted("<none>");
                } else {
                    for (const auto& [key, value] : parserNode->shader_params_float) {
                        ImGui::Text("%s = %.6f", key.c_str(), value);
                    }
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Shader Params (Vec4)")) {
                if (parserNode->shader_params_vec4.empty()) {
                    ImGui::TextUnformatted("<none>");
                } else {
                    for (const auto& [key, value] : parserNode->shader_params_vec4) {
                        ImGui::Text("%s = (%.4f, %.4f, %.4f, %.4f)", key.c_str(), value.x, value.y, value.z, value.w);
                    }
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Shader Params (MLP UV Stretch)")) {
                if (parserNode->shader_params_mlp_uv_stretch.empty()) {
                    ImGui::TextUnformatted("<none>");
                } else {
                    for (const auto& [index, value] : parserNode->shader_params_mlp_uv_stretch) {
                        ImGui::Text("[%d] = (%.4f, %.4f, %.4f, %.4f)", index, value.x, value.y, value.z, value.w);
                    }
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Shader Params (MLP Spec Intensity)")) {
                if (parserNode->shader_params_mlp_spec_intensity.empty()) {
                    ImGui::TextUnformatted("<none>");
                } else {
                    for (const auto& [index, value] : parserNode->shader_params_mlp_spec_intensity) {
                        ImGui::Text("[%d] = (%.4f, %.4f, %.4f, %.4f)", index, value.x, value.y, value.z, value.w);
                    }
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("String Attributes")) {
                if (parserNode->string_attrs.empty()) {
                    ImGui::TextUnformatted("<none>");
                } else {
                    for (const auto& [key, value] : parserNode->string_attrs) {
                        ImGui::Text("%s = %s", key.c_str(), value.c_str());
                    }
                }
                ImGui::TreePop();
            }
        } else {
            ImGui::SeparatorText("Parser Core Parameters");
            ImGui::TextUnformatted("<source node unavailable>");
        }
    } else {
        ImGui::Text("Node: <none>");
    }

    ImGui::SeparatorText("Visibility System");
    if (ImGui::Checkbox("Enable Visibility Grid", &enableVisibilityGrid_)) {
        lastVisibleCells_.clear();
        if (!enableVisibilityGrid_) {
            visibleCells_.clear();
        }
    }
    if (enableVisibilityGrid_) {
        if (solidVisGrid_.IsBuilt()) {
            if (ImGui::SliderFloat("Visible Range", &visibleRange_, 0.0f, 2000.0f,
                                   visibleRange_ <= 0.0f ? "Infinite" : "%.0f units")) {
                lastVisibleCells_.clear();
            }
            ImGui::Text("Cells visible:      %d / %d",
                        solidVisGrid_.GetLastVisibleCellCount(),
                        solidVisGrid_.GetTotalCellCount());
            ImGui::Text("Solid draws:        %d / %d",
                        solidVisGrid_.GetLastVisibleDrawCount(),
                        (int)solidDraws.size());
            ImGui::Text("AlphaTest draws:    %d / %d",
                        alphaTestVisGrid_.GetLastVisibleDrawCount(),
                        (int)alphaTestDraws.size());
            ImGui::Text("Env draws:          %d / %d",
                        envVisGrid_.GetLastVisibleDrawCount(),
                        (int)environmentDraws.size());
        } else {
            ImGui::TextDisabled("Grid not built (load a map first)");
        }
        if (ImGui::Button("Rebuild Grid Now")) {
            BuildVisibilityGrids();
            lastVisibleCells_.clear();
        }
        ImGui::Checkbox("Debug: Show Cells", &debugShowVisibilityCells_);
    }

    ImGui::SeparatorText("Disabled Objects");
    ImGui::BeginChild("DisabledObjects", ImVec2(0.0f, 140.0f), true);
    int visibleCount = 0;
    for (size_t i = 0; i < disabledDrawOrder.size(); ++i) {
        const DisabledDrawKey& key = disabledDrawOrder[i];
        if (disabledDrawSet.find(key) == disabledDrawSet.end()) continue;
        visibleCount++;

        std::string label = key.nodeName + " | " + key.shdr + " | g=" + std::to_string(key.group);
        label += "##disabled_" + std::to_string(i);
        const bool selected = (disabledSelectionIndex == static_cast<int>(i));
        if (ImGui::Selectable(label.c_str(), selected)) {
            disabledSelectionIndex = static_cast<int>(i);
        }
    }
    if (visibleCount == 0) {
        ImGui::TextUnformatted("<none>");
    }
    ImGui::EndChild();

    if (ImGui::Button("Enable Highlighted")) {
        if (disabledSelectionIndex >= 0 &&
            disabledSelectionIndex < static_cast<int>(disabledDrawOrder.size())) {
            const DisabledDrawKey key = disabledDrawOrder[disabledSelectionIndex];
            disabledDrawSet.erase(key);
            disabledSelectionIndex = -1;
            ApplyDisabledDrawFlags();
            lastVisibleCells_.clear(); // force visibility to re-apply next frame
            DrawBatchSystem::instance().reset(true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Enable All")) {
        ClearDisabledDraws();
    }

    ImGui::End();
}


void DeferredRenderer::RenderViewportOnlyImage() {
    sceneViewportValid_ = false;
    sceneViewportHovered_ = false;
    sceneViewportFocused_ = false;

    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    if (!mainViewport) return;

    ImVec2 viewportSize = mainViewport->WorkSize;
    if (viewportSize.x < 1.0f) viewportSize.x = 1.0f;
    if (viewportSize.y < 1.0f) viewportSize.y = 1.0f;

    GLuint finalTexture = 0;
    if (lightingGraph) {
        auto finalTextureInterface = lightingGraph->getTextureInterface("sceneColor");
        if (finalTextureInterface) {
            finalTexture = *(GLuint*)finalTextureInterface->GetNativeHandle();
        }
    }

    if (finalTexture != 0) {
        const ImVec2 p0 = mainViewport->WorkPos;
        const ImVec2 p1(p0.x + viewportSize.x, p0.y + viewportSize.y);
        ImDrawList* drawList = ImGui::GetBackgroundDrawList(const_cast<ImGuiViewport*>(mainViewport));
        drawList->PushClipRect(p0, p1, true);
        drawList->AddImage(
            reinterpret_cast<void*>(static_cast<intptr_t>(finalTexture)),
            p0,
            p1,
            ImVec2(0.0f, 1.0f),
            ImVec2(1.0f, 0.0f));
        drawList->PopClipRect();

        sceneViewportX_ = p0.x;
        sceneViewportY_ = p0.y;
        sceneViewportW_ = std::max(0.0f, p1.x - p0.x);
        sceneViewportH_ = std::max(0.0f, p1.y - p0.y);
        sceneViewportValid_ = sceneViewportW_ > 1.0f && sceneViewportH_ > 1.0f;

        const ImVec2 mousePos = ImGui::GetIO().MousePos;
        sceneViewportHovered_ =
            sceneViewportValid_ &&
            mousePos.x >= sceneViewportX_ &&
            mousePos.x <= (sceneViewportX_ + sceneViewportW_) &&
            mousePos.y >= sceneViewportY_ &&
            mousePos.y <= (sceneViewportY_ + sceneViewportH_);
        sceneViewportFocused_ = sceneViewportValid_;
    }
}


void DeferredRenderer::RenderImGui() {
    if (!imguiInitialized) return;
    if (!imguiViewportOnly_) {
        ValidateSelectionPointer();
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    frameProfile_.panelUpdateHzEffective = panelEffectiveHz_;

    if (imguiViewportOnly_) {
        RenderViewportOnlyImage();
    } else if (editorModeEnabled_) {
        RenderEditorShell();
    } else {
        RenderLookAtPanel();
    }

    if (!imguiViewportOnly_) {
        DrawVisibilityCellsDebug();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
