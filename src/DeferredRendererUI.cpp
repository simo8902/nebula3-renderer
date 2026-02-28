// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "DeferredRenderer.h"
#include "DeferredRendererAnimation.h"
#include "SelectionRaycaster.h"
#include "Parser.h"
#include "GLStateDebug.h"
#include "AnimationSystem.h"

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "rendering/opengl/OpenGLDevice.h"
#include "rendering/opengl/GLFWPlatform.h"
#include "rendering/opengl/OpenGLShaderManager.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include "DrawBatchSystem.h"
#include "MegaBuffer.h"
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "gtc/quaternion.hpp"
#include "gtx/string_cast.hpp"
#include "Model/ModelServer.h"
#include "Servers/MeshServer.h"
#include "Servers/TextureServer.h"
#include "Map/MapHeader.h"
#include "ParticleData/ParticleServer.h"
#include "NC.Logger.h"
#include "gtx/norm.hpp"

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

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Persist window positions, sizes, and dock layout between sessions.
    // File is written next to the executable on shutdown via DestroyContext().
    io.IniFilename = "n3_editor_layout.ini";
    ApplyEditorTheme(io);
    editorModeEnabled_ = ReadEditorEnvToggle("NDEVC_EDITOR_MODE");
    editorViewportInputRouting_ = ReadEditorEnvToggle("NDEVC_EDITOR_ROUTE_INPUT", true);

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

    int solidTotal = static_cast<int>(solidDraws.size());
    int solidVisible = 0;
    for (const auto& d : solidDraws) {
        if (!d.disabled) ++solidVisible;
    }
    int alphaTotal = static_cast<int>(alphaTestDraws.size());
    int alphaVisible = 0;
    for (const auto& d : alphaTestDraws) {
        if (!d.disabled) ++alphaVisible;
    }
    int envTotal = static_cast<int>(environmentDraws.size());
    int envVisible = 0;
    for (const auto& d : environmentDraws) {
        if (!d.disabled) ++envVisible;
    }
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
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            ImGui::MenuItem("Open", "Ctrl+O", false, false);
            ImGui::MenuItem("Save", "Ctrl+S", false, false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
            ImGui::MenuItem("Redo", "Ctrl+Y", false, false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset Layout")) {
                layoutResetPending   = true;   // force rebuild even if ini exists
                editorDockInitialized_ = false;
            }
            ImGui::MenuItem("Route Input To Viewport", nullptr, &editorViewportInputRouting_);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
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
        if (ImGui::Begin("ActionStrip", nullptr, toolbarFlags)) {
            // Transform buttons
            if (ImGui::Button("Select"))    {}  ImGui::SameLine();
            if (ImGui::Button("Translate")) {}  ImGui::SameLine();
            if (ImGui::Button("Rotate"))    {}  ImGui::SameLine();
            if (ImGui::Button("Scale"))     {}  ImGui::SameLine();

            // Vertical separator
            ImGui::TextDisabled("|");  ImGui::SameLine();

            // Pick checkboxes
            ImGui::Checkbox("Pick",        &clickPickEnabled);       ImGui::SameLine();
            ImGui::Checkbox("Transparent", &pickIncludeTransparent); ImGui::SameLine();
            ImGui::Checkbox("Decals",      &pickIncludeDecals);      ImGui::SameLine();

            // Vertical separator
            ImGui::TextDisabled("|");  ImGui::SameLine();

            // Centered playback controls
            const float windowWidth    = ImGui::GetWindowWidth();
            const float buttonWidth    = 72.0f;
            const float groupW         = buttonWidth * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
            const float centerX        = (windowWidth - groupW) * 0.5f;
            if (centerX > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(centerX);
            if (ImGui::Button("Play",  ImVec2(buttonWidth, 0.0f))) {}  ImGui::SameLine();
            if (ImGui::Button("Pause", ImVec2(buttonWidth, 0.0f))) {}  ImGui::SameLine();
            if (ImGui::Button("Step",  ImVec2(buttonWidth, 0.0f))) {}
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
    }

    // ── Viewport ──────────────────────────────────────────────────────────────
    if (ImGui::Begin("Viewport")) {
        sceneViewportFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        if (viewportSize.x < 1.0f) viewportSize.x = 1.0f;
        if (viewportSize.y < 1.0f) viewportSize.y = 1.0f;

        GLuint finalTexture = 0;
        if (lightingGraph) {
            finalTexture = lightingGraph->getTexture("sceneColor");
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

            // Overlay card — rose/graphite
            ImDrawList* drawList = ImGui::GetWindowDrawList();
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
            std::string overlayLine = std::to_string(sceneVisible) + "/" + std::to_string(sceneTotal)
                                    + " objects  \xc2\xb7  ";
            overlayLine += selectedObject ? selectedObject->nodeName : "<none>";
            drawList->AddText(ImVec2(cardMin.x + 10.0f, cardMin.y + 33.0f),
                              IM_COL32(170, 110, 148, 255),
                              overlayLine.c_str());
        } else {
            ImGui::TextUnformatted("Scene texture is unavailable.");
        }
    }
    ImGui::End();

    // ── Hierarchy (left — Unity-style scene object list) ─────────────────────
    if (ImGui::Begin("Hierarchy")) {
        // Colored draw-type rows
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.82f, 0.45f, 1.0f)); // green
        ImGui::Text("Solid        %d / %d", solidVisible, solidTotal);
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.72f, 0.30f, 1.0f)); // amber
        ImGui::Text("AlphaTest    %d / %d", alphaVisible, alphaTotal);
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.42f, 0.72f, 0.96f, 1.0f)); // blue
        ImGui::Text("Environment  %d / %d", envVisible, envTotal);
        ImGui::PopStyleColor();

        ImGui::Text("Particles    %d", static_cast<int>(particleNodes.size()));

        // Thin rose visibility ratio bar
        if (sceneTotal > 0) {
            const float ratio = static_cast<float>(sceneVisible) / static_cast<float>(sceneTotal);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, roseAccent);
            ImGui::ProgressBar(ratio, ImVec2(-1.0f, 4.0f), "");
            ImGui::PopStyleColor();
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
            selectedObject = nullptr;
            selectedIndex  = -1;
            cachedObj      = DrawCmd{};
            cachedIndex    = -1;
        }
    }
    ImGui::End();

    // ── Properties (was Node Codex) ───────────────────────────────────────────
    if (ImGui::Begin("Properties")) {
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
        } else {
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
                if (lastMapPath != currentMapSourcePath_) {
                    lastMapPath = currentMapSourcePath_;
                    cachedModels.clear();
                    cachedMeshes.clear();
                    cachedTextures.clear();
                    cachedAnims.clear();
                    cachedMaps.clear();
                    selItem = -1;

                    // Models: one entry per unique model path loaded by the renderer
                    for (const auto& kv : loadedModelRefCountByPath_)
                        cachedModels.push_back(kv.first);

                    // Meshes: one entry per unique mesh path
                    for (const auto& kv : loadedMeshByModelPath_)
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
                    if (!currentMapSourcePath_.empty())
                        cachedMaps.push_back(currentMapSourcePath_);

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
                if (currentMapSourcePath_.empty())
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
                    for (const auto& path : items) {
                        if (!matchSearch(fileTail(path))) continue;
                        drawCell(path, typeIdx);
                    }
                    if (cellIdx == 0) {
                        if (currentMapSourcePath_.empty())
                            ImGui::TextDisabled("Load a map to populate assets.");
                        else
                            ImGui::TextDisabled("No resources of this type.");
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
        ImGui::SameLine(); ImGui::Text("%.0f fps", ImGui::GetIO().Framerate);
        ImGui::SameLine(); ImGui::TextDisabled("|");
        ImGui::SameLine(); ImGui::Text("%d draws", lastFrameDrawCalls_);
        ImGui::SameLine(); ImGui::TextDisabled("|");
        if (currentMapSourcePath_.empty()) {
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

    // Count visible (non-disabled) draws per list
    int solidTotal    = (int)solidDraws.size();
    int solidVisible  = 0;
    for (const auto& d : solidDraws) if (!d.disabled) ++solidVisible;

    int alphaTotal   = (int)alphaTestDraws.size();
    int alphaVisible = 0;
    for (const auto& d : alphaTestDraws) if (!d.disabled) ++alphaVisible;

    int envTotal   = (int)environmentDraws.size();
    int envVisible = 0;
    for (const auto& d : environmentDraws) if (!d.disabled) ++envVisible;

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
        selectedObject = nullptr;
        selectedIndex = -1;
        cachedObj = DrawCmd{};
        cachedIndex = -1;
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
    ImGui::Checkbox("Enable Visibility Grid", &enableVisibilityGrid_);
    if (enableVisibilityGrid_) {
        if (solidVisGrid_.IsBuilt()) {
            ImGui::SliderFloat("Visible Range", &visibleRange_, 0.0f, 2000.0f,
                               visibleRange_ <= 0.0f ? "Infinite" : "%.0f units");
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


void DeferredRenderer::RenderImGui() {
    if (!imguiInitialized) return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (editorModeEnabled_) {
        RenderEditorShell();
    } else {
        RenderLookAtPanel();
    }

    DrawVisibilityCellsDebug();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
