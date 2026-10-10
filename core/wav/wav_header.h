#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Reading the header of a WAV file, without loading its sound data. Portable (no file API, no allocation): the
// platform supplies the file through the small FileReader interface (a stdio file on the PC, the SD card on the Teensy).
//
// Accepted files: RIFF/WAVE, PCM 16-bit or IEEE float 32-bit (also inside WAVE_FORMAT_EXTENSIBLE), 1 or 2 channels,
// cfg::SAMPLE_RATE Hz. Chunks that are not 'fmt ' or 'data' (LIST...) are skipped, with their padding byte. A file whose
// 'data' chunk is longer than the file itself (cut file, or a streaming writer that left the size at 0xFFFFFFFF) is
// accepted: only the whole frames that are really there are counted.
namespace wav {

// A file read by position. Implemented by the platform.
class FileReader {
public:
    virtual ~FileReader() = default;

    // Size of the file in bytes.
    virtual uint32_t size() const = 0;

    // Reads up to `length` bytes starting at byte `offset`; returns the number of bytes read (fewer than asked only at
    // the end of the file, or on an error).
    virtual std::size_t read_at(uint32_t offset, uint8_t* dst, std::size_t length) = 0;
};

// A file held in memory, for tests and tools.
class MemoryFile : public FileReader {
public:
    MemoryFile(const uint8_t* data, std::size_t size) : data_(data), size_(static_cast<uint32_t>(size)) {}

    uint32_t size() const override { return size_; }

    std::size_t read_at(uint32_t offset, uint8_t* dst, std::size_t length) override {
        if (offset >= size_) return 0;
        const std::size_t n = length < size_ - offset ? length : size_ - offset;
        std::memcpy(dst, data_ + offset, n);
        return n;
    }

private:
    const uint8_t* data_;
    uint32_t       size_;
};

enum class Format : uint8_t { Unknown, Pcm16, Float32 };

enum class Status : uint8_t {
    Ok,
    NotWav,         // RIFF/WAVE absent
    NoFmt,          // no usable 'fmt ' chunk
    DataBeforeFmt,  // a 'data' chunk before any 'fmt '
    NoData,         // no 'data' chunk
    Unsupported,    // format, channel count or sample rate not accepted (the fields of Info say which)
};

struct Info {
    // As declared by the 'fmt ' chunk (filled as soon as it has been read, also when the status is Unsupported).
    uint16_t tag         = 0;  // 1 = PCM, 3 = float, after resolving WAVE_FORMAT_EXTENSIBLE
    uint16_t bits        = 0;
    uint16_t channels    = 0;
    uint32_t sample_rate = 0;

    // Only when the status is Ok.
    Format   format      = Format::Unknown;
    uint32_t data_offset = 0;  // byte position of the first sample
    uint32_t frames      = 0;  // whole frames present in the file (one sample per channel each)
    uint32_t data_bytes  = 0;  // frames * frame size: the bytes to read from data_offset
};

// Reads the header. A few hundred bytes are read at most, whatever the size of the file.
Status read_header(FileReader& file, Info& info);

}  // namespace wav
