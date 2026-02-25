// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVCHEADERS_H_
#define NDEVCHEADERS_H_

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef APIENTRY
#define APIENTRY __stdcall
#include <wincodec.h>
#include <combaseapi.h>
#endif

// #include <DirectXTex/DirectXTex.h>

#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <vector>
#include <array>
#include <random>
#include <unordered_set>
#include <sstream>
#include <set>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <map>
#include <deque>
#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <cstdlib>

namespace NDEVC::Paths {
inline std::string ReadEnvString(const char* name) {
    if (!name || !name[0]) return {};
#if defined(_WIN32)
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) return {};
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

inline std::string NormalizeRoot(std::filesystem::path p) {
    std::error_code ec;
    p = std::filesystem::weakly_canonical(p, ec);
    if (ec) {
        p = p.lexically_normal();
    }
    std::string out = p.generic_string();
    if (!out.empty() && out.back() != '/') out.push_back('/');
    return out;
}

inline std::string ResolveAssetRoot(const char* envName,
                                    std::initializer_list<std::filesystem::path> relativeCandidates) {
    std::error_code ec;

    if (const std::string envPath = ReadEnvString(envName); !envPath.empty()) {
        const std::filesystem::path envRoot(envPath);
        if (std::filesystem::exists(envRoot, ec) && std::filesystem::is_directory(envRoot, ec)) {
            return NormalizeRoot(envRoot);
        }
        std::cerr << "[PATH][ERROR] Invalid " << envName << "='" << envPath << "' (directory not found)\n";
    }

#ifdef SOURCE_DIR
    for (const auto& rel : relativeCandidates) {
        const std::filesystem::path candidate = std::filesystem::path(SOURCE_DIR) / rel;
        if (std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec)) {
            return NormalizeRoot(candidate);
        }
    }
#endif

    for (const auto& rel : relativeCandidates) {
        if (std::filesystem::exists(rel, ec) && std::filesystem::is_directory(rel, ec)) {
            return NormalizeRoot(rel);
        }
    }

    const auto fallback = NormalizeRoot(std::filesystem::current_path(ec));
    std::cerr << "[PATH][ERROR] Could not resolve " << envName
              << "; using working directory '" << fallback << "'\n";
    return fallback;
}
} // namespace NDEVC::Paths

namespace NDEVC::Paths {
inline const std::string& GetModelsRoot() {
    static const std::string value = ResolveAssetRoot(
        "NDEVC_MODELS_ROOT", {"assets/models", "models", "bin/models"});
    return value;
}

inline const std::string& GetMeshesRoot() {
    static const std::string value = ResolveAssetRoot(
        "NDEVC_MESHES_ROOT", {"assets/meshes", "meshes", "bin/meshes"});
    return value;
}

inline const std::string& GetMapsRoot() {
    static const std::string value = ResolveAssetRoot(
        "NDEVC_MAPS_ROOT", {"assets/maps", "maps", "bin/maps"});
    return value;
}

inline const std::string& GetAnimsRoot() {
    static const std::string value = ResolveAssetRoot(
        "NDEVC_ANIMS_ROOT", {"assets/anims", "anims", "bin/anims"});
    return value;
}

inline const std::string& GetTexturesRoot() {
    static const std::string value = ResolveAssetRoot(
        "NDEVC_TEXTURES_ROOT", {"assets/textures", "textures", "bin/textures"});
    return value;
}
} // namespace NDEVC::Paths

#define MODELS_ROOT (NDEVC::Paths::GetModelsRoot())
#define MESHES_ROOT (NDEVC::Paths::GetMeshesRoot())
#define MAP_ROOT (NDEVC::Paths::GetMapsRoot())
#define ANIMS_ROOT (NDEVC::Paths::GetAnimsRoot())
#define TEXTURES_ROOT (NDEVC::Paths::GetTexturesRoot())
#endif
