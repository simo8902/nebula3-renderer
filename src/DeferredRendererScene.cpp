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

static constexpr bool kLogGroundDecalReceiveSolidDiffuse = false;
static constexpr bool kParticleEmitterTransformsAreStatic = false;

namespace {
bool ReadEnvToggle(const char* name) {
    if (!name || !name[0]) return false;
#if defined(_WIN32)
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) {
        return false;
    }
    const bool enabled = value[0] != '\0' && value[0] != '0';
    std::free(value);
    return enabled;
#else
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && value[0] != '0';
#endif
}

bool ParticlesDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_PARTICLES");
    return disabled;
}

bool AnimationsDisabled() {
    static const bool disabled = ReadEnvToggle("NDEVC_DISABLE_ANIMATIONS");
    return disabled;
}

bool MaterialInputDebugEnabled() {
    static const bool enabled = ReadEnvToggle("NDEVC_DEBUG_MATERIAL_INPUTS");
    return enabled;
}
}

ModelInstance* DeferredRenderer::appendN3WTransform(const std::string& path,
    const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale) {
    Reporter rep;
    Options opt;

    auto model = ModelServer::instance().loadModel(path, rep, opt);
    if (!model) {
        std::cerr << "[FAIL] load model " << path << "\n";
        return nullptr;
    }

    auto inst = ModelServer::instance().createInstance(model);
    inst->setTransform(pos, rot, scale);
    instanceSpawnTimes[inst.get()] = glfwGetTime();

    if (!AnimationsDisabled() && DeferredRendererAnimation::IsAnimationEnabled()) {
        std::unordered_map<std::string, Node*> modelNodeMap;
        modelNodeMap.reserve(model->getNodes().size());
        for (auto* n : model->getNodes()) {
            if (!n) continue;
            modelNodeMap[n->node_name] = const_cast<Node*>(n);
        }
        RegisterAnimationOwnerNodes(inst.get(), modelNodeMap);

        for (auto* node : model->getNodes()) {
            if (!node) continue;

            if (!node->animation_resource.empty()) {
                PlayClip(node->animation_resource, node->node_name, 0, true, inst.get());
            }

            if (node->animSections.empty()) continue;

            for (size_t i = 0; i < node->animSections.size(); ++i) {
                const auto& section = node->animSections[i];
                if (section.animationNodeType != TransformCurveAnimator) continue;

                const bool loop = (section.loopType == AnimLoopType::Loop);
                const bool saniLooksLikeSource = DeferredRendererAnimation::LooksLikeAnimationResourcePath(section.animationName);
                std::string clipSource = node->animation_resource;
                if (clipSource.empty() && !section.animationName.empty()) {
                    clipSource = section.animationName;
                }
                if (clipSource.empty()) continue;

                auto playSectionClip = [&](const std::string& targetNode) {
                    if (targetNode.empty()) return;

                    // Common authored layout:
                    // ANIM=resource path, SANI=clip name, SAGR=clip index override.
                    if (!node->animation_resource.empty() &&
                        !section.animationName.empty() &&
                        !saniLooksLikeSource) {
                        PlayClip(clipSource, targetNode, section.animationName, loop, inst.get());
                    } else {
                        PlayClip(clipSource, targetNode, section.animationGroup, loop, inst.get());
                    }
                };

                bool playedAnyTarget = false;
                for (const auto& animPath : section.animatedNodesPath) {
                    if (animPath.empty()) continue;
                    std::string targetNode = DeferredRendererAnimation::LeafNodeName(animPath);
                    if (targetNode.empty()) continue;
                    playSectionClip(targetNode);
                    playedAnyTarget = true;
                }

                if (!playedAnyTarget) {
                    playSectionClip(node->node_name);
                }
            }

            bool hasInstanceAnimations = false;
            for (const auto& section : node->animSections) {
                if (section.animationNodeType == FloatAnimator ||
                    section.animationNodeType == Float4Animator ||
                    section.animationNodeType == IntAnimator ||
                    section.animationNodeType == TransformAnimator ||
                    section.animationNodeType == TransformCurveAnimator ||
                    section.animationNodeType == UvAnimator) {
                    hasInstanceAnimations = true;
                    break;
                }
            }

            if (hasInstanceAnimations) {
                auto animatorInstance = std::make_unique<AnimatorNodeInstance>();
                animatorInstance->Setup(node, modelNodeMap, inst.get());
                animatorInstance->OnShow(glfwGetTime());
                animatorInstances.push_back(std::move(animatorInstance));
            }
        }
    }

    std::vector<DrawCmd> temp;
    BuildDrawsWithTransform(*model, inst->getTransform(), inst.get(), temp);
    
    for (auto& dc : temp) {
        std::string nodeNameLower = dc.nodeName;
        std::transform(nodeNameLower.begin(), nodeNameLower.end(), nodeNameLower.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        std::string nodeTypeLower = dc.modelNodeType;
        std::transform(nodeTypeLower.begin(), nodeTypeLower.end(), nodeTypeLower.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        std::string shaderNameLower = dc.shdr;
        std::transform(shaderNameLower.begin(), shaderNameLower.end(), shaderNameLower.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });

        bool isParticle =
            nodeNameLower.find("pemitter") != std::string::npos ||
            nodeTypeLower.find("particle") != std::string::npos ||
            shaderNameLower.find("particle") != std::string::npos;
        bool isRefraction = dc.shdr == "shd:refraction";
        bool isEnvironment =
            shaderNameLower.find("environment") != std::string::npos ||
            shaderNameLower.find("envirionment") != std::string::npos;
        bool isSimpleLayer =
            dc.shdr.find("simplelayer") != std::string::npos;
        bool isDecalShader = (shaderNameLower == "shd:decal") || (shaderNameLower.find("decal") != std::string::npos) || dc.isDecal;
        bool isWater = dc.shdr == "shd:water";
        bool isPostAlphaUnlit = (nodeTypeLower.find("postalphaunlit") != std::string::npos);
        bool isAdditiveType = (nodeTypeLower == "additive");
        bool isAlphaBlendType = (nodeTypeLower == "alpha" || nodeTypeLower == "alphablend");
        bool isUvAnimated = (dc.shdr == "shd:uvanimated");

        if (isParticle && ParticlesDisabled()) {
            continue;
        }

        if (isParticle) {
            for (auto* node : model->getNodes()) {
                if (node && node->node_name == dc.nodeName) {
                    auto pnode = std::make_shared<Particles::ParticleSystemNode>();
                    if (pnode->Setup(node, dc.mesh)) {
                        auto pickParticleTexture = [](const Node* n) -> std::string {
                            if (!n) return {};
                            auto isPlaceholder = [](const std::string& texId) {
                                if (texId.empty()) return true;
                                std::string v = texId;
                                std::transform(v.begin(), v.end(), v.begin(),
                                               [](unsigned char c) { return (char)std::tolower(c); });
                                return v == "tex:system/white" || v == "system/white" ||
                                       v == "tex:system/black" || v == "system/black" ||
                                       v == "tex:system/normal" || v == "system/normal";
                            };
                            static const char* keys[] = {
                                "ParticleMap",
                                "MainTex",
                                "Texture",
                                "AlbedoMap",
                                "EmissiveMap",
                                "DiffMap0",
                                "DiffMap",
                                "MaskMap"
                            };
                            for (const char* key : keys) {
                                auto it = n->shader_params_texture.find(key);
                                if (it != n->shader_params_texture.end() &&
                                    !it->second.empty() &&
                                    !isPlaceholder(it->second)) {
                                    return it->second;
                                }
                            }
                            if (!n->shader_params_texture.empty()) {
                                for (const auto& [_, value] : n->shader_params_texture) {
                                    if (!isPlaceholder(value)) return value;
                                }
                            }
                            return {};
                        };

                        GLuint particleTex = 0;
                        const std::string texValue = pickParticleTexture(node);
                        if (!texValue.empty()) {
                            particleTex = TextureServer::instance().loadTexture(texValue);
                            if (particleTex) {
                                pnode->SetParticleTexture(particleTex);
                            } else {
                                std::cerr << "[PARTICLE TEX] FAILED to load '" << texValue << "'\n";
                            }
                        } else {
                            std::cerr << "[PARTICLE TEX] no texture param found for node '" << node->node_name << "'\n";
                        }
                        if (!particleTex) {
                            pnode->SetParticleTexture(whiteTex);
                        }

                        const bool dynamicParticleTransform =
                            (!kParticleEmitterTransformsAreStatic) && dc.hasPotentialTransformAnimation;
                        particleNodes.push_back(ParticleAttach{
                            pnode, dc.nodeName, dc.sourceNode, dc.rootMatrix, dc.instance, dynamicParticleTransform
                        });
                        auto psInst = pnode->GetInstance();
                        if (psInst) {
                            psInst->SetTransform(dc.worldMatrix);
                            psInst->SetRenderer(Particles::ParticleServer::Instance().GetParticleRenderer());
                        }
                    } else {
                        std::cerr << "[PARTICLE INIT] setup failed for node '" << node->node_name
                                  << "' (shader='" << node->shader
                                  << "', type='" << node->model_node_type
                                  << "', mesh='" << node->mesh_ressource_id << "')\n";
                    }
                    break;
                }
            }
        }
        else if (isWater) {
            waterDraws.push_back(std::move(dc));
        }
        else if (isEnvironment) {
            environmentDraws.push_back(std::move(dc));
        } else if ((isUvAnimated && isPostAlphaUnlit) || isPostAlphaUnlit) {
            postAlphaUnlitDraws.push_back(std::move(dc));
        }
        else if (isRefraction) {
            // PASS DISABLED: refraction
            // refractionDraws.push_back(std::move(dc));
        } else if (isAdditiveType || isAlphaBlendType) {
            postAlphaUnlitDraws.push_back(std::move(dc));
        } else if (isSimpleLayer) {
            simpleLayerDraws.push_back(std::move(dc));
        } else if (isDecalShader) {
            decalDraws.push_back(std::move(dc));
        } else {
            if (dc.alphaTest) alphaTestDraws.push_back(std::move(dc));
            else solidDraws.push_back(std::move(dc));
        }
    }

    instances.push_back(inst);
    std::string firstMeshResourceId;
    for (auto* node : model->getNodes()) {
        if (node && !node->mesh_ressource_id.empty()) {
            firstMeshResourceId = node->mesh_ressource_id;
            break;
        }
    }
    instanceModelPathByOwner_[inst.get()] = path;
    if (!firstMeshResourceId.empty()) {
        instanceMeshResourceByOwner_[inst.get()] = firstMeshResourceId;
    } else {
        instanceMeshResourceByOwner_.erase(inst.get());
    }
    int& refCount = loadedModelRefCountByPath_[path];
    refCount++;
    if (!firstMeshResourceId.empty()) {
        loadedMeshByModelPath_[path] = firstMeshResourceId;
    } else if (loadedMeshByModelPath_.find(path) == loadedMeshByModelPath_.end()) {
        loadedMeshByModelPath_[path] = {};
    }
    if (refCount == 1) {
        NotifyWebModelLoaded(path, firstMeshResourceId);
    }
    decalDrawsSorted = false;
    decalBatchDirty = true;
    return inst.get();
}


void DeferredRenderer::BuildDrawsWithTransform(const Model& model, const glm::mat4& instanceTransform, void* instance, std::vector<DrawCmd>& out)
{
    std::unordered_set<std::string> transformAnimTargets;

    for (auto* node : model.getNodes()) {
        if (node) {
            if (!node->animation_resource.empty()) {
                transformAnimTargets.insert(node->node_name);
                const std::string leaf = DeferredRendererAnimation::LeafNodeName(node->node_name);
                if (!leaf.empty()) transformAnimTargets.insert(leaf);
            }

            for (const auto& section : node->animSections) {
                if (section.animationNodeType != TransformAnimator &&
                    section.animationNodeType != TransformCurveAnimator) continue;
                if (section.animatedNodesPath.empty()) {
                    transformAnimTargets.insert(node->node_name);
                    const std::string nodeLeaf = DeferredRendererAnimation::LeafNodeName(node->node_name);
                    if (!nodeLeaf.empty()) transformAnimTargets.insert(nodeLeaf);
                    continue;
                }
                for (const auto& animPath : section.animatedNodesPath) {
                    const std::string targetNode = DeferredRendererAnimation::LeafNodeName(animPath);
                    if (!targetNode.empty()) {
                        transformAnimTargets.insert(targetNode);
                    }
                }
            }
        }
    }

    for (auto* node : model.getNodes())
    {
        if (!node) {
            continue;
        }

        // Keep all nodes (including meshless animator/controller nodes) in the
        // lookup map so shader-var animation bindings can discover source sections.
        nodeMap[node->node_name] = const_cast<Node*>(node);

        // Check if this is a decal/particle node before mesh lookup so meshless
        // particle emitters are still kept for particle initialization.
        std::string typeLower = node->model_node_type;
        std::transform(typeLower.begin(), typeLower.end(), typeLower.begin(), ::tolower);
        std::string shaderLower = node->shader;
        std::transform(shaderLower.begin(), shaderLower.end(), shaderLower.begin(), ::tolower);
        std::string meshLower = node->mesh_ressource_id;
        std::transform(meshLower.begin(), meshLower.end(), meshLower.begin(), ::tolower);

        std::string nodeNameLower = node->node_name;
        std::transform(nodeNameLower.begin(), nodeNameLower.end(), nodeNameLower.begin(), ::tolower);

        bool isDecalNode = (typeLower == "decal") ||
                          (shaderLower == "shd:decal") ||
                          (shaderLower.find("decal") != std::string::npos) ||
                          (meshLower.find("decal") != std::string::npos);
        bool isParticleNode = (nodeNameLower.find("pemitter") != std::string::npos) ||
                              (nodeNameLower.find("emitter") != std::string::npos) ||
                              (typeLower.find("particle") != std::string::npos) ||
                              (shaderLower.find("particle") != std::string::npos);

        if (node->mesh_ressource_id.empty() && !isParticleNode) {
            continue;
        }


        DrawCmd dc;
        dc.instance = instance;
        dc.nodeName = node->node_name;
        dc.sourceNode = node;
        dc.modelNodeType = node->model_node_type;
        dc.shdr = node->shader;
        dc.group = node->primitive_group_idx;
        const bool animationsDisabled = AnimationsDisabled();
        dc.hasShaderVarAnimations = !animationsDisabled && std::any_of(
            node->animSections.begin(),
            node->animSections.end(),
            [](const AnimSection& section) {
                return !section.shaderVarSemantic.empty() && !section.floatKeyArray.empty();
            });
        bool hasTargetInHierarchy = false;
        bool hasAnimResInHierarchy = false;
        for (const Node* p = node; p; p = p->node_parent) {
            const std::string pLeaf = DeferredRendererAnimation::LeafNodeName(p->node_name);
            if (transformAnimTargets.find(p->node_name) != transformAnimTargets.end() ||
                (!pLeaf.empty() && transformAnimTargets.find(pLeaf) != transformAnimTargets.end())) {
                hasTargetInHierarchy = true;
            }
            if (!p->animation_resource.empty()) {
                hasAnimResInHierarchy = true;
            }
            if (hasTargetInHierarchy || hasAnimResInHierarchy) {
                break;
            }
        }
        dc.hasPotentialTransformAnimation = !animationsDisabled && (hasTargetInHierarchy || hasAnimResInHierarchy);
        // Only transform animation makes a DrawCmd ineligible for the static matrix cache.
        // Shader-var animations (UV scroll, color pulse, etc.) are driven by global uniforms
        // and don't change the worldMatrix, so those objects can still be cached as static.
        dc.isStatic = !dc.hasPotentialTransformAnimation;

        if (!node->mesh_ressource_id.empty()) {
            dc.mesh = MeshServer::instance().loadMesh(node->mesh_ressource_id);
        }
        if (!dc.mesh && !isParticleNode) {
            continue;
        }
        if (dc.mesh) {
            dc.megaVertexOffset = dc.mesh->megaVertexOffset;
            dc.megaIndexOffset  = dc.mesh->megaIndexOffset;

            dc.groupEnabled.assign(dc.mesh->groups.size(), 0);
            if (dc.group >= 0 && dc.group < (int)dc.mesh->groups.size()) dc.groupEnabled[dc.group] = 1;
            else std::fill(dc.groupEnabled.begin(), dc.groupEnabled.end(), 1);
        }

        dc.localBoxMin = node->local_box_min;
        dc.localBoxMax = node->local_box_max;
        dc.shaderParamsFloat = node->shader_params_float;
        dc.shaderParamsInt = node->shader_params_int;
        dc.shaderParamsVec4 = node->shader_params_vec4;
        dc.shaderParamsTexture = node->shader_params_texture;

        // Pre-cache frequently accessed shader params to avoid per-frame map lookups.
        {
            auto getF = [&](const char* key, float def) {
                auto it = dc.shaderParamsFloat.find(key);
                return it != dc.shaderParamsFloat.end() ? it->second : def;
            };
            auto getI = [&](const char* key, int def) {
                auto it = dc.shaderParamsInt.find(key);
                return it != dc.shaderParamsInt.end() ? it->second : def;
            };
            dc.cachedIsAdditive = (typeLower == "additive");
            dc.cachedHasSpecMap =
                dc.shaderParamsTexture.count("SpecMap0") || dc.shaderParamsTexture.count("SpecMap");
            {
                auto diffIt = dc.shaderParamsTexture.find("DiffMap0");
                if (diffIt == dc.shaderParamsTexture.end())
                    diffIt = dc.shaderParamsTexture.find("DiffMap");
                dc.cachedHasCustomDiffMap = diffIt != dc.shaderParamsTexture.end() &&
                    !diffIt->second.empty() &&
                    diffIt->second != "tex:system/white" &&
                    diffIt->second != "system/white";
            }
            {
                auto velIt = dc.shaderParamsVec4.find("Velocity");
                dc.cachedHasVelocity = velIt != dc.shaderParamsVec4.end();
                if (dc.cachedHasVelocity)
                    dc.cachedVelocity = glm::vec2(velIt->second.x, velIt->second.y);
            }
            dc.cachedTwoSided          = getI("twoSided", 0);
            dc.cachedIsFlatNormal      = getI("isFlatNormal", 0);
            dc.cachedWaterCullMode     = getI("CullMode", 2);
            dc.cachedIntensity0        = glm::clamp(getF("Intensity0", getF("Reflectivity", 0.25f)), 0.0f, 1.0f);
            dc.cachedMatEmissiveIntensity = getF("MatEmissiveIntensity", 1.0f);
            dc.cachedMatSpecularIntensity = getF("MatSpecularIntensity", 0.0f);
            dc.cachedMatSpecularPower  = getF("MatSpecularPower", 32.0f);
            dc.cachedScale             = getF("Scale", 1.0f);
            dc.cachedBumpScale         = getF("BumpScale", 1.0f);
            dc.cachedAlphaBlendFactor  = getF("alphaBlendFactor", getF("Intensity0", 1.0f));
        }

        if (isDecalNode && dc.mesh) {
            dc.localBoxMin = glm::vec4(dc.mesh->boundingBoxMin, 1.0f);
            dc.localBoxMax = glm::vec4(dc.mesh->boundingBoxMax, 1.0f);
        } else {
            glm::vec3 boxSize = glm::vec3(dc.localBoxMax - dc.localBoxMin);
            if (std::abs(boxSize.x) < 1e-5f && std::abs(boxSize.y) < 1e-5f && std::abs(boxSize.z) < 1e-5f) {
                if (dc.mesh) {
                    dc.localBoxMin = glm::vec4(dc.mesh->boundingBoxMin, 1.0f);
                    dc.localBoxMax = glm::vec4(dc.mesh->boundingBoxMax, 1.0f);
                }
            }
        }

        if (!node->model_node_type.empty()) {
            std::string typeName = node->model_node_type;
            std::transform(typeName.begin(), typeName.end(), typeName.begin(),
                [](unsigned char c) { return (char)std::tolower(c); });
            dc.alphaTest = (typeName.find("alphatest") != std::string::npos);
        }
        if (dc.alphaTest) {
            auto alphaIt = dc.shaderParamsFloat.find("AlphaRef");
            if (alphaIt != dc.shaderParamsFloat.end()) {
                dc.alphaCutoff = alphaIt->second;
            } else {
                auto alphaIntIt = dc.shaderParamsInt.find("AlphaRef");
                if (alphaIntIt != dc.shaderParamsInt.end()) {
                    dc.alphaCutoff = (alphaIntIt->second > 1) ? (alphaIntIt->second / 255.0f) : (float)alphaIntIt->second;
                }
            }
        }

        dc.position = node->position;
        dc.rotation = node->rotation;
        dc.scale = node->scale;

        glm::mat4 local = glm::mat4(1.0f);
        for (const Node* p = node; p; p = p->node_parent) {
            glm::vec3 pPos(p->position.x, p->position.y, p->position.z);
            glm::quat pRot(p->rotation.w, p->rotation.x, p->rotation.y, p->rotation.z);
            glm::vec3 pScale(p->scale.x, p->scale.y, p->scale.z);
            glm::vec3 rPivot(p->rotate_pivot.x, p->rotate_pivot.y, p->rotate_pivot.z);
            glm::vec3 sPivot(p->scale_pivot.x, p->scale_pivot.y, p->scale_pivot.z);

            glm::mat4 M = glm::translate(glm::mat4(1.0f), pPos)
                * glm::translate(glm::mat4(1.0f), rPivot)
                * glm::mat4_cast(pRot)
                * glm::translate(glm::mat4(1.0f), -rPivot)
                * glm::translate(glm::mat4(1.0f), sPivot)
                * glm::scale(glm::mat4(1.0f), pScale)
                * glm::translate(glm::mat4(1.0f), -sPivot);
            local = M * local;
        }
        dc.worldMatrix = instanceTransform * local;
        dc.rootMatrix = local;

        auto checkTex = [&](const char* key) -> std::string {
            auto it = node->shader_params_texture.find(key);
            if (it != node->shader_params_texture.end()) return it->second;
            return "";
        };

        std::string diffMap0 = checkTex("DiffMap0");
        if (diffMap0.empty()) diffMap0 = checkTex("DiffMap");

        bool hasOnlyDefaults = (diffMap0.empty() ||
                                diffMap0 == "tex:system/white" ||
                                diffMap0 == "system/white");

        if (hasOnlyDefaults && !isParticleNode) {
            bool hasAnyCustomTexture = false;
            for (const auto& [key, val] : node->shader_params_texture) {
                if (!val.empty() &&
                    val != "tex:system/white" && val != "system/white" &&
                    val != "tex:system/black" && val != "system/black" &&
                    val != "tex:system/nobump" && val != "system/nobump") {
                    hasAnyCustomTexture = true;
                    break;
                }
            }

            if (!hasAnyCustomTexture) {
               // std::cout << "[SKIP] Node '" << node->node_name << "' uses only default textures (placeholder geometry)\n";
                continue;
            }
        }

        // Load textures - use fallbacks if not found
        auto it = node->shader_params_texture.find("DiffMap0");
        if (it == node->shader_params_texture.end())
            it = node->shader_params_texture.find("DiffMap");

        auto pick = [&](const char* key) -> GLuint {
            it = node->shader_params_texture.find(key);
            if (it == node->shader_params_texture.end()) return 0;
            GLuint texID = TextureServer::instance().loadTexture(it->second);
            if (texID == 0) {
                std::cerr << "[TEXTURE LOAD FAIL] " << key << "='" << it->second << "' in node '" << node->node_name << "'\n";
            }
            return texID;
        };

        dc.tex[0] = pick("DiffMap0"); if (!dc.tex[0]) dc.tex[0] = pick("DiffMap");
        if (isDecalNode && !dc.tex[0]) dc.tex[0] = pick("DiffMap2");
        dc.tex[1] = pick("SpecMap0"); if (!dc.tex[1]) dc.tex[1] = pick("SpecMap");
        dc.tex[2] = pick("BumpMap0"); if (!dc.tex[2]) dc.tex[2] = pick("BumpMap");
        dc.tex[3] = pick("EmsvMap0");
        if (!dc.tex[3]) dc.tex[3] = pick("EmissiveMap");
        if (!dc.tex[3]) dc.tex[3] = pick("EmsvMap");
        if (isDecalNode && !dc.tex[3]) dc.tex[3] = pick("MaskMap");
        if (isDecalNode && !dc.tex[3]) dc.tex[3] = pick("DiffMap3");

        dc.tex[4] = pick("DiffMap2");
        dc.tex[5] = pick("SpecMap1");
        dc.tex[6] = pick("BumpMap1");
        dc.tex[7] = pick("MaskMap"); if (!dc.tex[7]) dc.tex[7] = pick("DiffMap3");

        dc.tex[9] = pick("CubeMap0"); if (!dc.tex[9]) dc.tex[9] = pick("EnvironmentMap");

        if (!dc.tex[0]) dc.tex[0] = whiteTex;
        if (!dc.tex[1]) dc.tex[1] = whiteTex;
        if (!dc.tex[2]) dc.tex[2] = normalTex;
        if (!dc.tex[3]) dc.tex[3] = blackTex;
        if (!dc.tex[4]) dc.tex[4] = dc.tex[0];
        if (!dc.tex[5]) dc.tex[5] = dc.tex[1];
        if (!dc.tex[6]) dc.tex[6] = dc.tex[2];
        if (!dc.tex[7]) dc.tex[7] = blackTex;
        if (!dc.tex[8]) dc.tex[8] = whiteTex;

        if (MaterialInputDebugEnabled() &&
            (dc.shdr == "shd:standard" || dc.shdr == "shd:environment")) {
            auto texName = [&](const char* key, const char* fallback = nullptr) -> std::string {
                auto itKey = node->shader_params_texture.find(key);
                if (itKey != node->shader_params_texture.end() && !itKey->second.empty()) return itKey->second;
                if (fallback) {
                    auto itFallback = node->shader_params_texture.find(fallback);
                    if (itFallback != node->shader_params_texture.end()) return itFallback->second;
                }
                return "<none>";
            };
            std::cout << "[MATDBG] node='" << dc.nodeName
                      << "' shdr='" << dc.shdr
                      << "' type='" << dc.modelNodeType
                      << "' DiffMap0='" << texName("DiffMap0", "DiffMap")
                      << "' SpecMap0='" << texName("SpecMap0", "SpecMap")
                      << "' BumpMap0='" << texName("BumpMap0", "BumpMap")
                      << "' EmsvMap0='" << texName("EmsvMap0", "EmsvMap")
                      << "' texIDs=(" << dc.tex[0] << "," << dc.tex[1] << "," << dc.tex[2] << "," << dc.tex[3] << ")\n";
        }

        auto scaleIt = node->shader_params_float.find("Scale");
        if (scaleIt != node->shader_params_float.end()) {
            dc.decalScale = scaleIt->second;
        }

        auto cullModeIt = node->shader_params_int.find("CullMode");
        if (cullModeIt != node->shader_params_int.end()) {
            dc.cullMode = cullModeIt->second;
        }
        dc.isDecal = isDecalNode;
        std::string nodeTypeLower = node->model_node_type;
        std::transform(nodeTypeLower.begin(), nodeTypeLower.end(), nodeTypeLower.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });

        if (kLogGroundDecalReceiveSolidDiffuse && nodeTypeLower == "decalreceivesolid") {
            const auto itDiff0 = node->shader_params_texture.find("DiffMap0");
            const auto itDiff = node->shader_params_texture.find("DiffMap");
            const bool hasDiff0 = (itDiff0 != node->shader_params_texture.end() && !itDiff0->second.empty());
            const bool hasDiff = (itDiff != node->shader_params_texture.end() && !itDiff->second.empty());
            const char* selectedKey = hasDiff0 ? "DiffMap0" : (hasDiff ? "DiffMap" : "<none>");
            const std::string selectedVal = hasDiff0 ? itDiff0->second : (hasDiff ? itDiff->second : "<none>");

            std::cerr << "[GNDDBG] nodeName='" << node->node_name
                      << "' shdr='" << dc.shdr
                      << "' nodeType='" << node->model_node_type
                      << "' DiffKey='" << selectedKey
                      << "' DiffValue='" << selectedVal
                      << "' tex0=" << dc.tex[0] << std::endl;
        }
        // Accept explicit receiver tags first, then fall back to opaque non-decal geometry.
        std::string shaderLowerForReceiver = dc.shdr;
        std::transform(shaderLowerForReceiver.begin(), shaderLowerForReceiver.end(), shaderLowerForReceiver.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        const bool explicitReceiver = (nodeTypeLower == "decalreceivesolid");
        const bool isTransparentType = (nodeTypeLower.find("alpha") != std::string::npos) ||
                                       (nodeTypeLower.find("postalphaunlit") != std::string::npos) ||
                                       (nodeTypeLower == "additive");
        const bool isReceiverFallback = !isDecalNode &&
                                        !isParticleNode &&
                                        !isTransparentType &&
                                        shaderLowerForReceiver != "shd:water" &&
                                        shaderLowerForReceiver != "shd:refraction";
        dc.receivesDecals = explicitReceiver || isReceiverFallback;
        if (isDecalNode) {
            dc.sourceNode = nullptr;
            dc.hasPotentialTransformAnimation = false;
            dc.isStatic = true;
        }
        out.push_back(dc);
    }
}

void DeferredRenderer::ResetStreamingState() {
    streamState_.map = nullptr;
    streamState_.gridW = 0;
    streamState_.gridH = 0;
    streamState_.cellSizeX = 32.0f;
    streamState_.cellSizeZ = 32.0f;
    streamState_.originXZ = glm::vec2(0.0f, 0.0f);
    streamState_.cellToInstances.clear();
    streamState_.loadedOwnersByMapIndex.clear();
    streamState_.lastTickTime = 0.0;
    streamState_.initialized = false;
}

void DeferredRenderer::BuildStreamingIndex(const MapData* map) {
    ResetStreamingState();
    if (!map) return;

    streamState_.map = map;
    const MapInfo& info = map->info;
    streamState_.gridW = std::max(1, info.map_size_x > 0 ? static_cast<int>(info.map_size_x) : 32);
    streamState_.gridH = std::max(1, info.map_size_z > 0 ? static_cast<int>(info.map_size_z) : 32);
    streamState_.cellSizeX = info.grid_size.x > 0.0f ? info.grid_size.x : 32.0f;
    streamState_.cellSizeZ = info.grid_size.z > 0.0f ? info.grid_size.z : 32.0f;
    streamState_.originXZ = glm::vec2(info.center.x - info.extents.x,
                                      info.center.z - info.extents.z);
    streamState_.cellToInstances.resize(static_cast<size_t>(streamState_.gridW * streamState_.gridH));
    streamState_.initialized = true;

    for (size_t i = 0; i < map->instances.size(); ++i) {
        const auto& inst = map->instances[i];
        if ((unsigned)inst.templ_index >= map->templates.size()) continue;
        const auto& tmpl = map->templates[inst.templ_index];
        if (tmpl.gfx_res_id >= map->string_table.size()) continue;
        if (map->string_table[tmpl.gfx_res_id].empty()) continue;
        const glm::vec3 pos(inst.pos.x, inst.pos.y, inst.pos.z);
        const int cell = ComputeStreamingCellIndex(pos);
        if (cell < 0) continue;
        streamState_.cellToInstances[static_cast<size_t>(cell)].push_back(i);
    }
}

int DeferredRenderer::ComputeStreamingCellIndex(const glm::vec3& worldPos) const {
    if (!streamState_.initialized || streamState_.gridW <= 0 || streamState_.gridH <= 0) {
        return -1;
    }

    int cx = static_cast<int>(std::floor((worldPos.x - streamState_.originXZ.x) / streamState_.cellSizeX));
    int cz = static_cast<int>(std::floor((worldPos.z - streamState_.originXZ.y) / streamState_.cellSizeZ));
    cx = std::clamp(cx, 0, streamState_.gridW - 1);
    cz = std::clamp(cz, 0, streamState_.gridH - 1);
    return cz * streamState_.gridW + cx;
}

ModelInstance* DeferredRenderer::LoadMapInstanceByIndex(const MapData* map, size_t mapIndex) {
    if (!map || mapIndex >= map->instances.size()) return nullptr;

    const int eventCount = static_cast<int>(map->event_mapping.size());
    if (eventCount <= 0) {
        activeEventFilterIndex = 0;
    } else {
        activeEventFilterIndex = std::clamp(activeEventFilterIndex, 0, eventCount - 1);
    }

    const auto& inst = map->instances[mapIndex];
    if ((unsigned)inst.templ_index >= map->templates.size()) return nullptr;

    if (filterEventsOnly && eventCount > 0 &&
        static_cast<int>(inst.index_to_mapping) != activeEventFilterIndex) {
        return nullptr;
    }

    const auto& tmpl = map->templates[inst.templ_index];
    if (tmpl.gfx_res_id >= map->string_table.size()) return nullptr;
    const std::string& gfxName = map->string_table[tmpl.gfx_res_id];
    if (gfxName.empty()) return nullptr;

    std::string modelPath = std::string(MODELS_ROOT) + gfxName + ".n3";

    glm::vec3 pos(inst.pos.x, inst.pos.y, inst.pos.z);
    glm::quat rot(inst.rot.w, inst.rot.x, inst.rot.y, inst.rot.z);
    glm::vec3 scl = inst.use_scaling
        ? glm::vec3(inst.scale.x, inst.scale.y, inst.scale.z)
        : glm::vec3(1.0f);

    return appendN3WTransform(modelPath, pos, rot, scl);
}

void DeferredRenderer::RebuildNodeMapFromInstances() {
    nodeMap.clear();
    for (const auto& inst : instances) {
        if (!inst) continue;
        const auto model = inst->getModel();
        if (!model) continue;
        for (const auto* node : model->getNodes()) {
            if (!node) continue;
            nodeMap[node->node_name] = const_cast<Node*>(node);
        }
    }
}

void DeferredRenderer::UnloadInstanceByOwner(void* owner) {
    if (!owner) return;

    std::string modelPath;
    std::string meshResourceId;
    if (const auto it = instanceModelPathByOwner_.find(owner); it != instanceModelPathByOwner_.end()) {
        modelPath = it->second;
    }
    if (const auto it = instanceMeshResourceByOwner_.find(owner); it != instanceMeshResourceByOwner_.end()) {
        meshResourceId = it->second;
    }
    if (!modelPath.empty()) {
        if (auto itRef = loadedModelRefCountByPath_.find(modelPath); itRef != loadedModelRefCountByPath_.end()) {
            itRef->second--;
            if (itRef->second <= 0) {
                std::string resolvedMesh = meshResourceId;
                if (resolvedMesh.empty()) {
                    if (const auto itMesh = loadedMeshByModelPath_.find(modelPath); itMesh != loadedMeshByModelPath_.end()) {
                        resolvedMesh = itMesh->second;
                    }
                }
                NotifyWebModelUnloaded(modelPath, resolvedMesh);
                loadedModelRefCountByPath_.erase(itRef);
                loadedMeshByModelPath_.erase(modelPath);
            }
        }
    } else if (!meshResourceId.empty()) {
        NotifyWebModelUnloaded(modelPath, meshResourceId);
    }
    instanceModelPathByOwner_.erase(owner);
    instanceMeshResourceByOwner_.erase(owner);

    auto eraseByOwner = [owner](std::vector<DrawCmd>& draws) {
        draws.erase(std::remove_if(draws.begin(), draws.end(),
                    [owner](const DrawCmd& dc) { return dc.instance == owner; }),
                draws.end());
    };

    eraseByOwner(solidDraws);
    eraseByOwner(alphaTestDraws);
    eraseByOwner(simpleLayerDraws);
    eraseByOwner(environmentDraws);
    eraseByOwner(environmentAlphaDraws);
    eraseByOwner(waterDraws);
    eraseByOwner(refractionDraws);
    eraseByOwner(postAlphaUnlitDraws);
    eraseByOwner(decalDraws);

    particleNodes.erase(std::remove_if(particleNodes.begin(), particleNodes.end(),
                        [owner](const ParticleAttach& p) { return p.instance == owner; }),
                    particleNodes.end());

    animatorInstances.erase(std::remove_if(animatorInstances.begin(), animatorInstances.end(),
                           [owner](const std::unique_ptr<AnimatorNodeInstance>& anim) {
                               return anim && anim->GetOwner() == owner;
                           }),
                       animatorInstances.end());

    ClearAnimationOwnerData(owner);

    instances.erase(std::remove_if(instances.begin(), instances.end(),
                    [owner](const std::shared_ptr<ModelInstance>& inst) {
                        return inst && inst.get() == owner;
                    }),
                instances.end());
    instanceSpawnTimes.erase(owner);

    for (auto it = disabledDrawSet.begin(); it != disabledDrawSet.end();) {
        if (it->instance == owner) it = disabledDrawSet.erase(it);
        else ++it;
    }
    disabledDrawOrder.erase(std::remove_if(disabledDrawOrder.begin(), disabledDrawOrder.end(),
                           [owner](const DisabledDrawKey& key) { return key.instance == owner; }),
                       disabledDrawOrder.end());
    if (disabledSelectionIndex >= static_cast<int>(disabledDrawOrder.size())) {
        disabledSelectionIndex = -1;
    }

    selectedObject = nullptr;
    selectedIndex = -1;
    cachedObj = DrawCmd{};
    cachedIndex = -1;
    decalDrawsSorted = false;
    decalBatchDirty = true;
}

void DeferredRenderer::ApplySceneRebuildAfterStreamingChanges(bool meshLayoutChanged) {
    if (meshLayoutChanged) {
        MeshServer::instance().buildMegaBuffer();

        auto setMegaOffsets = [](std::vector<DrawCmd>& draws) {
            for (auto& dc : draws) {
                if (!dc.mesh) continue;
                dc.megaVertexOffset = dc.mesh->megaVertexOffset;
                dc.megaIndexOffset = dc.mesh->megaIndexOffset;
            }
        };

        setMegaOffsets(solidDraws);
        setMegaOffsets(simpleLayerDraws);
        setMegaOffsets(alphaTestDraws);
        setMegaOffsets(decalDraws);
        setMegaOffsets(waterDraws);
        setMegaOffsets(refractionDraws);
        setMegaOffsets(environmentDraws);
        setMegaOffsets(environmentAlphaDraws);
        setMegaOffsets(postAlphaUnlitDraws);
    }

    RebuildNodeMapFromInstances();
    rebuildAnimatedDrawLists();
    DrawBatchSystem::instance().init(solidDraws);
    BuildVisibilityGrids();
    ApplyDisabledDrawFlags();
    lastVisibleCells_.clear();
}

void DeferredRenderer::UpdateIncrementalStreaming(bool forceFullSync) {
    if (!enableIncrementalStreaming_ || !currentMap) return;

    if (streamState_.map != currentMap || !streamState_.initialized) {
        BuildStreamingIndex(currentMap);
    }
    if (!streamState_.initialized) return;

    const double now = glfwGetTime();
    if (!forceFullSync &&
        streamState_.lastTickTime > 0.0 &&
        (now - streamState_.lastTickTime) < streamTickIntervalSec_) {
        return;
    }
    streamState_.lastTickTime = now;

    const glm::vec3 camPos = camera_.getPosition();
    const int camCell = ComputeStreamingCellIndex(camPos);
    if (camCell < 0) return;
    const int camCellX = camCell % streamState_.gridW;
    const int camCellZ = camCell / streamState_.gridW;

    const float streamRadius = visibleRange_ > 0.0f
        ? std::max(220.0f, visibleRange_ * 1.35f)
        : 700.0f;
    const int rangeCellsX = std::max(1, static_cast<int>(std::ceil(streamRadius / streamState_.cellSizeX)) + 1);
    const int rangeCellsZ = std::max(1, static_cast<int>(std::ceil(streamRadius / streamState_.cellSizeZ)) + 1);

    const int xMin = std::max(0, camCellX - rangeCellsX);
    const int xMax = std::min(streamState_.gridW - 1, camCellX + rangeCellsX);
    const int zMin = std::max(0, camCellZ - rangeCellsZ);
    const int zMax = std::min(streamState_.gridH - 1, camCellZ + rangeCellsZ);

    std::vector<size_t> desired;
    for (int z = zMin; z <= zMax; ++z) {
        for (int x = xMin; x <= xMax; ++x) {
            const int cellIndex = z * streamState_.gridW + x;
            const auto& cell = streamState_.cellToInstances[static_cast<size_t>(cellIndex)];
            desired.insert(desired.end(), cell.begin(), cell.end());
        }
    }

    const int eventCount = static_cast<int>(currentMap->event_mapping.size());
    if (eventCount <= 0) {
        activeEventFilterIndex = 0;
    } else {
        activeEventFilterIndex = std::clamp(activeEventFilterIndex, 0, eventCount - 1);
    }
    if (filterEventsOnly && eventCount > 0) {
        desired.erase(std::remove_if(desired.begin(), desired.end(), [&](size_t idx) {
            if (idx >= currentMap->instances.size()) return true;
            const auto& inst = currentMap->instances[idx];
            return static_cast<int>(inst.index_to_mapping) != activeEventFilterIndex;
        }), desired.end());
    }

    std::sort(desired.begin(), desired.end(), [&](size_t a, size_t b) {
        const auto& ia = currentMap->instances[a];
        const auto& ib = currentMap->instances[b];
        const glm::vec3 pa(ia.pos.x, ia.pos.y, ia.pos.z);
        const glm::vec3 pb(ib.pos.x, ib.pos.y, ib.pos.z);
        const float da = glm::dot(pa - camPos, pa - camPos);
        const float db = glm::dot(pb - camPos, pb - camPos);
        if (da != db) return da < db;
        return a < b;
    });
    desired.erase(std::unique(desired.begin(), desired.end()), desired.end());
    if (maxInstancesPerFrame > 0 &&
        static_cast<int>(desired.size()) > maxInstancesPerFrame) {
        desired.resize(static_cast<size_t>(maxInstancesPerFrame));
    }

    std::unordered_set<size_t> desiredSet;
    desiredSet.reserve(desired.size() * 2 + 1);
    desiredSet.insert(desired.begin(), desired.end());

    std::vector<size_t> unloadCandidates;
    unloadCandidates.reserve(streamState_.loadedOwnersByMapIndex.size());
    for (const auto& [idx, owner] : streamState_.loadedOwnersByMapIndex) {
        (void)owner;
        if (desiredSet.find(idx) == desiredSet.end()) {
            unloadCandidates.push_back(idx);
        }
    }
    std::sort(unloadCandidates.begin(), unloadCandidates.end(), [&](size_t a, size_t b) {
        const auto& ia = currentMap->instances[a];
        const auto& ib = currentMap->instances[b];
        const glm::vec3 pa(ia.pos.x, ia.pos.y, ia.pos.z);
        const glm::vec3 pb(ib.pos.x, ib.pos.y, ib.pos.z);
        const float da = glm::dot(pa - camPos, pa - camPos);
        const float db = glm::dot(pb - camPos, pb - camPos);
        if (da != db) return da > db; // unload farthest first
        return a > b;
    });

    std::vector<size_t> loadCandidates;
    loadCandidates.reserve(desired.size());
    for (size_t idx : desired) {
        if (streamState_.loadedOwnersByMapIndex.find(idx) == streamState_.loadedOwnersByMapIndex.end()) {
            loadCandidates.push_back(idx);
        }
    }

    int unloadBudget = forceFullSync ? std::numeric_limits<int>::max() : streamUnloadBudgetPerTick_;
    int loadBudget = forceFullSync ? std::numeric_limits<int>::max() : streamLoadBudgetPerTick_;
    if (maxInstancesPerFrame > 0) {
        const int loadedNow = static_cast<int>(streamState_.loadedOwnersByMapIndex.size());
        const int overflow = loadedNow - maxInstancesPerFrame;
        if (overflow > 0) {
            unloadBudget = std::max(unloadBudget, overflow);
        }
    }

    int eventPumpCounter = 0;
    auto pumpWindowEvents = [&]() -> bool {
        if (!forceFullSync || !window_) return false;
        ++eventPumpCounter;
        if ((eventPumpCounter % 32) != 0) return false;
        window_->PollEvents();
        return window_->ShouldClose();
    };

    bool changed = false;
    bool meshLayoutChanged = false;
    int unloadCount = 0;
    for (size_t idx : unloadCandidates) {
        if (unloadCount >= unloadBudget) break;
        if (pumpWindowEvents()) break;
        auto it = streamState_.loadedOwnersByMapIndex.find(idx);
        if (it == streamState_.loadedOwnersByMapIndex.end()) continue;
        UnloadInstanceByOwner(it->second);
        streamState_.loadedOwnersByMapIndex.erase(it);
        unloadCount++;
        changed = true;
    }

    int freeSlots = std::numeric_limits<int>::max();
    if (maxInstancesPerFrame > 0) {
        freeSlots = std::max(0, maxInstancesPerFrame -
                                static_cast<int>(streamState_.loadedOwnersByMapIndex.size()));
    }

    int loadCount = 0;
    for (size_t idx : loadCandidates) {
        if (loadCount >= loadBudget) break;
        if (freeSlots <= 0) break;
        if (pumpWindowEvents()) break;

        ModelInstance* loaded = LoadMapInstanceByIndex(currentMap, idx);
        if (!loaded) continue;

        streamState_.loadedOwnersByMapIndex[idx] = loaded;
        loadCount++;
        freeSlots--;
        changed = true;
        meshLayoutChanged = true;
    }

    if (changed) {
        ApplySceneRebuildAfterStreamingChanges(meshLayoutChanged);
        if (optRenderLOG) {
            std::cout << "[STREAM] loaded=" << streamState_.loadedOwnersByMapIndex.size()
                      << " (+ " << loadCount << ", - " << unloadCount << ")\n";
        }
    }
}

void DeferredRenderer::LoadMapInstances(const MapData* map) {
    if (!map) {
        std::cerr << "map is null\n";
        return;
    }

    BuildStreamingIndex(map);
    const bool preloadAtStartup = ReadEnvToggle("NDEVC_PRELOAD_STREAMING_AT_STARTUP");
    if (preloadAtStartup) {
        const bool forceFullSync = ReadEnvToggle("NDEVC_FORCE_FULL_SYNC_STREAMING");
        UpdateIncrementalStreaming(forceFullSync);
    }
}

// ---------------------------------------------------------------------------
// Visibility grid helpers
// ---------------------------------------------------------------------------


void DeferredRenderer::ReloadMapWithCurrentMode() {
    if (!currentMap) {
        std::cerr << "No map loaded, cannot reload\n";
        return;
    }

    solidDraws.clear();
    decalDraws.clear();
    decalDrawsSorted = false;
    decalBatchDirty = true;
    alphaTestDraws.clear();
    simpleLayerDraws.clear();
    environmentDraws.clear();
    environmentAlphaDraws.clear();
    refractionDraws.clear();
    waterDraws.clear();
    postAlphaUnlitDraws.clear();
    particleNodes.clear();
    animatedDraws.clear();
    animatorInstances.clear();
    nodeMap.clear();
    gClips.clear();
    gAnimPose.clear();
    ClearAnimationOwnerData();
    selectedObject = nullptr;
    selectedIndex = -1;
    cachedObj = DrawCmd{};
    cachedIndex = -1;
    ClearDisabledDraws();

    instances.clear();
    instanceSpawnTimes.clear();
    instanceModelPathByOwner_.clear();
    instanceMeshResourceByOwner_.clear();
    loadedModelRefCountByPath_.clear();
    loadedMeshByModelPath_.clear();
    ResetStreamingState();

    LoadMapInstances(currentMap);

    if (currentMap) {
        const MapInfo& info = currentMap->info;
        const float halfDiag = glm::length(glm::vec2(info.extents.x, info.extents.z));
        const float elevation = std::max(halfDiag * 1.2f, 200.0f);
        camera_.setPosition(glm::vec3(info.center.x,
                                      info.center.y + elevation,
                                      info.center.z + elevation * 0.7f));
    }

    std::cout << "Map reloaded successfully!\n";
    WriteWebSnapshot("reload_map");
}

void DeferredRenderer::rebuildAnimatedDrawLists() {
    animatedDraws.clear();
    solidShaderVarAnimatedIndices.clear();

    if (!DeferredRendererAnimation::IsAnimationEnabled()) {
        return;
    }

    animatedDraws.reserve(solidDraws.size() + alphaTestDraws.size() + waterDraws.size() +
                          simpleLayerDraws.size() + environmentDraws.size());

    auto collectAnimated = [this](std::vector<DrawCmd>& draws) {
        for (auto& dc : draws) {
            if (dc.instance && dc.hasPotentialTransformAnimation) {
                animatedDraws.push_back(&dc);
            }
        }
    };

    collectAnimated(solidDraws);
    collectAnimated(alphaTestDraws);
    collectAnimated(waterDraws);
    collectAnimated(simpleLayerDraws);
    collectAnimated(environmentDraws);
    // collectAnimated(refractionDraws);
    // collectAnimated(postAlphaUnlitDraws);

    solidShaderVarAnimatedIndices.reserve(solidDraws.size());
    for (size_t i = 0; i < solidDraws.size(); ++i) {
        if (solidDraws[i].hasShaderVarAnimations) {
            solidShaderVarAnimatedIndices.push_back(i);
        }
    }

    DeferredRendererAnimation::RebuildShaderVarTargetBindings(nodeMap);
}
