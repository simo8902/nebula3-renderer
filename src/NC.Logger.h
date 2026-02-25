// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_LOGGING_SYSTEM
#define NDEVC_LOGGING_SYSTEM

#include <iostream>

namespace NC::LOGGING {
    static constexpr bool ENABLED = true;

    template<typename... Args>
    inline void Log(Args&&... args) {
        if constexpr (ENABLED) {
            std::cout << "\033[0m";
            (std::cout << ... << args);
            std::cout << "\033[0m\n";
        }
    }

    template<typename... Args>
    inline void Warning(Args&&... args) {
        if constexpr (ENABLED) {
            std::cout << "\033[33m[WARNING] ";
            (std::cout << ... << args);
            std::cout << "\033[0m\n";
        }
    }

    template<typename... Args>
    inline void Error(Args&&... args) {
        if constexpr (ENABLED) {
            std::cout << "\033[31m[ERROR] ";
            (std::cout << ... << args);
            std::cout << "\033[0m\n";
        }
    }
}
#endif