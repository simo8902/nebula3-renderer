// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_EXPORT_PACKAGE_READER_H
#define NDEVC_EXPORT_PACKAGE_READER_H

#include <cstdint>
#include <string>

namespace NDEVC::Export {

struct PackageMountResult {
    bool        success = false;
    std::string packagePath;
    std::string mountRoot;
    std::string startupMapPath;
    uint32_t    extractedAssetCount = 0;
    std::string errorMsg;
};

// Mounts the package by materializing packaged assets to outputDir.
// Returns startupMapPath resolved from packaged .map assets when available.
PackageMountResult MountPackageToDirectory(const std::string& packagePath,
                                           const std::string& outputDir);

} // namespace NDEVC::Export

#endif // NDEVC_EXPORT_PACKAGE_READER_H
