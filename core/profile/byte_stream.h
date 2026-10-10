#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace profile {

// Streaming byte input / output for the binary profile codec: the codec never needs the whole file in memory.
// On the PC they wrap a file, on the Teensy the SD card. Fixed-size, no allocation.
class ByteSource {
public:
    virtual ~ByteSource() = default;

    // Reads up to `capacity` bytes into `dst`. Returns the number of bytes read, 0 at the end of the stream.
    virtual std::size_t read(uint8_t* dst, std::size_t capacity) = 0;
};

class ByteSink {
public:
    virtual ~ByteSink() = default;

    // Writes all `length` bytes. false on failure (card full, I/O error).
    virtual bool write(const uint8_t* src, std::size_t length) = 0;
};

// Memory-backed implementations (fixed buffer), for tests and tools.
class MemorySource : public ByteSource {
public:
    MemorySource(const uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    std::size_t read(uint8_t* dst, std::size_t capacity) override {
        const std::size_t n = capacity < size_ - pos_ ? capacity : size_ - pos_;
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
        return n;
    }

private:
    const uint8_t* data_;
    std::size_t    size_;
    std::size_t    pos_ = 0;
};

class MemorySink : public ByteSink {
public:
    MemorySink(uint8_t* buffer, std::size_t capacity) : buffer_(buffer), capacity_(capacity) {}

    bool write(const uint8_t* src, std::size_t length) override {
        if (length > capacity_ - size_) return false;  // nothing partial: the write is refused as a whole
        std::memcpy(buffer_ + size_, src, length);
        size_ += length;
        return true;
    }

    std::size_t size() const { return size_; }

private:
    uint8_t*    buffer_;
    std::size_t capacity_;
    std::size_t size_ = 0;
};

}  // namespace profile
