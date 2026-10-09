#include "wav_writer.h"

#include <cstdint>
#include <cstdio>
#include <vector>

static void put_u32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
static void put_u16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
static void put_tag(std::vector<uint8_t>& b, const char* tag) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(tag[i]));
}

static int16_t to_int16(float x) {
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    return static_cast<int16_t>(x * 32767.0f);
}

bool write_wav_stereo16(const char* path, const float* left, const float* right,
                        std::size_t frames, int sample_rate) {
    const uint16_t channels   = 2;
    const uint16_t bits       = 16;
    const uint32_t data_bytes = static_cast<uint32_t>(frames * channels * (bits / 8));

    std::vector<uint8_t> b;
    b.reserve(44 + data_bytes);

    put_tag(b, "RIFF");
    put_u32(b, 36 + data_bytes);
    put_tag(b, "WAVE");
    put_tag(b, "fmt ");
    put_u32(b, 16);
    put_u16(b, 1);  // PCM
    put_u16(b, channels);
    put_u32(b, static_cast<uint32_t>(sample_rate));
    put_u32(b, static_cast<uint32_t>(sample_rate) * channels * (bits / 8));
    put_u16(b, channels * (bits / 8));
    put_u16(b, bits);
    put_tag(b, "data");
    put_u32(b, data_bytes);

    for (std::size_t i = 0; i < frames; ++i) {
        put_u16(b, static_cast<uint16_t>(to_int16(left[i])));
        put_u16(b, static_cast<uint16_t>(to_int16(right[i])));
    }

    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
    std::fclose(f);
    return ok;
}
