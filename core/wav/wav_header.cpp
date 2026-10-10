#include "wav_header.h"

#include "../../config.h"

namespace wav {

namespace {

constexpr int MAX_CHUNKS = 64;  // chunks walked at most: a crafted file cannot keep us busy

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

Status read_header(FileReader& file, Info& info) {
    info = Info{};

    uint8_t head[12] = {};
    if (file.read_at(0, head, sizeof head) < sizeof head || std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVE", 4) != 0)
        return Status::NotWav;

    const uint64_t file_size = file.size();
    bool           have_fmt  = false;

    // The chunks are not at fixed positions (LIST, etc.): walk them, jumping from one to the next.
    uint64_t pos = 12;
    for (int n = 0; n < MAX_CHUNKS && pos + 8 <= file_size; ++n) {
        uint8_t chunk[8];
        if (file.read_at(static_cast<uint32_t>(pos), chunk, sizeof chunk) < sizeof chunk) break;

        const uint32_t size  = rd32(chunk + 4);
        const uint64_t body  = pos + 8;
        const uint64_t avail = file_size - body;

        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16 && avail >= 16) {
            uint8_t       fmt[26] = {};
            const size_t  want = (size >= 26 && avail >= 26) ? 26 : 16;
            if (file.read_at(static_cast<uint32_t>(body), fmt, want) < want) break;
            info.tag         = rd16(fmt);
            info.channels    = rd16(fmt + 2);
            info.sample_rate = rd32(fmt + 4);
            info.bits        = rd16(fmt + 14);
            // WAVE_FORMAT_EXTENSIBLE: the real format is in the sub-format.
            if (info.tag == 0xFFFE && want == 26) info.tag = rd16(fmt + 24);
            have_fmt = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            if (!have_fmt) return Status::DataBeforeFmt;

            const bool pcm16 = info.tag == 1 && info.bits == 16;
            const bool f32   = info.tag == 3 && info.bits == 32;
            if ((!pcm16 && !f32) || (info.channels != 1 && info.channels != 2) || info.sample_rate != static_cast<uint32_t>(cfg::SAMPLE_RATE))
                return Status::Unsupported;

            const uint32_t frame_size = (pcm16 ? 2u : 4u) * info.channels;
            const uint64_t bytes      = size < avail ? size : avail;  // tolerates a cut file
            info.format      = pcm16 ? Format::Pcm16 : Format::Float32;
            info.data_offset = static_cast<uint32_t>(body);
            info.frames      = static_cast<uint32_t>(bytes / frame_size);
            info.data_bytes  = info.frames * frame_size;
            return Status::Ok;
        }

        pos = body + size + (size & 1u);  // chunks are aligned on 2 bytes
    }

    return have_fmt ? Status::NoData : Status::NoFmt;
}

}  // namespace wav
