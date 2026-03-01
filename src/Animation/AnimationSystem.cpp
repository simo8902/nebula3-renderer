// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <unordered_set>
#include "Animation/AnimationSystem.h"

#include "Platform/NDEVcHeaders.h"
#include "Assets/NDEVcStructure.h"

namespace fs = std::filesystem;

float gAnimTimeScale = 1.0f;
std::unordered_map<std::string, AnimPose> gStaticPose;
std::unordered_map<std::string, AnimPose> gAnimPose;
std::unordered_map<uint64_t, AnimPose> gAnimPoseScoped;
std::vector<ClipInstance> gClips;
std::unordered_map<std::string, std::vector<std::string>> gCharacterJoints;
std::vector<AnimEventInfoLite> gFrameAnimEvents;
Nax3SampleFn gNax3Sample = nullptr;
AnimEventCallbackFn gAnimEventCallback = nullptr;

namespace {
struct N3Curve {
    uint32_t firstKeyIndex = 0;
    uint8_t isActive = 0;
    uint8_t isStatic = 0;
    uint8_t curveType = 0;
    uint8_t pad = 0;
    glm::vec4 staticKey{0, 0, 0, 1};
};

struct N3Event {
    std::string name;
    std::string category;
    uint16_t keyIndex = 0;
};

struct N3Clip {
    std::string name;
    uint16_t startKeyIndex = 0;
    uint16_t numKeys = 0;
    uint16_t keyStride = 0;
    uint16_t keyDuration = 1;
    uint8_t preInf = 0;
    uint8_t postInf = 0;
    uint16_t numEvents = 0;
    std::vector<N3Curve> curves;
    float tps = 30.0f;
    float duration = 0.0f;
    bool loop = true;
    uint32_t keyBase = 0;
    bool fromHAN0 = false;
    std::vector<int> activeIndex;
    std::vector<N3Event> events;
};

struct N3File {
    std::vector<N3Clip> clips;
    std::vector<glm::vec4> keys;
    bool ok = false;
};

std::unordered_map<std::string, N3File> gNax3DB;
std::unordered_map<uint64_t, int> gClipChoice;
std::unordered_map<const void*, std::unordered_map<std::string, int>> gOwnerJointIndexMap;
std::unordered_map<const void*, std::unordered_map<std::string, AnimPose>> gOwnerStaticPoseMap;
uint64_t gClipSerialCounter = 1;

inline bool FourCCEq(uint32_t v, const char tag[4]) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(&v);
    return p[0] == static_cast<unsigned char>(tag[0]) &&
           p[1] == static_cast<unsigned char>(tag[1]) &&
           p[2] == static_cast<unsigned char>(tag[2]) &&
           p[3] == static_cast<unsigned char>(tag[3]);
}

inline bool ReadBytes(std::ifstream& f, void* p, size_t n) {
    return static_cast<bool>(f.read(reinterpret_cast<char*>(p), n));
}

inline uint64_t HashOwnerNode(const void* owner, const std::string& node) {
    static std::hash<std::string> h;
    uint64_t x = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(owner));
    uint64_t y = static_cast<uint64_t>(h(node));
    return (x << 1) ^ (y + 0x9e3779b97f4a7c15ull + (x << 6) + (x >> 2));
}

inline uint64_t HashKey(const std::string& a, const std::string& b, const void* owner) {
    static std::hash<std::string> h;
    uint64_t x = static_cast<uint64_t>(h(a));
    uint64_t y = static_cast<uint64_t>(h(b));
    uint64_t z = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(owner));
    uint64_t xy = (x << 1) ^ (y + 0x9e3779b97f4a7c15ull + (x << 6) + (x >> 2));
    return (xy << 1) ^ (z + 0x9e3779b97f4a7c15ull + (xy << 6) + (xy >> 2));
}

std::string LeafNodeName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return path;
    return path.substr(slash + 1);
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

void InsertJointName(std::unordered_map<std::string, int>& map, const std::string& name, int idx) {
    if (name.empty() || idx < 0) return;
    map.emplace(name, idx);
    map.emplace(ToLower(name), idx);
    const std::string leaf = LeafNodeName(name);
    if (!leaf.empty() && leaf != name) {
        map.emplace(leaf, idx);
        map.emplace(ToLower(leaf), idx);
    }
}

void InsertOwnerPoseName(std::unordered_map<std::string, AnimPose>& map, const std::string& name, const AnimPose& pose) {
    if (name.empty()) return;
    map.emplace(name, pose);
    map.emplace(ToLower(name), pose);
    const std::string leaf = LeafNodeName(name);
    if (!leaf.empty() && leaf != name) {
        map.emplace(leaf, pose);
        map.emplace(ToLower(leaf), pose);
    }
}

bool TryGetOwnerStaticPose(const void* owner, const std::string& nodeName, AnimPose& out) {
    if (!owner || nodeName.empty()) return false;
    auto itOwner = gOwnerStaticPoseMap.find(owner);
    if (itOwner == gOwnerStaticPoseMap.end()) return false;
    const auto& poseMap = itOwner->second;

    auto findPose = [&](const std::string& key) -> const AnimPose* {
        auto it = poseMap.find(key);
        return (it == poseMap.end()) ? nullptr : &it->second;
    };

    if (const AnimPose* p = findPose(nodeName)) {
        out = *p;
        return true;
    }
    const std::string leaf = LeafNodeName(nodeName);
    if (!leaf.empty()) {
        if (const AnimPose* p = findPose(leaf)) {
            out = *p;
            return true;
        }
    }
    const std::string lower = ToLower(nodeName);
    if (const AnimPose* p = findPose(lower)) {
        out = *p;
        return true;
    }
    if (!leaf.empty()) {
        if (const AnimPose* p = findPose(ToLower(leaf))) {
            out = *p;
            return true;
        }
    }
    return false;
}

int ResolveJointIndex(const void* owner, const std::string& nodeName) {
    if (!owner || nodeName.empty()) return -1;
    auto itOwner = gOwnerJointIndexMap.find(owner);
    if (itOwner == gOwnerJointIndexMap.end()) return -1;
    auto& idxMap = itOwner->second;

    auto findIdx = [&](const std::string& key) -> int {
        auto it = idxMap.find(key);
        return (it == idxMap.end()) ? -1 : it->second;
    };

    if (int idx = findIdx(nodeName); idx >= 0) return idx;
    const std::string leaf = LeafNodeName(nodeName);
    if (!leaf.empty()) {
        if (int idx = findIdx(leaf); idx >= 0) return idx;
    }

    const std::string lower = ToLower(nodeName);
    if (int idx = findIdx(lower); idx >= 0) return idx;
    if (!leaf.empty()) {
        if (int idx = findIdx(ToLower(leaf)); idx >= 0) return idx;
    }

    return -1;
}

std::string MakeClipLookupNode(const std::string& node, int trackIndex) {
    return node + "|track=" + std::to_string(trackIndex);
}

AnimPose BlendPose(const AnimPose& a, const AnimPose& b, float w) {
    AnimPose out{};
    float ww = std::clamp(w, 0.0f, 1.0f);
    out.pos = glm::mix(a.pos, b.pos, ww);
    out.scl = glm::mix(a.scl, b.scl, ww);
    glm::quat qb = (glm::dot(a.rot, b.rot) < 0.0f) ? -b.rot : b.rot;
    out.rot = glm::normalize(glm::slerp(a.rot, qb, ww));
    return out;
}

float ComputeClipMixWeight(const ClipInstance& c) {
    float w = std::clamp(c.blendWeight, 0.0f, 1.0f);
    if (c.fadeInTime > 0.0f) {
        w *= std::clamp(c.t / c.fadeInTime, 0.0f, 1.0f);
    }
    if (!c.loop && c.fadeOutTime > 0.0f && c.dur > 0.0f) {
        const float remain = std::max(0.0f, c.dur - c.t);
        w *= std::clamp(remain / c.fadeOutTime, 0.0f, 1.0f);
    }
    return std::clamp(w, 0.0f, 1.0f);
}

bool TryGetScopedPose(const void* owner, const std::string& nodeName, AnimPose& out) {
    if (!owner || nodeName.empty()) return false;
    auto it = gAnimPoseScoped.find(HashOwnerNode(owner, nodeName));
    if (it != gAnimPoseScoped.end()) {
        out = it->second;
        return true;
    }

    const std::string leaf = LeafNodeName(nodeName);
    if (!leaf.empty() && leaf != nodeName) {
        it = gAnimPoseScoped.find(HashOwnerNode(owner, leaf));
        if (it != gAnimPoseScoped.end()) {
            out = it->second;
            return true;
        }
    }
    return false;
}

const AnimPose* FindPoseBySuffix(const std::unordered_map<std::string, AnimPose>& poseMap, const std::string& target) {
    if (auto it = poseMap.find(target); it != poseMap.end()) {
        return &it->second;
    }
    const std::string targetLeaf = LeafNodeName(target);
    if (!targetLeaf.empty()) {
        if (auto it = poseMap.find(targetLeaf); it != poseMap.end()) {
            return &it->second;
        }
    }

    for (const auto& [key, pose] : poseMap) {
        if (key.size() >= target.size()) {
            const size_t offset = key.size() - target.size();
            if (key.compare(offset, target.size(), target) == 0) {
                if (offset == 0) return &pose;
                const char sep = key[offset - 1];
                if (sep == '/' || sep == '\\') {
                    return &pose;
                }
            }
        }

        if (!targetLeaf.empty() && key.size() >= targetLeaf.size()) {
            const size_t offset = key.size() - targetLeaf.size();
            if (key.compare(offset, targetLeaf.size(), targetLeaf) == 0) {
                if (offset == 0) return &pose;
                const char sep = key[offset - 1];
                if (sep == '/' || sep == '\\') {
                    return &pose;
                }
            }
        }

        if (target.size() >= key.size()) {
            const size_t offset = target.size() - key.size();
            if (target.compare(offset, key.size(), key) == 0) {
                if (offset == 0) return &pose;
                const char sep = target[offset - 1];
                if (sep == '/' || sep == '\\') {
                    return &pose;
                }
            }
        }
    }
    return nullptr;
}

constexpr float N_TICKS_PER_SEC = 6000.0f;

void BuildActiveMap(N3Clip& c) {
    c.activeIndex.assign(c.curves.size(), -1);
    int k = 0;
    for (size_t i = 0; i < c.curves.size(); ++i) {
        const auto& cv = c.curves[i];
        if (cv.isActive && !cv.isStatic) {
            c.activeIndex[i] = k++;
        }
    }
    c.keyStride = static_cast<uint16_t>(k);
}

std::string ResolveNax3Path(std::string src) {
    if (src.rfind("ani:", 0) == 0) src = src.substr(4);
    if (src.size() >= 2 && (src[1] == ':' || src.rfind("\\\\", 0) == 0 || src.rfind("//", 0) == 0)) {
        return src;
    }

    std::string path = ANIMS_ROOT + src;
    if (path.size() < 5 || path.substr(path.size() - 5) != ".nax3") {
        path += ".nax3";
    }
    for (auto& c : path) {
        if (c == '/') c = '\\';
    }
    return path;
}

bool LoadNACForClip(const std::string& naxPath, int clipIndex, N3Clip& clip, std::vector<glm::vec4>& outKeys, uint32_t& baseOut) {
    auto dir = fs::path(ResolveNax3Path(naxPath)).parent_path();
    auto base = fs::path(ResolveNax3Path(naxPath)).stem().string();
    const std::string clipName = clip.name;
    if (base.size() > 12 && base.find("_animations") != std::string::npos) {
        base.erase(base.find("_animations"));
    }

    std::vector<std::string> candidates;
    if (!clipName.empty()) candidates.push_back((dir / (base + "_" + clipName + ".nac")).string());
    candidates.push_back((dir / (base + "_clip" + std::to_string(clipIndex) + ".nac")).string());
    if (!clipName.empty()) candidates.push_back((dir / (clipName + ".nac")).string());

    BuildActiveMap(clip);
    const int frames = std::max<int>(1, clip.numKeys);
    const int active = static_cast<int>(clip.keyStride);
    if (active <= 0 || frames <= 0) {
        baseOut = static_cast<uint32_t>(outKeys.size());
        return false;
    }

    for (const auto& p : candidates) {
        std::ifstream f(p, std::ios::binary);
        if (!f) continue;

        uint32_t magic = 0;
        if (!ReadBytes(f, &magic, 4)) continue;
        if (!(FourCCEq(magic, "0CAN") || FourCCEq(magic, "CAN0"))) continue;

        baseOut = static_cast<uint32_t>(outKeys.size());
        outKeys.resize(baseOut + static_cast<size_t>(frames) * static_cast<size_t>(active), glm::vec4(0, 0, 0, 1));

        for (int frame = 0; frame < frames; ++frame) {
            for (size_t ci = 0; ci < clip.curves.size(); ++ci) {
                const auto& cv = clip.curves[ci];
                const int ai = (ci < clip.activeIndex.size()) ? clip.activeIndex[ci] : -1;
                if (ai < 0) continue;

                glm::vec4 v(0, 0, 0, 1);
                if (cv.curveType == 2) {
                    int16_t s[4];
                    if (!ReadBytes(f, s, 8)) {
                        outKeys.resize(baseOut);
                        return false;
                    }
                    v.x = static_cast<float>(s[0]) * (1.0f / 32768.0f);
                    v.y = static_cast<float>(s[1]) * (1.0f / 32768.0f);
                    v.z = static_cast<float>(s[2]) * (1.0f / 32768.0f);
                    v.w = static_cast<float>(s[3]) * (1.0f / 32768.0f);
                } else {
                    float fv[4];
                    if (!ReadBytes(f, fv, 16)) {
                        outKeys.resize(baseOut);
                        return false;
                    }
                    v = glm::vec4(fv[0], fv[1], fv[2], fv[3]);
                }

                outKeys[baseOut + static_cast<size_t>(frame) * static_cast<size_t>(active) + static_cast<size_t>(ai)] = v;
            }
        }

        clip.keyBase = baseOut;
        std::cout << "[NAC] loaded file='" << p << "' frames=" << frames
                  << " activeCurves=" << active << " keyBase=" << baseOut << "\n";
        return true;
    }

    std::cout << "[NAC] missing for nax='" << ResolveNax3Path(naxPath)
              << "' clipIndex=" << clipIndex << " clipName='" << clip.name << "'\n";
    baseOut = static_cast<uint32_t>(outKeys.size());
    return false;
}

bool LoadHAN0(std::ifstream& f, const std::string& path, N3File& out) {
    int32_t numClips = 0;
    int32_t fileNumKeys = 0;
    if (!ReadBytes(f, &numClips, 4) || !ReadBytes(f, &fileNumKeys, 4)) return false;

    out.clips.clear();
    out.keys.clear();
    out.clips.reserve(std::max(0, numClips));

    std::cout << "[NAX3] file=" << path << " clips=" << numClips << " header keys=" << fileNumKeys << "\n";

    for (int ci = 0; ci < numClips; ++ci) {
        uint16_t numCurves = 0;
        uint16_t startKeyIndex = 0;
        uint16_t clipNumKeys = 0;
        uint16_t keyStride = 0;
        uint16_t keyDuration = 0;
        uint8_t pre = 0;
        uint8_t post = 0;
        uint16_t numEvents = 0;
        char name50[50]{};

        if (!ReadBytes(f, &numCurves, 2) ||
            !ReadBytes(f, &startKeyIndex, 2) ||
            !ReadBytes(f, &clipNumKeys, 2) ||
            !ReadBytes(f, &keyStride, 2) ||
            !ReadBytes(f, &keyDuration, 2) ||
            !ReadBytes(f, &pre, 1) ||
            !ReadBytes(f, &post, 1) ||
            !ReadBytes(f, &numEvents, 2) ||
            !ReadBytes(f, name50, 50)) {
            return false;
        }

        N3Clip clip;
        clip.name = std::string(name50, strnlen(name50, 50));
        clip.startKeyIndex = startKeyIndex;
        clip.numKeys = clipNumKeys;
        clip.keyStride = keyStride;
        clip.keyDuration = keyDuration ? keyDuration : 1;
        clip.preInf = pre;
        clip.postInf = post;
        clip.numEvents = numEvents;
        clip.tps = clip.keyDuration > 0 ? (N_TICKS_PER_SEC / static_cast<float>(clip.keyDuration)) : 30.0f;
        clip.loop = true;
        clip.duration = (clip.numKeys > 0) ? static_cast<float>(clip.numKeys) / clip.tps : 0.0f;
        clip.fromHAN0 = true;

        clip.events.clear();
        clip.events.reserve(numEvents);
        for (uint16_t ev = 0; ev < numEvents; ++ev) {
            char evName[47]{};
            char evCat[15]{};
            uint16_t keyIndex = 0;
            if (!ReadBytes(f, evName, 47) ||
                !ReadBytes(f, evCat, 15) ||
                !ReadBytes(f, &keyIndex, 2)) {
                return false;
            }
            N3Event event;
            event.name = std::string(evName, strnlen(evName, 47));
            event.category = std::string(evCat, strnlen(evCat, 15));
            event.keyIndex = keyIndex;
            clip.events.push_back(std::move(event));
        }

        clip.curves.resize(numCurves);
        for (uint16_t i = 0; i < numCurves; ++i) {
            N3Curve cv;
            if (!ReadBytes(f, &cv.firstKeyIndex, 4) ||
                !ReadBytes(f, &cv.isActive, 1) ||
                !ReadBytes(f, &cv.isStatic, 1) ||
                !ReadBytes(f, &cv.curveType, 1) ||
                !ReadBytes(f, &cv.pad, 1) ||
                !ReadBytes(f, &cv.staticKey.x, 4) ||
                !ReadBytes(f, &cv.staticKey.y, 4) ||
                !ReadBytes(f, &cv.staticKey.z, 4) ||
                !ReadBytes(f, &cv.staticKey.w, 4)) {
                return false;
            }
            clip.curves[i] = cv;
        }

        out.clips.push_back(std::move(clip));
    }

    out.ok = true;
    return true;
}

bool LoadNax3File(const std::string& source, N3File& out) {
    const std::string path = ResolveNax3Path(source);
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "[NAX3] open fail " << path << "\n";
        return false;
    }

    uint32_t magic = 0;
    if (!ReadBytes(f, &magic, 4)) {
        std::cerr << "[NAX3] bad header " << path << "\n";
        return false;
    }
    if (FourCCEq(magic, "HAN0") || FourCCEq(magic, "0HAN")) {
        return LoadHAN0(f, path, out);
    }

    std::cerr << "[NAX3] invalid magic 0x" << std::hex << magic << std::dec << " " << path << "\n";
    return false;
}

N3File* GetNax3(const std::string& source) {
    const std::string key = ResolveNax3Path(source);
    auto it = gNax3DB.find(key);
    if (it != gNax3DB.end() && it->second.ok) return &it->second;

    N3File f{};
    if (!LoadNax3File(source, f) || !f.ok || f.clips.empty()) {
        return nullptr;
    }

    auto res = gNax3DB.emplace(key, std::move(f));
    return &res.first->second;
}

int GetChosenClipIndex(const std::string& source, const std::string& node, const void* owner) {
    auto it = gClipChoice.find(HashKey(ResolveNax3Path(source), node, owner));
    return (it == gClipChoice.end()) ? -1 : it->second;
}

void SetChosenClipIndex(const std::string& source, const std::string& node, int idx, const void* owner) {
    gClipChoice[HashKey(ResolveNax3Path(source), node, owner)] = idx;
}

int FindClipIndexByName(const std::string& source, const std::string& clipName) {
    N3File* db = GetNax3(source);
    if (!db || db->clips.empty()) return 0;
    for (int i = 0; i < static_cast<int>(db->clips.size()); ++i) {
        if (db->clips[i].name == clipName) return i;
    }
    return 0;
}

bool SampleTRSFromNAC(const N3File& db, const N3Clip& c, int jointIndex, int frame, glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScl) {
    if (c.keyStride == 0 || c.keyBase >= db.keys.size()) return false;

    auto fetch = [&](int curveIndex) -> glm::vec4 {
        if (curveIndex < 0 || curveIndex >= static_cast<int>(c.curves.size())) return glm::vec4(0, 0, 0, 1);
        const auto& cv = c.curves[curveIndex];
        int ai = (curveIndex < static_cast<int>(c.activeIndex.size())) ? c.activeIndex[curveIndex] : -1;
        if (ai < 0) {
            return cv.staticKey;
        }
        uint32_t idx = c.keyBase + static_cast<uint32_t>(ai) + static_cast<uint32_t>(frame) * static_cast<uint32_t>(c.keyStride);
        if (idx >= db.keys.size()) return glm::vec4(0, 0, 0, 1);
        return db.keys[idx];
    };

    const int base = jointIndex * 3;
    int ciT = base + 0;
    int ciR = base + 1;
    int ciS = base + 2;
    if (ciS >= static_cast<int>(c.curves.size())) return false;

    if (c.curves[ciR].curveType != 2) {
        for (int k = 0; k < 3; ++k) {
            const int candidate = base + k;
            if (candidate >= 0 && candidate < static_cast<int>(c.curves.size()) && c.curves[candidate].curveType == 2) {
                ciR = candidate;
                break;
            }
        }
    }

    glm::vec3 P(0.0f);
    glm::vec3 S(1.0f);
    glm::quat Q(1, 0, 0, 0);

    if (c.curves[ciT].isStatic || c.activeIndex[ciT] < 0) {
        const auto& sk = c.curves[ciT].staticKey;
        P = glm::vec3(sk.x, sk.y, sk.z);
    } else {
        glm::vec4 v = fetch(ciT);
        P = glm::vec3(v.x, v.y, v.z);
    }

    if (c.curves[ciR].isStatic || c.activeIndex[ciR] < 0) {
        const auto& sk = c.curves[ciR].staticKey;
        Q = glm::normalize(glm::quat(sk.w, sk.x, sk.y, sk.z));
    } else {
        glm::vec4 v = fetch(ciR);
        Q = glm::normalize(glm::quat(v.w, v.x, v.y, v.z));
    }

    if (c.curves[ciS].isStatic || c.activeIndex[ciS] < 0) {
        const auto& sk = c.curves[ciS].staticKey;
        S = glm::max(glm::vec3(1e-6f), glm::vec3(sk.x, sk.y, sk.z));
    } else {
        glm::vec4 v = fetch(ciS);
        S = glm::max(glm::vec3(1e-6f), glm::vec3(v.x, v.y, v.z));
    }

    outPos = P;
    outRot = Q;
    outScl = S;
    return true;
}

bool Nax3Sample(const std::string& source, float t, const std::string& node, const void* owner, int jointIndex, AnimPose& out, float& outDur, float& outTicksPerSec) {
    N3File* db = GetNax3(source);
    if (!db) {
        std::string s = source;
        if (s.rfind("ani:", 0) == 0 || s.rfind("ANI:", 0) == 0) s = s.substr(4);
        db = GetNax3(s);
    }
    if (!db || db->clips.empty()) return false;

    int clipIdx = GetChosenClipIndex(source, node, owner);
    if (clipIdx < 0 || clipIdx >= static_cast<int>(db->clips.size())) {
        clipIdx = 0;
    }
    N3Clip& c = db->clips[clipIdx];

    if (c.fromHAN0 && (c.activeIndex.empty() || c.keyStride == 0 || c.keyBase >= db->keys.size())) {
        uint32_t base = 0;
        LoadNACForClip(ResolveNax3Path(source), clipIdx, c, db->keys, base);
        if (c.keyBase == 0 && base > 0) c.keyBase = base;
    }

    const float tps = (c.tps > 0.0f) ? c.tps : 30.0f;
    const float dur = (c.duration > 0.0f) ? c.duration : ((c.numKeys > 0) ? (static_cast<float>(c.numKeys) / tps) : 0.0f);
    float tt = (dur > 0.0f) ? std::max(0.0f, std::min(t, dur)) : 0.0f;
    float frameF = tt * tps;
    const int frames = std::max<int>(1, c.numKeys);
    int f0 = std::min(frames - 1, static_cast<int>(std::floor(frameF)));
    int f1 = std::min(frames - 1, f0 + 1);
    float a = frameF - static_cast<float>(f0);

    out.pos = glm::vec3(0.0f);
    out.rot = glm::quat(1, 0, 0, 0);
    out.scl = glm::vec3(1.0f);

    if (c.fromHAN0 && jointIndex >= 0) {
        glm::vec3 p0, s0, p1, s1;
        glm::quat q0, q1;
        if (!SampleTRSFromNAC(*db, c, jointIndex, f0, p0, q0, s0) ||
            !SampleTRSFromNAC(*db, c, jointIndex, f1, p1, q1, s1)) {
            outDur = dur;
            outTicksPerSec = tps;
            return true;
        }
        glm::quat q1c = (glm::dot(q0, q1) < 0.0f) ? -q1 : q1;
        out.pos = glm::mix(p0, p1, a);
        out.rot = glm::normalize(glm::slerp(q0, q1c, a));
        out.scl = glm::mix(s0, s1, a);
    } else if (c.fromHAN0 && c.curves.size() >= 3) {
        // Non-character effect tracks commonly store a single TRS block.
        glm::vec3 p0, s0, p1, s1;
        glm::quat q0, q1;
        if (SampleTRSFromNAC(*db, c, 0, f0, p0, q0, s0) &&
            SampleTRSFromNAC(*db, c, 0, f1, p1, q1, s1)) {
            glm::quat q1c = (glm::dot(q0, q1) < 0.0f) ? -q1 : q1;
            out.pos = glm::mix(p0, p1, a);
            out.rot = glm::normalize(glm::slerp(q0, q1c, a));
            out.scl = glm::mix(s0, s1, a);
        }
    } else if (c.fromHAN0) {
        // Fallback: use first active curve as quaternion.
        auto k = [&](int frame) -> glm::vec4 {
            if (c.keyStride == 0) return glm::vec4(0, 0, 0, 1);
            uint32_t idx = c.keyBase + static_cast<uint32_t>(frame) * static_cast<uint32_t>(c.keyStride);
            if (idx >= db->keys.size()) return glm::vec4(0, 0, 0, 1);
            return db->keys[idx];
        };
        glm::vec4 rq0 = k(f0), rq1 = k(f1);
        glm::quat q0(rq0.w, rq0.x, rq0.y, rq0.z), q1(rq1.w, rq1.x, rq1.y, rq1.z);
        glm::quat q1c = (glm::dot(q0, q1) < 0.0f) ? -q1 : q1;
        out.rot = glm::normalize(glm::slerp(q0, q1c, a));
    }

    outDur = dur;
    outTicksPerSec = tps;
    return true;
}

const N3Clip* GetActiveClip(const ClipInstance& c) {
    N3File* db = GetNax3(c.source);
    if (!db || db->clips.empty()) return nullptr;
    const std::string& lookupNode = c.clipKeyNode.empty() ? c.node : c.clipKeyNode;
    int clipIdx = GetChosenClipIndex(c.source, lookupNode, c.owner);
    if (clipIdx < 0 || clipIdx >= static_cast<int>(db->clips.size())) {
        clipIdx = 0;
    }
    return &db->clips[clipIdx];
}

void EmitClipEvents(const ClipInstance& clipInst, const N3Clip& clip, float prevTicks, float curTicks, float totalTicks, bool looped) {
    if (clip.events.empty() || totalTicks <= 0.0f) return;

    auto emitIfInRange = [&](float from, float to) {
        for (const auto& ev : clip.events) {
            const float eventTick = static_cast<float>(ev.keyIndex);
            if (eventTick > from && eventTick <= to) {
                AnimEventInfoLite info;
                info.source = clipInst.source;
                info.node = clipInst.node;
                info.clip = clip.name;
                info.name = ev.name;
                info.category = ev.category;
                gFrameAnimEvents.push_back(info);
                if (gAnimEventCallback) {
                    gAnimEventCallback(info);
                }
            }
        }
    };

    if (!looped) {
        emitIfInRange(prevTicks, curTicks);
    } else {
        emitIfInRange(prevTicks, totalTicks);
        emitIfInRange(0.0f, curTicks);
    }
}
} // namespace

Nax3SampleFn GetBuiltInNax3Provider() {
    return &Nax3Sample;
}

void SetNax3Provider(Nax3SampleFn fn) {
    gNax3Sample = fn;
    std::cout << "[ANIM] SetNax3Provider=" << (fn ? "NON-NULL" : "NULL") << "\n";
}

void SetAnimEventCallback(AnimEventCallbackFn fn) {
    gAnimEventCallback = fn;
}

void InstallNax3Provider() {
    if (!gNax3Sample) {
        SetNax3Provider(GetBuiltInNax3Provider());
    }
}

void RegisterAnimationOwnerNodes(const void* owner, const std::unordered_map<std::string, Node*>& nodeMap) {
    if (!owner) return;

    auto& jointMap = gOwnerJointIndexMap[owner];
    jointMap.clear();
    auto& staticPoseMap = gOwnerStaticPoseMap[owner];
    staticPoseMap.clear();

    std::unordered_set<const Node*> visited;
    visited.reserve(nodeMap.size());
    for (const auto& [name, node] : nodeMap) {
        (void)name;
        if (!node || !visited.insert(node).second) continue;

        for (size_t i = 0; i < node->joints.size(); ++i) {
            const auto& j = node->joints[i];
            const int idx = (j.joint_idx >= 0) ? j.joint_idx : static_cast<int>(i);
            InsertJointName(jointMap, j.joint_name, idx);
        }

        AnimPose bindPose{};
        bindPose.pos = glm::vec3(node->position);
        bindPose.rot = glm::normalize(glm::quat(node->rotation.w, node->rotation.x, node->rotation.y, node->rotation.z));
        bindPose.scl = glm::max(glm::vec3(1e-6f), glm::vec3(node->scale));
        InsertOwnerPoseName(staticPoseMap, node->node_name, bindPose);
    }
}

void ClearAnimationOwnerData() {
    gOwnerJointIndexMap.clear();
    gOwnerStaticPoseMap.clear();
    gClipChoice.clear();
    gAnimPoseScoped.clear();
    gFrameAnimEvents.clear();
    gClipSerialCounter = 1;
}

void ClearAnimationOwnerData(const void* owner) {
    if (!owner) {
        ClearAnimationOwnerData();
        return;
    }

    gOwnerJointIndexMap.erase(owner);
    gOwnerStaticPoseMap.erase(owner);

    for (const auto& c : gClips) {
        if (c.owner != owner) continue;
        const std::string clipLookupNode =
            c.clipKeyNode.empty() ? MakeClipLookupNode(c.node, c.trackIndex) : c.clipKeyNode;
        gClipChoice.erase(HashKey(c.source, clipLookupNode, owner));
    }

    gClips.erase(std::remove_if(gClips.begin(), gClips.end(),
                 [owner](const ClipInstance& c) { return c.owner == owner; }),
             gClips.end());
}

void StopClip(const std::string& source, const std::string& node, const void* owner) {
    for (auto& c : gClips) if (c.source == source && c.node == node && c.owner == owner) {
        c.active = false;
    }
}

void PlayClip(const std::string& source, const std::string& node, int clipIndex, bool loop, const void* owner) {
    PlayClipAdvanced(source, node, clipIndex, loop, 0, 1.0f, 0.0f, 0.0f, owner);
}

void PlayClip(const std::string& source, const std::string& node, const std::string& clipName, bool loop, const void* owner) {
    int idx = FindClipIndexByName(source, clipName);
    PlayClipAdvanced(source, node, idx, loop, 0, 1.0f, 0.0f, 0.0f, owner);
}

void PlayClipAdvanced(const std::string& source,
                      const std::string& node,
                      int clipIndex,
                      bool loop,
                      int trackIndex,
                      float blendWeight,
                      float fadeInTime,
                      float fadeOutTime,
                      const void* owner) {
    if (!gNax3Sample) {
        SetNax3Provider(GetBuiltInNax3Provider());
    }

    const std::string clipLookupNode = MakeClipLookupNode(node, trackIndex);
    SetChosenClipIndex(source, clipLookupNode, clipIndex, owner);
    bool reused = false;
    for (auto& c : gClips) {
        if (c.source == source && c.node == node && c.owner == owner && c.trackIndex == trackIndex) {
            c.t = 0.0f; c.ticks = 0.0f; c.dur = 0.0f; c.tps = 0.0f;
            c.clipKeyNode = clipLookupNode;
            c.active = true;
            c.loop = loop;
            c.blendWeight = std::max(0.0f, blendWeight);
            c.fadeInTime = std::max(0.0f, fadeInTime);
            c.fadeOutTime = std::max(0.0f, fadeOutTime);
            c.trackIndex = trackIndex;
            c.serial = gClipSerialCounter++;
            reused = true;
            break;
        }
    }
    if (!reused) {
        ClipInstance ci;
        ci.source = source;
        ci.node = node;
        ci.clipKeyNode = clipLookupNode;
        ci.owner = owner;
        ci.loop = loop;
        ci.blendWeight = std::max(0.0f, blendWeight);
        ci.fadeInTime = std::max(0.0f, fadeInTime);
        ci.fadeOutTime = std::max(0.0f, fadeOutTime);
        ci.trackIndex = trackIndex;
        ci.serial = gClipSerialCounter++;
        ci.active = true;
        gClips.push_back(ci);
    }
}

void PlayClipAdvanced(const std::string& source,
                      const std::string& node,
                      const std::string& clipName,
                      bool loop,
                      int trackIndex,
                      float blendWeight,
                      float fadeInTime,
                      float fadeOutTime,
                      const void* owner) {
    int idx = FindClipIndexByName(source, clipName);
    PlayClipAdvanced(source, node, idx, loop, trackIndex, blendWeight, fadeInTime, fadeOutTime, owner);
}

void UpdateAnimations(float dt) {
    if (!gNax3Sample) {
        SetNax3Provider(GetBuiltInNax3Provider());
    }

    if (!gNax3Sample) {
        static bool warnedMissingProvider = false;
        if (!warnedMissingProvider) {
            std::cerr << "[ANIM] Update: provider=NULL, clips=" << gClips.size() << "\n";
            warnedMissingProvider = true;
        }
        return;
    }

    gAnimPose.clear();
    gAnimPoseScoped.clear();
    gFrameAnimEvents.clear();

    static int frameCount = 0;
    frameCount++;

    struct ClipSampleState {
        const void* owner = nullptr;
        std::string node;
        AnimPose pose{};
        int trackIndex = 0;
        uint64_t serial = 0;
        float mixWeight = 1.0f;
    };
    std::vector<ClipSampleState> clipSamples;
    clipSamples.reserve(gClips.size());

    for (auto& c : gClips) {
        if (!c.active) continue;

        const std::string& sampleNode = c.clipKeyNode.empty() ? c.node : c.clipKeyNode;
        const int jointIndex = ResolveJointIndex(c.owner, c.node);
        const N3Clip* currentClip = GetActiveClip(c);

        if (c.dur <= 0.0f || c.tps <= 0.0f) {
            AnimPose p{};
            float d = 0.0f;
            float tps = 0.0f;
            bool ok0 = gNax3Sample(c.source, 0.0f, sampleNode, c.owner, jointIndex, p, d, tps);
            if (ok0 && d > 0.0f) c.dur = d;
            if (ok0 && tps > 0.0f) c.tps = tps;
            if (c.dur <= 0.0f) c.dur = 1.0f;
            if (c.tps <= 0.0f) c.tps = 30.0f;
            c.ticks = 0.0f;
            c.t = 0.0f;
        }

        const float rate = std::max(1e-4f, c.tps) * c.speed * gAnimTimeScale;
        const float maxStepTicks = std::max(1.0f, c.tps * 0.25f);
        const float stepTicks = std::min(maxStepTicks, dt * rate);
        const float prevTicks = c.ticks;
        c.ticks += stepTicks;

        const float totalTicks = c.dur * c.tps;
        bool looped = false;
        if (totalTicks > 0.0f && c.ticks >= totalTicks) {
            if (c.loop) {
                while (c.ticks >= totalTicks) {
                    c.ticks -= totalTicks;
                    looped = true;
                }
                if (c.ticks < 0.0f) c.ticks = 0.0f;
            } else {
                c.ticks = totalTicks;
                c.active = false;
            }
        }

        if (currentClip) {
            EmitClipEvents(c, *currentClip, prevTicks, c.ticks, totalTicks, looped);
        }

        c.t = c.ticks / std::max(1e-4f, c.tps);

        AnimPose pose{};
        float dur = 0.0f;
        float tps = 0.0f;
        bool ok = gNax3Sample(c.source, c.t, sampleNode, c.owner, jointIndex, pose, dur, tps);
        if (!ok) {
            continue;
        }

        if (dur > 0.0f) c.dur = dur;
        if (tps > 0.0f) c.tps = tps;

        ClipSampleState sample;
        sample.owner = c.owner;
        sample.node = c.node;
        sample.pose = pose;
        sample.trackIndex = c.trackIndex;
        sample.serial = c.serial;
        sample.mixWeight = ComputeClipMixWeight(c);
        clipSamples.push_back(std::move(sample));
    }

    std::unordered_map<uint64_t, std::vector<ClipSampleState>> grouped;
    grouped.reserve(clipSamples.size());
    for (auto& s : clipSamples) {
        grouped[HashOwnerNode(s.owner, s.node)].push_back(std::move(s));
    }

    for (auto& [key, samples] : grouped) {
        (void)key;
        if (samples.empty()) continue;
        std::sort(samples.begin(), samples.end(), [](const ClipSampleState& a, const ClipSampleState& b) {
            if (a.trackIndex != b.trackIndex) return a.trackIndex < b.trackIndex;
            return a.serial < b.serial;
        });

        AnimPose mixed{};
        if (!TryGetOwnerStaticPose(samples.front().owner, samples.front().node, mixed)) {
            if (const AnimPose* staticPose = FindPoseBySuffix(gStaticPose, samples.front().node)) {
                mixed = *staticPose;
            } else {
                mixed.pos = glm::vec3(0.0f);
                mixed.rot = glm::quat(1, 0, 0, 0);
                mixed.scl = glm::vec3(1.0f);
            }
        }

        for (const auto& sample : samples) {
            mixed = BlendPose(mixed, sample.pose, sample.mixWeight);
        }

        SetAnimatedPose(samples.back().owner, samples.back().node, mixed);
    }
}

void SetAnimatedPose(const void* owner, const std::string& nodeName, const AnimPose& pose) {
    if (nodeName.empty()) return;
    gAnimPose[nodeName] = pose;
    gAnimPoseScoped[HashOwnerNode(owner, nodeName)] = pose;
    const std::string leaf = LeafNodeName(nodeName);
    if (!leaf.empty()) {
        gAnimPoseScoped[HashOwnerNode(owner, leaf)] = pose;
    }
}

bool HasAnimatedPose(const void* owner, const std::string& nodeName) {
    AnimPose pose{};
    if (TryGetScopedPose(owner, nodeName, pose)) {
        return true;
    }
    return FindPoseBySuffix(gAnimPose, nodeName) != nullptr;
}

glm::mat4 MakeLocal(const void* owner, const std::string& nodeName) {
    AnimPose finalPose;
    if (const AnimPose* staticPose = FindPoseBySuffix(gStaticPose, nodeName)) {
        finalPose = *staticPose;
    } else {
        finalPose.pos = {0, 0, 0};
        finalPose.rot = glm::quat(1, 0, 0, 0);
        finalPose.scl = {1, 1, 1};
    }

    AnimPose scopedPose{};
    if (TryGetScopedPose(owner, nodeName, scopedPose)) {
        finalPose = scopedPose;
    } else if (const AnimPose* animPose = FindPoseBySuffix(gAnimPose, nodeName)) {
        finalPose = *animPose;
    }

    glm::mat4 m(1.0f);
    m = glm::translate(m, finalPose.pos);
    m *= glm::mat4_cast(finalPose.rot);
    m = glm::scale(m, finalPose.scl);
    return m;
}

glm::mat4 MakeLocal(const std::string& nodeName) {
    return MakeLocal(nullptr, nodeName);
}
