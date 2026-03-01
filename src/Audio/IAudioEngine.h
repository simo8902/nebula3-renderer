#pragma once
#include <string>
namespace NDEVC::Audio {
    struct IAudioEngine {
        virtual ~IAudioEngine() = default;
        virtual void PlaySound(const std::string& path) = 0;
        virtual void StopAll() = 0;
    };
}
