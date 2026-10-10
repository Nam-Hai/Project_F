#include "binary_codec.h"

#include <cmath>
#include <cstring>

#include "crc32.h"

namespace profile {

namespace {

constexpr char    MAGIC[4]    = {'D', 'M', 'P', 'B'};
constexpr uint8_t DEFAULT_VEL = static_cast<uint8_t>(cfg::VELOCITY_MAX);

bool in_range(int v, int lo, int hi) { return v >= lo && v <= hi; }

bool valid_hit(int offset, int velocity, int pitch, int pan) {
    return in_range(offset, cfg::OFFSET_MIN, cfg::OFFSET_MAX) && in_range(velocity, cfg::VELOCITY_MIN, cfg::VELOCITY_MAX) &&
           in_range(pitch, cfg::STEP_PITCH_MIN, cfg::STEP_PITCH_MAX) && in_range(pan, cfg::PAN_MIN, cfg::PAN_MAX);
}

// A name: no control characters (below 0x20 or 0x7F). Bytes of UTF-8 sequences are accepted.
bool valid_text(const uint8_t* s, std::size_t len) {
    for (std::size_t i = 0; i < len; ++i)
        if (s[i] < 0x20 || s[i] == 0x7F) return false;
    return true;
}

// Length of a zero-terminated name stored in a char array of `capacity` bytes, or -1 if it is not terminated.
int text_length(const char* s, int capacity) {
    for (int i = 0; i < capacity; ++i)
        if (s[i] == '\0') return i;
    return -1;
}

bool valid_name(const char* s, int capacity) {
    const int len = text_length(s, capacity);
    return len >= 0 && valid_text(reinterpret_cast<const uint8_t*>(s), static_cast<std::size_t>(len));
}

bool valid_track(const TrackData& t, int sample_count) {
    if (t.sample >= sample_count || !std::isfinite(t.gain) || !std::isfinite(t.pitch)) return false;
    if (t.quantize > cfg::QUANTIZE_MAX || !in_range(t.width, cfg::WIDTH_MIN, cfg::WIDTH_MAX) ||
        !in_range(t.balance, cfg::PAN_MIN, cfg::PAN_MAX))
        return false;
    if (t.attack > cfg::ENV_MS_MAX || t.decay > cfg::ENV_MS_MAX || t.release > cfg::ENV_MS_MAX || t.sustain > 100) return false;
    for (int s = 0; s < cfg::MAX_STEPS; ++s) {
        if (!in_range(t.step_velocity[s], cfg::VELOCITY_MIN, cfg::VELOCITY_MAX) ||
            !in_range(t.step_pitch[s], cfg::STEP_PITCH_MIN, cfg::STEP_PITCH_MAX) || !in_range(t.step_pan[s], cfg::PAN_MIN, cfg::PAN_MAX) ||
            !in_range(t.step_offset[s], cfg::OFFSET_MIN, cfg::OFFSET_MAX))
            return false;
        if (t.extra_count[s] > cfg::MAX_EXTRA_HITS) return false;
        for (int h = 0; h < t.extra_count[s]; ++h) {
            const Hit& hit = t.extra[s][h];
            if (!valid_hit(hit.offset, hit.velocity, hit.pitch, hit.pan)) return false;
        }
    }
    return true;
}

// ============================================================================================
// Writing
// ============================================================================================
class Writer {
public:
    explicit Writer(ByteSink& sink) : sink_(sink) {}

    void put(const uint8_t* data, std::size_t length, bool in_crc = true) {
        if (in_crc) crc_.update(data, length);
        while (length > 0 && ok_) {
            const std::size_t room = sizeof buffer_ - used_;
            const std::size_t n    = length < room ? length : room;
            std::memcpy(buffer_ + used_, data, n);
            used_ += n;
            data += n;
            length -= n;
            if (used_ == sizeof buffer_) flush();
        }
    }

    void record(uint8_t tag, const uint8_t* payload, std::size_t length) {
        const uint8_t head[2] = {tag, static_cast<uint8_t>(length)};
        put(head, 2);
        put(payload, length);
    }

    void record_byte(uint8_t tag, uint8_t v) { record(tag, &v, 1); }
    void record_u16(uint8_t tag, uint16_t v) {
        const uint8_t b[2] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8)};
        record(tag, b, 2);
    }
    void record_f32(uint8_t tag, float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        const uint8_t b[4] = {static_cast<uint8_t>(bits), static_cast<uint8_t>(bits >> 8), static_cast<uint8_t>(bits >> 16),
                              static_cast<uint8_t>(bits >> 24)};
        record(tag, b, 4);
    }
    void record_text(uint8_t tag, const char* s) {
        record(tag, reinterpret_cast<const uint8_t*>(s), std::strlen(s));
    }

    // END record: its tag and length are covered by the CRC, the 4 CRC bytes are not.
    bool finish() {
        const uint8_t head[2] = {tag::END, 4};
        put(head, 2);
        const uint32_t c    = crc_.value();
        const uint8_t  b[4] = {static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c >> 16),
                               static_cast<uint8_t>(c >> 24)};
        put(b, 4, false);
        flush();
        return ok_;
    }

private:
    void flush() {
        if (ok_ && used_ > 0) ok_ = sink_.write(buffer_, used_);
        used_ = 0;
    }

    ByteSink&   sink_;
    Crc32       crc_;
    uint8_t     buffer_[128];
    std::size_t used_ = 0;
    bool        ok_   = true;
};

void write_list(Writer& w, uint8_t tag_id, const int8_t* values) {
    w.record(tag_id, reinterpret_cast<const uint8_t*>(values), cfg::MAX_STEPS);
}

// ============================================================================================
// Reading
// ============================================================================================
class Reader {
public:
    explicit Reader(ByteSource& source) : source_(source) {}

    // Reads exactly `length` bytes (into `dst`, or dropped if null). false if the stream ends first.
    bool get(uint8_t* dst, std::size_t length, bool in_crc = true) {
        while (length > 0) {
            if (pos_ == size_) {
                size_ = source_.read(buffer_, sizeof buffer_);
                pos_  = 0;
                if (size_ == 0) return false;
            }
            const std::size_t avail = size_ - pos_;
            const std::size_t n     = length < avail ? length : avail;
            if (in_crc) crc_.update(buffer_ + pos_, n);
            if (dst) {
                std::memcpy(dst, buffer_ + pos_, n);
                dst += n;
            }
            pos_ += n;
            length -= n;
        }
        return true;
    }

    bool at_end() {
        if (pos_ < size_) return false;
        size_ = source_.read(buffer_, sizeof buffer_);
        pos_  = 0;
        return size_ == 0;
    }

    uint32_t crc() const { return crc_.value(); }

private:
    ByteSource& source_;
    Crc32       crc_;
    uint8_t     buffer_[128];
    std::size_t size_ = 0;
    std::size_t pos_  = 0;
};

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

float f32(const uint8_t* p) {
    const uint32_t bits = u32(p);
    float          v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}

// Reads a list of 16 values of the track, each within [lo, hi].
bool read_list(const uint8_t* p, int8_t* dst, int lo, int hi) {
    for (int s = 0; s < cfg::MAX_STEPS; ++s)
        if (!in_range(static_cast<int8_t>(p[s]), lo, hi)) return false;
    for (int s = 0; s < cfg::MAX_STEPS; ++s) dst[s] = static_cast<int8_t>(p[s]);
    return true;
}

}  // namespace

// ============================================================================================
// Public interface
// ============================================================================================
bool BinaryCodec::valid(const ProfileData& p) {
    if (!valid_name(p.name, PROFILE_NAME_LEN + 1)) return false;
    if (p.sample_count > MAX_SAMPLES || p.loop_count > MAX_LOOPS) return false;
    for (int i = 0; i < p.sample_count; ++i)
        if (!valid_name(p.samples[i], SAMPLE_NAME_LEN + 1)) return false;
    for (int l = 0; l < p.loop_count; ++l) {
        const LoopData& loop = p.loops[l];
        if (!std::isfinite(loop.bpm) || loop.track_count > cfg::MAX_TRACKS) return false;
        for (int t = 0; t < loop.track_count; ++t)
            if (!valid_track(loop.tracks[t], p.sample_count)) return false;
    }
    return true;
}

bool BinaryCodec::write(const ProfileData& p, ByteSink& sink) {
    if (!valid(p)) return false;

    Writer w(sink);
    w.put(reinterpret_cast<const uint8_t*>(MAGIC), sizeof MAGIC);
    w.put(&BINARY_FORMAT_VERSION, 1);

    w.record_text(tag::PROFILE_NAME, p.name);
    for (int i = 0; i < p.sample_count; ++i) w.record_text(tag::SAMPLE, p.samples[i]);

    for (int l = 0; l < p.loop_count; ++l) {
        const LoopData& loop = p.loops[l];
        w.record_f32(tag::LOOP, loop.bpm);

        for (int t = 0; t < loop.track_count; ++t) {
            const TrackData& tr = loop.tracks[t];
            w.record_byte(tag::TRACK, tr.sample);
            if (tr.gain != 1.0f) w.record_f32(tag::GAIN, tr.gain);
            if (tr.pitch != 1.0f) w.record_f32(tag::PITCH, tr.pitch);
            if (tr.pattern != 0) w.record_u16(tag::PATTERN, tr.pattern);

            bool custom_velocity = false, custom_pitch = false, custom_pan = false, custom_offset = false;
            for (int s = 0; s < cfg::MAX_STEPS; ++s) {
                custom_velocity = custom_velocity || tr.step_velocity[s] != DEFAULT_VEL;
                custom_pitch    = custom_pitch || tr.step_pitch[s] != 0;
                custom_pan      = custom_pan || tr.step_pan[s] != 0;
                custom_offset   = custom_offset || tr.step_offset[s] != 0;
            }
            if (custom_velocity) w.record(tag::VELOCITY, tr.step_velocity, cfg::MAX_STEPS);
            if (custom_pitch) write_list(w, tag::STEP_PITCH, tr.step_pitch);
            if (custom_pan) write_list(w, tag::STEP_PAN, tr.step_pan);
            if (custom_offset) write_list(w, tag::STEP_OFFSET, tr.step_offset);

            for (int s = 0; s < cfg::MAX_STEPS; ++s) {
                for (int h = 0; h < tr.extra_count[s]; ++h) {
                    const Hit&    hit = tr.extra[s][h];
                    const uint8_t b[5] = {static_cast<uint8_t>(s), static_cast<uint8_t>(hit.offset), hit.velocity,
                                          static_cast<uint8_t>(hit.pitch), static_cast<uint8_t>(hit.pan)};
                    w.record(tag::EXTRA_HIT, b, 5);
                }
            }

            if (tr.quantize != 0) w.record_byte(tag::QUANTIZE, tr.quantize);
            if (tr.width != 0) w.record_byte(tag::WIDTH, static_cast<uint8_t>(tr.width));
            if (tr.balance != 0) w.record_byte(tag::BALANCE, static_cast<uint8_t>(tr.balance));
            if (tr.attack != 0) w.record_u16(tag::ATTACK, tr.attack);
            if (tr.decay != 0) w.record_u16(tag::DECAY, tr.decay);
            if (tr.sustain != 100) w.record_byte(tag::SUSTAIN, tr.sustain);
            if (tr.release != cfg::ENV_DEFAULT_RELEASE_MS) w.record_u16(tag::RELEASE, tr.release);
        }
    }
    return w.finish();
}

ProfileStatus BinaryCodec::read(ByteSource& source, ProfileData& out) {
    // In-place reset (a temporary ProfileData would weigh tens of kilobytes of stack).
    std::strcpy(out.name, "Default");
    out.sample_count = 0;
    out.loop_count   = 0;

    Reader r(source);

    // Header. A file that does not even start like ours is "not a profile" rather than "truncated".
    uint8_t head[5];
    for (int i = 0; i < 4; ++i) {
        if (!r.get(&head[i], 1)) return ProfileStatus::Truncated;
        if (head[i] != static_cast<uint8_t>(MAGIC[i])) return ProfileStatus::NotAProfile;
    }
    if (!r.get(&head[4], 1)) return ProfileStatus::Truncated;
    if (head[4] == 0) return ProfileStatus::BadField;
    if (head[4] > BINARY_FORMAT_VERSION) return ProfileStatus::TooNew;

    int loop  = -1;  // loop being filled (-1: none yet)
    int track = -1;  // track being filled in that loop (-1: none yet)

    for (;;) {
        uint8_t tg, len;
        if (!r.get(&tg, 1) || !r.get(&len, 1)) return ProfileStatus::Truncated;

        if (tg == tag::END) {
            if (len < 4) return ProfileStatus::BadField;
            const uint32_t computed = r.crc();
            uint8_t        stored[4];
            if (!r.get(stored, 4, false) || !r.get(nullptr, static_cast<std::size_t>(len) - 4, false)) return ProfileStatus::Truncated;
            if (u32(stored) != computed) return ProfileStatus::BadChecksum;
            return r.at_end() ? ProfileStatus::Ok : ProfileStatus::BadField;  // nothing may follow the END record
        }

        uint8_t p[255];
        if (!r.get(p, len)) return ProfileStatus::Truncated;

        // The payload must hold at least `n` bytes (longer is accepted: later versions may append fields).
        auto need = [&](int n) { return len >= n; };

        if (tg == tag::PROFILE_NAME) {
            if (loop >= 0 || len > PROFILE_NAME_LEN || !valid_text(p, len)) return ProfileStatus::BadField;
            std::memcpy(out.name, p, len);
            out.name[len] = '\0';
        } else if (tg == tag::SAMPLE) {
            if (loop >= 0 || len > SAMPLE_NAME_LEN || !valid_text(p, len)) return ProfileStatus::BadField;
            if (out.sample_count >= MAX_SAMPLES) return ProfileStatus::TooBig;
            std::memcpy(out.samples[out.sample_count], p, len);
            out.samples[out.sample_count][len] = '\0';
            ++out.sample_count;
        } else if (tg == tag::LOOP) {
            if (!need(4) || !std::isfinite(f32(p))) return ProfileStatus::BadField;
            if (out.loop_count >= MAX_LOOPS) return ProfileStatus::TooBig;
            loop        = out.loop_count++;
            out.loops[loop] = LoopData{};
            out.loops[loop].bpm = f32(p);
            track       = -1;
        } else if (tg == tag::TRACK) {
            if (loop < 0 || !need(1) || p[0] >= out.sample_count) return ProfileStatus::BadField;
            LoopData& lp = out.loops[loop];
            if (lp.track_count >= cfg::MAX_TRACKS) return ProfileStatus::TooBig;
            track                = lp.track_count++;  // the track is still at its defaults: the whole loop was reset by LOOP
            lp.tracks[track].sample = p[0];
        } else if (tg >= tag::GAIN && tg <= tag::RELEASE) {
            if (track < 0) return ProfileStatus::BadField;  // a track field with no track to belong to
            TrackData& tr = out.loops[loop].tracks[track];
            bool       ok = true;
            switch (tg) {
                case tag::GAIN:  ok = need(4) && std::isfinite(f32(p)); if (ok) tr.gain = f32(p); break;
                case tag::PITCH: ok = need(4) && std::isfinite(f32(p)); if (ok) tr.pitch = f32(p); break;
                case tag::PATTERN: ok = need(2); if (ok) tr.pattern = u16(p); break;
                case tag::VELOCITY: {
                    ok = need(cfg::MAX_STEPS);
                    for (int s = 0; ok && s < cfg::MAX_STEPS; ++s) ok = in_range(p[s], cfg::VELOCITY_MIN, cfg::VELOCITY_MAX);
                    if (ok) std::memcpy(tr.step_velocity, p, cfg::MAX_STEPS);
                    break;
                }
                case tag::STEP_PITCH:  ok = need(cfg::MAX_STEPS) && read_list(p, tr.step_pitch, cfg::STEP_PITCH_MIN, cfg::STEP_PITCH_MAX); break;
                case tag::STEP_PAN:    ok = need(cfg::MAX_STEPS) && read_list(p, tr.step_pan, cfg::PAN_MIN, cfg::PAN_MAX); break;
                case tag::STEP_OFFSET: ok = need(cfg::MAX_STEPS) && read_list(p, tr.step_offset, cfg::OFFSET_MIN, cfg::OFFSET_MAX); break;
                case tag::EXTRA_HIT: {
                    ok = need(5) && p[0] < cfg::MAX_STEPS && tr.extra_count[p[0]] < cfg::MAX_EXTRA_HITS &&
                         valid_hit(static_cast<int8_t>(p[1]), p[2], static_cast<int8_t>(p[3]), static_cast<int8_t>(p[4]));
                    if (ok) {
                        Hit& h     = tr.extra[p[0]][tr.extra_count[p[0]]++];
                        h.offset   = static_cast<int8_t>(p[1]);
                        h.velocity = p[2];
                        h.pitch    = static_cast<int8_t>(p[3]);
                        h.pan      = static_cast<int8_t>(p[4]);
                    }
                    break;
                }
                case tag::QUANTIZE: ok = need(1) && p[0] <= cfg::QUANTIZE_MAX; if (ok) tr.quantize = p[0]; break;
                case tag::WIDTH:    ok = need(1) && in_range(static_cast<int8_t>(p[0]), cfg::WIDTH_MIN, cfg::WIDTH_MAX); if (ok) tr.width = static_cast<int8_t>(p[0]); break;
                case tag::BALANCE:  ok = need(1) && in_range(static_cast<int8_t>(p[0]), cfg::PAN_MIN, cfg::PAN_MAX); if (ok) tr.balance = static_cast<int8_t>(p[0]); break;
                case tag::ATTACK:   ok = need(2) && u16(p) <= cfg::ENV_MS_MAX; if (ok) tr.attack = u16(p); break;
                case tag::DECAY:    ok = need(2) && u16(p) <= cfg::ENV_MS_MAX; if (ok) tr.decay = u16(p); break;
                case tag::SUSTAIN:  ok = need(1) && p[0] <= 100; if (ok) tr.sustain = p[0]; break;
                case tag::RELEASE:  ok = need(2) && u16(p) <= cfg::ENV_MS_MAX; if (ok) tr.release = u16(p); break;
                default: break;  // a tag in the track range that this version does not know: skipped
            }
            if (!ok) return ProfileStatus::BadField;
        }
        // Any other tag is unknown to this version: its payload has already been consumed, so it is skipped.
    }
}

}  // namespace profile
