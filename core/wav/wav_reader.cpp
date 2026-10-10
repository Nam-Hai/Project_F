#include "wav_reader.h"

#include <cmath>
#include <cstring>

namespace wav {

namespace {

constexpr uint32_t CHUNK_BYTES = 4096;  // read from the file at a time (a multiple of every frame size: 2, 4 and 8 bytes)

}  // namespace

int16_t float_bits_to_int16(uint32_t bits) {
    float v;
    std::memcpy(&v, &bits, sizeof v);
    if (!(v == v)) return 0;
    const float scaled = v * 32767.0f;
    if (scaled >= 32767.0f) return 32767;
    if (scaled <= -32768.0f) return -32768;
    return static_cast<int16_t>(std::lround(scaled));
}

SampleReader::SampleReader(FileReader& file, const Info& info) : file_(file), info_(info), next_(0), left_(0), failed_(false) {
    const bool usable = (info.format == Format::Pcm16 || info.format == Format::Float32) && (info.channels == 1 || info.channels == 2);
    if (usable) left_ = info.frames;
    else failed_ = true;
}

uint32_t SampleReader::read(int16_t* dst, uint32_t max_frames) {
    if (failed_) return 0;

    const bool     pcm16      = info_.format == Format::Pcm16;
    const uint32_t width      = pcm16 ? 2u : 4u;
    const uint32_t frame_size = width * info_.channels;
    const uint32_t per_chunk  = CHUNK_BYTES / frame_size;

    uint8_t  buf[CHUNK_BYTES];
    uint32_t done = 0;  // frames written by this call

    uint32_t want_total = max_frames < left_ ? max_frames : left_;
    while (done < want_total) {
        const uint32_t frames = (want_total - done) < per_chunk ? (want_total - done) : per_chunk;
        const uint32_t bytes  = frames * frame_size;
        const size_t   got    = file_.read_at(info_.data_offset + (next_ + done) * frame_size, buf, bytes);

        // Only whole frames are delivered; a short read means the file stopped answering.
        const uint32_t good = static_cast<uint32_t>(got) / frame_size;
        const uint32_t n    = good * info_.channels;  // samples to convert
        for (uint32_t i = 0; i < n; ++i) {
            if (pcm16) {
                dst[done * info_.channels + i] = static_cast<int16_t>(static_cast<uint16_t>(buf[2 * i] | (buf[2 * i + 1] << 8)));
            } else {
                const uint32_t bits = static_cast<uint32_t>(buf[4 * i]) | (static_cast<uint32_t>(buf[4 * i + 1]) << 8) |
                                      (static_cast<uint32_t>(buf[4 * i + 2]) << 16) | (static_cast<uint32_t>(buf[4 * i + 3]) << 24);
                dst[done * info_.channels + i] = float_bits_to_int16(bits);
            }
        }
        done += good;
        if (got < bytes) {
            failed_ = true;
            break;
        }
    }

    next_ += done;
    left_ -= done;
    return done;
}

}  // namespace wav
