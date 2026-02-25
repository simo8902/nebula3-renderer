// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.
#ifndef NDEVC_LOGGING_H
#define NDEVC_LOGGING_H

#include <iostream>

namespace NC::LOGGING {
    template<typename... Args>
    inline void Log(Args&&... args) {
        std::cout << "\033[0m";
        (std::cout << ... << args);
        std::cout << "\033[0m\n";
    }

    template<typename... Args>
    inline void Warning(Args&&... args) {
        std::cout << "\033[33m[WARNING] ";
        (std::cout << ... << args);
        std::cout << "\033[0m\n";
    }

    template<typename... Args>
    inline void Error(Args&&... args) {
        std::cout << "\033[31m[ERROR] ";
        (std::cout << ... << args);
        std::cout << "\033[0m\n";
    }
}
#endif