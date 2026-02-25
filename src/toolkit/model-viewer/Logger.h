// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_LOGGER_H
#define NDEVC_LOGGER_H

#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

class Logger {
public:
    enum Level {
        DEBUG,
        INFO,
        WARN,
        ERR
    };

    static void SetLevel(Level level);
    static void Debug(const std::string& msg);
    static void Info(const std::string& msg);
    static void Warn(const std::string& msg);
    static void Error(const std::string& msg);

private:
    static Level minLevel;

    static void Log(Level level, const std::string& msg);
};
#endif