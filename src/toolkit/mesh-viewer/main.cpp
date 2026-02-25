// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "MeshViewer.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: mesh-viewer <path_to_nvx2_file>\n";
        return 1;
    }

    MeshViewer viewer;
    if (!viewer.initialize(argv[1])) {
        return 1;
    }

    viewer.run();
    return 0;
}
