#pragma once

#include <cstdint>
#include <cstdio>

#include "../../core/wav/wav_header.h"

// A file on the disk read by position (wav::FileReader over stdio). Closed by the destructor.
class StdioFile : public wav::FileReader {
public:
    explicit StdioFile(const char* path) : file_(std::fopen(path, "rb")) {
        if (!file_) return;
        if (std::fseek(file_, 0, SEEK_END) == 0) {
            const long end = std::ftell(file_);
            if (end > 0) size_ = end > static_cast<long>(UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(end);  // RIFF cannot go beyond 4 GB
        }
    }

    ~StdioFile() override {
        if (file_) std::fclose(file_);
    }

    StdioFile(const StdioFile&)            = delete;
    StdioFile& operator=(const StdioFile&) = delete;

    bool is_open() const { return file_ != nullptr; }

    uint32_t size() const override { return size_; }

    std::size_t read_at(uint32_t offset, uint8_t* dst, std::size_t length) override {
        if (!file_ || std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) return 0;
        return std::fread(dst, 1, length, file_);
    }

private:
    std::FILE* file_ = nullptr;
    uint32_t   size_ = 0;
};
