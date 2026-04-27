// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_LOGGING_SYSTEM
#define NDEVC_LOGGING_SYSTEM

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <string_view>

namespace NC::LOGGING {

    enum class Level {
        Trace = 0,   // High-volume noise (per-frame, individual compilation steps)
        Debug,       // Detailed info for developers
        Info,        // General engine status (Scene loaded, window created)
        Warning,     // Recoverable issues
        Error,       // Critical failures
        Fatal        // Unrecoverable crash
    };

    enum class Category {
        Generic,
        Engine,
        Graphics,
        Shader,
        VFS,
        Assets,
        Animation,
        Input,
        Platform,
        Editor
    };

#ifdef NDEVC_DISABLE_LOGGING
    static constexpr bool ENABLED = false;
#else
    static constexpr bool ENABLED = true;
#endif

    inline std::mutex& LogMutex() {
        static std::mutex m;
        return m;
    }

    inline std::string_view LevelToString(Level level) {
        switch (level) {
            case Level::Trace:   return "TRACE";
            case Level::Debug:   return "DEBUG";
            case Level::Info:    return "INFO ";
            case Level::Warning: return "WARN ";
            case Level::Error:   return "ERROR";
            case Level::Fatal:   return "FATAL";
            default:             return "UNKN ";
        }
    }

    inline std::string_view CategoryToString(Category cat) {
        switch (cat) {
            case Category::Generic:   return "GEN";
            case Category::Engine:    return "ENG";
            case Category::Graphics:  return "GFX";
            case Category::Shader:    return "SHD";
            case Category::VFS:       return "VFS";
            case Category::Assets:    return "AST";
            case Category::Animation: return "ANI";
            case Category::Input:     return "INP";
            case Category::Platform:  return "PLT";
            case Category::Editor:    return "EDT";
            default:                  return "???";
        }
    }

    inline std::ostream& operator<<(std::ostream& os, Category cat) {
        return os << CategoryToString(cat);
    }

    inline std::ostream& operator<<(std::ostream& os, Level level) {
        return os << LevelToString(level);
    }


    inline Level GetMinLogLevel() {
        static Level minLevel = []() {
            const char* env = std::getenv("NDEVC_LOG_LEVEL");
            if (!env) return Level::Info; // Default to INFO (Shader TRACE/DEBUG will be hidden)
            std::string_view s(env);
            if (s == "TRACE") return Level::Trace;
            if (s == "DEBUG") return Level::Debug;
            if (s == "INFO")  return Level::Info;
            if (s == "WARN")  return Level::Warning;
            if (s == "ERROR") return Level::Error;
            return Level::Info;
        }();
        return minLevel;
    }

    inline std::string TimestampNow() {
        const auto now = std::chrono::system_clock::now();
        const auto tt = std::chrono::system_clock::to_time_t(now);
        const int ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
        std::tm tmValue{};
#if defined(_WIN32)
        localtime_s(&tmValue, &tt);
#else
        localtime_r(&tt, &tmValue);
#endif
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
            tmValue.tm_year + 1900, tmValue.tm_mon + 1, tmValue.tm_mday,
            tmValue.tm_hour, tmValue.tm_min, tmValue.tm_sec, ms);
        return buf;
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
            file.open(logPath, std::ios::out | std::ios::trunc);
            if (file.is_open()) {
                file << "===== NDEVC SESSION START " << TimestampNow() << " =====\n";
                file << "Log Level: " << LevelToString(GetMinLogLevel()) << "\n";
                file.flush();
            }
        }
        return file;
    }

    template<typename... Args>
    inline void WriteFormatted(Level level, Category category, Args&&... args) {
        if (level < GetMinLogLevel()) return;

        auto& file = LogFile();
        const std::lock_guard<std::mutex> lock(LogMutex());
        
        std::string ts = TimestampNow();
        std::string lvl = std::string(LevelToString(level));
        std::string cat = std::string(CategoryToString(category));

        if (file.is_open()) {
            file << "[" << ts << "] [" << lvl << "] [" << cat << "] ";
            (file << ... << args);
            file << "\n";
            file.flush();
        }

        // Mirror to console for immediate visibility and flush it
        if (level >= Level::Info) {
            std::ostream& os = (level >= Level::Error) ? std::cerr : std::cout;
            os << "[" << ts << "] [" << lvl << "] [" << cat << "] ";
            (os << ... << args);
            os << "\n";
            os.flush();
        }
    }

    // --- Modern Level-Based API ---

    template<typename... Args>
    inline void Trace(Category cat, Args&&... args) {
        if constexpr (ENABLED) WriteFormatted(Level::Trace, cat, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void Debug(Category cat, Args&&... args) {
        if constexpr (ENABLED) WriteFormatted(Level::Debug, cat, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void Info(Category cat, Args&&... args) {
        if constexpr (ENABLED) WriteFormatted(Level::Info, cat, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void Warn(Category cat, Args&&... args) {
        if constexpr (ENABLED) WriteFormatted(Level::Warning, cat, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void Error(Category cat, Args&&... args) {
        if constexpr (ENABLED) WriteFormatted(Level::Error, cat, std::forward<Args>(args)...);
    }

    // --- Legacy Compatibility Shim ---
    
    template<typename... Args>
    inline void Log(Args&&... args) {
        if constexpr (ENABLED) WriteFormatted(Level::Info, Category::Generic, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void Warning(Args&&... args) {
        if constexpr (ENABLED) WriteFormatted(Level::Warning, Category::Generic, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void Error(Args&&... args) {
        if constexpr (ENABLED) WriteFormatted(Level::Error, Category::Generic, std::forward<Args>(args)...);
    }
}

#endif
