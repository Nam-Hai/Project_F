#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>

#include "../../core/profile/byte_stream.h"

// Adapters between the byte streams of the profile codec and std::istream / std::ostream (files on the PC).
class FileSource : public profile::ByteSource {
public:
    explicit FileSource(std::istream& in) : in_(in) {}

    std::size_t read(uint8_t* dst, std::size_t capacity) override {
        in_.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(capacity));
        return static_cast<std::size_t>(in_.gcount());
    }

private:
    std::istream& in_;
};

class FileSink : public profile::ByteSink {
public:
    explicit FileSink(std::ostream& out) : out_(out) {}

    bool write(const uint8_t* src, std::size_t length) override {
        out_.write(reinterpret_cast<const char*>(src), static_cast<std::streamsize>(length));
        return static_cast<bool>(out_);
    }

private:
    std::ostream& out_;
};
