// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_STANDALONE_BUILDER_H
#define NDEVC_STANDALONE_BUILDER_H

#include <string>
#include <vector>

namespace NDEVC::Export {

// ---------------------------------------------------------------------------
// StandaloneBuilder — produces a self-contained distributable directory
// analogous to Unity's "Build Player" output.
//
// Output layout (one blob, no raw asset files):
//   outputDir/
//     <executableName>.exe     — renamed NDEVC.exe
//     glfw3.dll                — GLFW runtime DLL
//     <mapname>.ndpk           — NDPK archive (all assets served in-memory at runtime)
// ---------------------------------------------------------------------------
struct StandaloneBuildConfig {
    std::string outputDir;
    std::string executableName = "NDEVC";

    // Engine project root — used to find NDEVC.exe and glfw3.dll.
    // Leave empty to use the compile-time SOURCE_DIR.
    std::string sourceDir;

    // Explicit DLL path. Auto-detected from sourceDir/libs/glfw/lib-vc2022/
    // if left empty.
    std::string glfwDllPath;

    // Path to the .ndpk produced by PackageNode (ctx.outputPackagePath).
    // Skipped if empty.
    std::string ndpkPath;

    // Optional startup map path that should be copied into data/maps.
    std::string startupMapPath;

    // Optional source assets to copy into data subfolders for runtime fallback.
    std::vector<std::string> assetPaths;
};

struct StandaloneBuildResult {
    bool success = false;

    // Path where NDEVC.exe was located. Non-empty even on failure if
    // the search completed.
    std::string runtimeExeFound;

    // All files/directories successfully copied into outputDir.
    std::vector<std::string> copiedFiles;

    std::string errorMsg;
};

// Builds a standalone distribution into cfg.outputDir.
// Thread-safe to call from the editor UI thread (no renderer state accessed).
StandaloneBuildResult BuildStandalone(const StandaloneBuildConfig& cfg);

} // namespace NDEVC::Export

#endif // NDEVC_STANDALONE_BUILDER_H
