// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Logger.h"

Logger::Level Logger::minLevel = Logger::INFO;

void Logger::SetLevel(Level level) {
    minLevel = level;
}

void Logger::Debug(const std::string& msg) {
    Log(DEBUG, msg);
}

void Logger::Info(const std::string& msg) {
    Log(INFO, msg);
}

void Logger::Warn(const std::string& msg) {
    Log(WARN, msg);
}

void Logger::Error(const std::string& msg) {
    Log(ERR, msg);
}

void Logger::Log(Level level, const std::string& msg) {
    if (level < minLevel) return;

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%H:%M:%S")
       << "." << std::setfill('0') << std::setw(3) << ms.count();

    std::string levelStr;
    std::ostream* out = &std::cout;

    switch (level) {
        case DEBUG:
            levelStr = "DEBUG";
            break;
        case INFO:
            levelStr = "INFO ";
            break;
        case WARN:
            levelStr = "WARN ";
            out = &std::cerr;
            break;
        case ERR:
            levelStr = "ERROR";
            out = &std::cerr;
            break;
    }

    *out << "[" << ss.str() << "] [" << levelStr << "] " << msg << "\n";
    out->flush();
}
