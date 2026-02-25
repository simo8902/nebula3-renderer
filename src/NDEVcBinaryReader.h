// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_NDEVCBINARYREADER_H
#define NDEVC_NDEVCBINARYREADER_H

#include "NDEVcHeaders.h"

class NDEVcBinaryReader {
public:
    NDEVcBinaryReader(const std::string& filepath, bool swapBytes = false)
        : file(filepath, std::ios::binary), swapDataBytes(swapBytes) {}

    ~NDEVcBinaryReader() { if (file.is_open()) file.close(); }

    bool isOpen() const { return file.is_open() && !file.fail(); }
    bool eof() const { return file.eof(); }

    // Allow changing byte swap mode after construction (needed after reading magic)
    void setSwapBytes(bool swap) { swapDataBytes = swap; }

    bool readBool(bool& out) { return read(&out, sizeof(bool)); }
    bool readI8(int8_t& out) { uint8_t u; if (!read(&u, 1)) return false; out = (int8_t)u; return true; }
    bool readU8(uint8_t& out) { return read(&out, 1); }
    bool readI16(int16_t& out) { if (!read(&out, 2)) return false; out = (int16_t)swap16((uint16_t)out); return true; }
    bool readU16(uint16_t& out) { if (!read(&out, 2)) return false; out = swap16(out); return true; }
    bool readI32(int32_t& out) { if (!read(&out, 4)) return false; out = (int32_t)swap32((uint32_t)out); return true; }
    bool readU32(uint32_t& out) { if (!read(&out, 4)) return false; out = swap32(out); return true; }

    bool readF32(float& out) {
        uint32_t u;
        if (!readU32(u)) return false;
        std::memcpy(&out, &u, sizeof(float));
        return true;
    }

    bool readF64(double& out) {
        uint64_t u;
        if (!read(&u, 8)) return false;
        u = swap64(u);
        std::memcpy(&out, &u, sizeof(double));
        return true;
    }

    bool readFourCC(std::string& out) {
        char buf[4];
        if (!read(buf, 4)) return false;
        out.assign(buf, 4);
        return true;
    }

    bool readString(std::string& out) {
        uint16_t len;
        if (!readU16(len)) return false;
        if (len == 0) { out.clear(); return true; }
        char* buf = new char[len];
        if (!read(buf, len)) { delete[] buf; return false; }
        out.assign(buf, len);
        delete[] buf;
        return true;
    }

    bool readRawData(void* ptr, size_t bytes) { return read(ptr, bytes); }

    std::streampos tell() { return file.tellg(); }
    void seek(std::streampos pos) { file.seekg(pos); }
    void seekRel(std::streamoff off) { file.seekg(off, std::ios::cur); }
    void seekEnd() { file.seekg(0, std::ios::end); }

private:
    std::ifstream file;
    bool swapDataBytes;

    bool read(void* ptr, size_t bytes) {
        return (bool)file.read((char*)ptr, bytes);
    }

    uint16_t swap16(uint16_t v) {
        return swapDataBytes ? ((v >> 8) | (v << 8)) : v;
    }

    uint32_t swap32(uint32_t v) {
        if (!swapDataBytes) return v;
        return ((v & 0xff) << 24) | ((v & 0xff00) << 8) | ((v & 0xff0000) >> 8) | ((v >> 24) & 0xff);
    }

    uint64_t swap64(uint64_t v) {
        if (!swapDataBytes) return v;
        return ((uint64_t)swap32(v & 0xffffffff) << 32) | swap32((v >> 32) & 0xffffffff);
    }
};

#endif //NDEVC_NDEVCBINARYREADER_H