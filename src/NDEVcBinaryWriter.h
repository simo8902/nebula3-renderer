// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_NDEVCBINARYWRITER_H
#define NDEVC_NDEVCBINARYWRITER_H

#include "NDEVcHeaders.h"

class NDEVcBinaryWriter {
public:
    NDEVcBinaryWriter(const std::string& filepath, bool swapBytes = false)
        : file(filepath, std::ios::binary), swapDataBytes(swapBytes) {}

    ~NDEVcBinaryWriter() { if (file.is_open()) file.close(); }

    bool isOpen() const { return file.is_open() && !file.fail(); }

    void writeBool(bool b) { file.write(reinterpret_cast<char*>(&b), sizeof(b)); }
    void writeI8(int8_t v) { file.write(reinterpret_cast<char*>(&v), 1); }
    void writeU8(uint8_t v) { file.write(reinterpret_cast<char*>(&v), 1); }
    void writeI16(int16_t v) { v = swap16(v); file.write(reinterpret_cast<char*>(&v), 2); }
    void writeU16(uint16_t v) { v = swap16(v); file.write(reinterpret_cast<char*>(&v), 2); }
    void writeI32(int32_t v) { v = (int32_t)swap32((uint32_t)v); file.write(reinterpret_cast<char*>(&v), 4); }
    void writeU32(uint32_t v) { v = swap32(v); file.write(reinterpret_cast<char*>(&v), 4); }
    void writeF32(float v) { uint32_t u = swap32(*reinterpret_cast<uint32_t*>(&v)); file.write(reinterpret_cast<char*>(&u), 4); }
    void writeF64(double v) { uint64_t u = swap64(*reinterpret_cast<uint64_t*>(&v)); file.write(reinterpret_cast<char*>(&u), 8); }

    void writeString(const std::string& s) {
        writeU16((uint16_t)s.length());
        if (!s.empty()) file.write(s.data(), s.length());
    }

    void writeFourCC(const std::string& cc) { file.write(cc.data(), 4); }
    void writeRawData(const void* ptr, size_t bytes) { file.write((const char*)ptr, bytes); }

private:
    std::ofstream file;
    bool swapDataBytes;

    uint16_t swap16(uint16_t v) { return swapDataBytes ? ((v >> 8) | (v << 8)) : v; }
    uint32_t swap32(uint32_t v) {
        if (!swapDataBytes) return v;
        return ((v & 0xff) << 24) | ((v & 0xff00) << 8) | ((v & 0xff0000) >> 8) | ((v >> 24) & 0xff);
    }
    uint64_t swap64(uint64_t v) {
        if (!swapDataBytes) return v;
        return ((uint64_t)swap32(v & 0xffffffff) << 32) | swap32((v >> 32) & 0xffffffff);
    }
};

#endif //NDEVC_NDEVCBINARYWRITER_H