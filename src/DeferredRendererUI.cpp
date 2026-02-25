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
#include <unordered_set>

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

bool DeferredRenderer::InitializeImGui() {
    if (imguiInitialized) return true;
    GLFWwindow* glfwWin = static_cast<GLFWwindow*>(window_ ? window_->GetNativeHandle() : nullptr);
    if (!glfwWin) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

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

void DeferredRenderer::ShutdownImGui() {
    if (!imguiInitialized) return;
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    imguiInitialized = false;
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

    RenderLookAtPanel();

    DrawVisibilityCellsDebug();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
