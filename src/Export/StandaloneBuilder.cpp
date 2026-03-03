// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Export/StandaloneBuilder.h"
#include "Core/Logger.h"

#include <filesystem>
#include <system_error>

namespace NDEVC::Export {

namespace {

static const char* kRuntimeExeRelPaths[] = {
    "/cmake-build-debug/NDEVC.exe",
    "/cmake-build-release/NDEVC.exe",
    "/cmake-build-relwithdebinfo/NDEVC.exe",
    "/cmake-build-minsizerel/NDEVC.exe",
    "/bin/NDEVC.exe",
    "/bin/Debug/NDEVC.exe",
    "/bin/Release/NDEVC.exe",
    "/out/build/x64-Debug/NDEVC.exe",
    "/out/build/x64-Release/NDEVC.exe",
};

static std::string FindRuntimeExe(const std::string& srcDir) {
    for (const char* rel : kRuntimeExeRelPaths) {
        std::string candidate = srcDir + rel;
        if (std::filesystem::exists(candidate))
            return candidate;
    }
    return {};
}

} // anonymous namespace

StandaloneBuildResult BuildStandalone(const StandaloneBuildConfig& cfg) {
    namespace fs = std::filesystem;
    StandaloneBuildResult result;

    if (cfg.outputDir.empty()) {
        result.errorMsg = "outputDir is empty";
        NC::LOGGING::Error("[STANDALONE] ", result.errorMsg);
        return result;
    }

    const std::string srcDir = cfg.sourceDir.empty() ? SOURCE_DIR : cfg.sourceDir;
    NC::LOGGING::Log("[STANDALONE] Build srcDir=", srcDir, " outDir=", cfg.outputDir);

    // ── 1. Locate runtime exe ─────────────────────────────────────────────
    const std::string runtimeExe = FindRuntimeExe(srcDir);
    result.runtimeExeFound = runtimeExe;
    if (runtimeExe.empty()) {
        result.errorMsg  = "NDEVC.exe not found under: " + srcDir;
        result.errorMsg += "\n(Searched cmake-build-debug/release, bin, out/build/x64-*)";
        result.errorMsg += "\nBuild the NDEVC target first.";
        NC::LOGGING::Error("[STANDALONE] ", result.errorMsg);
        return result;
    }
    NC::LOGGING::Log("[STANDALONE] Found runtime exe: ", runtimeExe);

    // ── 2. Create output directory ────────────────────────────────────────
    std::error_code ec;
    fs::create_directories(cfg.outputDir, ec);
    if (ec) {
        result.errorMsg = "Failed to create outputDir: " + ec.message();
        NC::LOGGING::Error("[STANDALONE] ", result.errorMsg);
        return result;
    }

    // ── 3. Copy runtime exe ───────────────────────────────────────────────
    const std::string exeName = cfg.executableName.empty() ? "NDEVC" : cfg.executableName;
    const std::string destExe = cfg.outputDir + "/" + exeName + ".exe";
    fs::copy_file(runtimeExe, destExe, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        result.errorMsg = "Failed to copy runtime exe: " + ec.message();
        NC::LOGGING::Error("[STANDALONE] ", result.errorMsg);
        return result;
    }
    result.copiedFiles.push_back(destExe);
    NC::LOGGING::Log("[STANDALONE] Copied exe -> ", destExe);

    // ── 4. Copy glfw3.dll ─────────────────────────────────────────────────
    std::string glfwDll = cfg.glfwDllPath;
    if (glfwDll.empty())
        glfwDll = srcDir + "/libs/glfw/lib-vc2022/glfw3.dll";
    if (fs::exists(glfwDll)) {
        const std::string destGlfw = cfg.outputDir + "/glfw3.dll";
        fs::copy_file(glfwDll, destGlfw, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
            result.copiedFiles.push_back(destGlfw);
            NC::LOGGING::Log("[STANDALONE] Copied glfw3.dll -> ", destGlfw);
        } else {
            NC::LOGGING::Warning("[STANDALONE] glfw3.dll copy failed: ", ec.message());
        }
    } else {
        NC::LOGGING::Warning("[STANDALONE] glfw3.dll not found at: ", glfwDll);
    }

    // ── 5. Copy .ndpk to standalone root ─────────────────────────────────
    // The NDPK contains everything: scene assets + all pipeline shaders.
    // At runtime, the exe auto-discovers it by scanning the same directory,
    // extracts to %TEMP%/ndevc/<stem>/, and sets all env vars from there.
    if (!cfg.ndpkPath.empty()) {
        if (fs::exists(cfg.ndpkPath)) {
            const std::string ndpkName = fs::path(cfg.ndpkPath).filename().string();
            const std::string destNdpk = cfg.outputDir + "/" + ndpkName;
            fs::copy_file(cfg.ndpkPath, destNdpk, fs::copy_options::overwrite_existing, ec);
            if (!ec) {
                result.copiedFiles.push_back(destNdpk);
                NC::LOGGING::Log("[STANDALONE] Copied package -> ", destNdpk);
            } else {
                NC::LOGGING::Warning("[STANDALONE] .ndpk copy failed: ", ec.message());
            }
        } else {
            NC::LOGGING::Warning("[STANDALONE] .ndpk not found: ", cfg.ndpkPath);
        }
    }

    result.success = true;
    NC::LOGGING::Log("[STANDALONE] Build complete. Output: ", cfg.outputDir,
                     "  Files: ", result.copiedFiles.size());
    return result;
}

} // namespace NDEVC::Export

