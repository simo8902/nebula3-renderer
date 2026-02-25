// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_VALIDATIONLAYER_H
#define NDEVC_VALIDATIONLAYER_H

#include <cmath>
#include <string>
#include <iostream>
#include <limits>

#if defined(_WIN32)
#include <windows.h>
#endif

inline void EnableAnsiColors() {
#if defined(_WIN32)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hOut, &mode)) {
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}
class ValidationLayer {
public:
    static bool enabled;
    static bool verbose;

    enum class Status {
        OK,
        SUSPICIOUS,
        DEFINITELY_WRONG
    };

    struct Result {
        Status status;
        std::string message;

        bool isOK() const { return status == Status::OK; }
        bool isSuspicious() const { return status == Status::SUSPICIOUS; }
        bool isWrong() const { return status == Status::DEFINITELY_WRONG; }
    };

    static Result validate(float value, const std::string& name,
                          float maxAbsValue = 1e6f,
                          const std::string& indent = "") {
        if (!enabled) return {Status::OK, ""};

        // Check for NaN
        if (std::isnan(value)) {
            std::string msg = indent + "\x1b[93mERR detected [" + name + "] Suspicious value!\x1b[0m\n";
            if (enabled) std::cout << msg;
            return {Status::DEFINITELY_WRONG, msg};

        }

        // Check for Infinity
        if (std::isinf(value)) {
            std::string msg = indent + "\x1b[93mERR detected [" + name + "] Infinity detected - DEFINITELY MISALIGNED!\x1b[0m\n";
            if (enabled) std::cout << msg;
            return {Status::DEFINITELY_WRONG, msg};
        }

        // Check for suspiciously large values
        if (std::fabs(value) > maxAbsValue) {
            std::string msg = indent + "\x1b[93mERR detected [" + name + "] Suspiciously large value ("
                + std::to_string(value) + " > " + std::to_string(maxAbsValue)
                + ") - LIKELY MISALIGNED!\x1b[0m\n";
            if (enabled) std::cout << msg;
            return {Status::SUSPICIOUS, msg};
        }

        // Check for denormalized numbers (very close to zero but not zero)
        // These can indicate misalignment
        if (value != 0.0f && std::fabs(value) < std::numeric_limits<float>::min()) {
            if (verbose) {
                std::string msg = indent + "\x1b[34mℹERR detected [" + name + "] Denormalized float detected: "
                          + std::to_string(value) + "\x1b[0m\n";
                std::cout << msg;
            }
        }

        if (verbose) {
            std::cout << indent << "✓ [" << name << "] = " << value << " (OK)\n";
        }

        return {Status::OK, ""};
    }

    // ========== DOUBLE VALIDATION ==========
    static Result validate(double value, const std::string& name,
                          double maxAbsValue = 1e6,
                          const std::string& indent = "") {
        if (!enabled) return {Status::OK, ""};

        if (std::isnan(value)) {
            std::string msg = indent + "\x1b[31mERR detected [" + name + "] NaN detected - DEFINITELY MISALIGNED!\x1b[0m\n";
            if (enabled) std::cout << msg;
            return {Status::DEFINITELY_WRONG, msg};
        }

        if (std::isinf(value)) {
            std::string msg = indent + "\x1b[31mERR detected [" + name + "] Infinity detected - DEFINITELY MISALIGNED!\x1b[0m\n";
            if (enabled) std::cout << msg;
            return {Status::DEFINITELY_WRONG, msg};
        }

        if (std::fabs(value) > maxAbsValue) {
            std::string msg = indent + "\x1b[33mERR detected [" + name + "] Suspiciously large value ("
                            + std::to_string(value) + ") - LIKELY MISALIGNED!\x1b[0m\n";
            if (enabled) std::cout << msg;
            return {Status::SUSPICIOUS, msg};
        }

        if (verbose) {
            std::cout << indent << "✓ [" << name << "] = " << value << " (OK)\n";
        }

        return {Status::OK, ""};
    }

    // ========== INTEGER VALIDATION ==========
    static Result validate(int32_t value, const std::string& name,
                          int32_t minValue = -1000000,
                          int32_t maxValue = 1000000,
                          const std::string& indent = "") {
        if (!enabled) return {Status::OK, ""};

        if (value < minValue || value > maxValue) {
            std::string msg = indent + "⚠️  [" + name + "] Out of range ("
                            + std::to_string(value) + " not in ["
                            + std::to_string(minValue) + ", " + std::to_string(maxValue)
                            + "]) - LIKELY MISALIGNED!\n";
            if (enabled) std::cout << msg;
            return {Status::SUSPICIOUS, msg};
        }

        if (verbose) {
            std::cout << indent << "✓ [" << name << "] = " << value << " (OK)\n";
        }

        return {Status::OK, ""};
    }

    // ========== UNSIGNED INTEGER VALIDATION ==========
    static Result validate(uint32_t value, const std::string& name,
                          uint32_t maxValue = 1000000,
                          const std::string& indent = "") {
        if (!enabled) return {Status::OK, ""};

        if (value > maxValue) {
            std::string msg = indent + "⚠️  [" + name + "] Suspiciously large value ("
                            + std::to_string(value) + " > " + std::to_string(maxValue)
                            + ") - LIKELY MISALIGNED!\n";
            if (enabled) std::cout << msg;
            return {Status::SUSPICIOUS, msg};
        }

        if (verbose) {
            std::cout << indent << "✓ [" << name << "] = " << value << " (OK)\n";
        }

        return {Status::OK, ""};
    }

    // ========== SPECIALIZED VALIDATORS ==========

    static Result validateExtent(float value, const std::string& name, const std::string& indent = "") {
        if (!enabled) return {Status::OK, ""};

        if (value < 0.0f) {
            std::string msg = indent + "\x1b[31mERR detected [" + name + "] Negative extent ("
                  + std::to_string(value) + ") - DEFINITELY MISALIGNED!\x1b[0m\n";
            if (enabled) std::cout << msg;
            return {Status::DEFINITELY_WRONG, msg};
        }

        return validate(value, name, 1e6f, indent);
    }

    static Result validateQuaternionComponent(float value, const std::string& name, const std::string& indent = "") {
        if (!enabled) return {Status::OK, ""};

        auto result = validate(value, name, 2.0f, indent);

        if (std::fabs(value) > 1.5f && result.isOK()) {
            std::string msg = indent + "\x1b[93mERR detected [" + name + "] Quaternion component out of typical range ("
                   + std::to_string(value) + ") - SUSPICIOUS!\x1b[0m\n";
            if (enabled) std::cout << msg;
            return {Status::SUSPICIOUS, msg};
        }

        return result;
    }

    // Validate normalized value (should be in [0, 1] range)
    static Result validateNormalized(float value, const std::string& name, const std::string& indent = "") {
        if (!enabled) return {Status::OK, ""};

        auto result = validate(value, name, 2.0f, indent);

        if ((value < 0.0f || value > 1.0f) && result.isOK()) {
            std::string msg = indent + "\x1b[93mERR detected [" + name + "] Normalized value out of range [0,1] ("
                         + std::to_string(value) + ") - SUSPICIOUS!\x1b[0m\n";
            if (enabled) std::cout << msg;
            return {Status::SUSPICIOUS, msg};
        }

        return result;
    }

    // Validate color component (should be in [0, 1] range typically, but can go higher for HDR)
    static Result validateColor(float value, const std::string& name, const std::string& indent = "") {
        if (!enabled) return {Status::OK, ""};

        auto result = validate(value, name, 100.0f, indent);

        if (value < 0.0f && result.isOK()) {
            std::string msg = indent + "\x1b[33mERR detected [" + name + "] Negative color value ("
                  + std::to_string(value) + ") - SUSPICIOUS!\x1b[0m\n";
            if (enabled) std::cout << msg;
            return {Status::SUSPICIOUS, msg};
        }

        return result;
    }

    // Validate string length (detect corrupted strings)
    static Result validateStringLength(size_t length, const std::string& name,
                                       size_t maxLength = 1024,
                                       const std::string& indent = "") {
        if (!enabled) return {Status::OK, ""};

        if (length > maxLength) {
            std::string msg = indent + "\x1b[33mERR detected [" + name + "] Suspiciously long string ("
                            + std::to_string(length) + " > " + std::to_string(maxLength)
                            + ") - LIKELY MISALIGNED!\x1b[0m\n";
            if (enabled) std::cout << msg;
            return {Status::SUSPICIOUS, msg};
        }

        if (verbose) {
            std::cout << indent << "✓ [" << name << "] length = " << length << " (OK)\n";
        }

        return {Status::OK, ""};
    }

    // ========== BATCH VALIDATION ==========

    static bool anyFailed(std::initializer_list<Result> results) {
        for (const auto& r : results) {
            if (r.isWrong() || r.isSuspicious()) return true;
        }
        return false;
    }

    static void printSummary(const std::string& tagName,
                           std::initializer_list<Result> results,
                           const std::string& indent = "") {
        if (!enabled) return;

        if (anyFailed(results)) {
            std::cout << indent << "[" << tagName << "] VALIDATION FAILED - CHECK ABOVE\n";
        } else if (verbose) {
            std::cout << indent << "[" << tagName << "] All validations passed\n";
        }
    }
};

inline bool ValidationLayer::enabled = true;
inline bool ValidationLayer::verbose = false;

#define VALIDATE(value, name) ValidationLayer::validate(value, name, 1e6f, ind)
#define VALIDATE_EXTENT(value, name) ValidationLayer::validateExtent(value, name, ind)
#define VALIDATE_QUAT(value, name) ValidationLayer::validateQuaternionComponent(value, name, ind)
#define VALIDATE_COLOR(value, name) ValidationLayer::validateColor(value, name, ind)
#define VALIDATE_NORM(value, name) ValidationLayer::validateNormalized(value, name, ind)

#endif //NDEVC_VALIDATIONLAYER_H
