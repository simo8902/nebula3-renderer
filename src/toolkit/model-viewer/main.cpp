// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "MdlViewer.h"
#include "Logger.h"
#include <iostream>

int main(int argc, char** argv) {
    Logger::SetLevel(Logger::INFO);
    Logger::Info("Model Viewer Starting");

    if (argc < 2) {
        std::cout << "Usage: model-viewer <path_to_n3_file>\n";
        Logger::Error("No N3 file specified");
        return 1;
    }

    MdlViewer viewer;
    if (!viewer.initialize(argv[1])) {
        Logger::Error("Failed to initialize model viewer");
        return 1;
    }

    viewer.run();
    Logger::Info("Model Viewer Exiting");
    return 0;
}
