// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_LOGGING_SYSTEM
#define NDEVC_LOGGING_SYSTEM

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace NC::LOGGING {
#ifdef NDEVC_DISABLE_LOGGING
    static constexpr bool ENABLED = false;
#else
    static constexpr bool ENABLED = true;
#endif

    inline std::mutex& LogMutex() {
        static std::mutex m;
        return m;
    }

    inline std::string TimestampNow() {
        const auto now = std::chrono::system_clock::now();
        const auto tt = std::chrono::system_clock::to_time_t(now);
        std::tm tmValue{};
#if defined(_WIN32)
        localtime_s(&tmValue, &tt);
#else
        localtime_r(&tt, &tmValue);
#endif
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::ostringstream oss;
        oss << std::put_time(&tmValue, "%Y-%m-%d %H:%M:%S")
            << "." << std::setw(3) << std::setfill('0') << ms.count();
        return oss.str();
    }

    inline std::ofstream& LogFile() {
        static std::ofstream file;
        static bool initialized = false;
        if (!initialized) {
            initialized = true;
            const char* envPath = std::getenv("NDEVC_DEBUG_LOG_FILE");
            std::filesystem::path logPath = (envPath && envPath[0])
                ? std::filesystem::path(envPath)
                : std::filesystem::path("ndevc_debug.log");
            std::error_code ec;
            if (logPath.has_parent_path()) {
                std::filesystem::create_directories(logPath.parent_path(), ec);
            }
            file.open(logPath, std::ios::out | std::ios::app);
            if (file.is_open()) {
                const std::filesystem::path absPath = std::filesystem::absolute(logPath, ec);
                file << "===== SESSION START " << TimestampNow() << " PATH=" << absPath.string() << " =====\n";
                file.flush();
                // NOTE: stdout/stderr redirect removed — it causes any stray
                // cout/cerr anywhere (including libraries) to trigger disk I/O,
                // killing frame rate.  All engine logging goes through Log/Warning/Error
                // which write to the file directly.
            }
        }
        return file;
    }

    template<typename... Args>
    inline void WriteFileLineUnlocked(const char* level, Args&&... args) {
        auto& file = LogFile();
        if (!file.is_open()) {
            return;
        }
        file << "[" << TimestampNow() << "] [T:" << std::this_thread::get_id() << "] [" << level << "] ";
        (file << ... << args);
        file << "\n";
    }

    template<typename... Args>
    inline void Log(Args&&... args) {
        if constexpr (ENABLED) {
            const std::lock_guard<std::mutex> lock(LogMutex());
            WriteFileLineUnlocked("INFO", std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    inline void Warning(Args&&... args) {
        if constexpr (ENABLED) {
            const std::lock_guard<std::mutex> lock(LogMutex());
            WriteFileLineUnlocked("WARNING", std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    inline void Error(Args&&... args) {
        if constexpr (ENABLED) {
            const std::lock_guard<std::mutex> lock(LogMutex());
            WriteFileLineUnlocked("ERROR", std::forward<Args>(args)...);
        }
    }
}
#endif
