#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "../core/profile/binary_codec.h"
#include "../core/profile/crc32.h"
#include "../core/profile/profile_codec.h"

using namespace profile;

static int failures = 0;

#define CHECK(cond, msg)                    \
    do {                                    \
        if (!(cond)) {                      \
            std::printf("FAIL: %s\n", msg); \
            ++failures;                     \
        }                                   \
    } while (0)

// Big objects are static, like on the Teensy (never on the stack).
static ProfileData source;
static ProfileData parsed;
static ProfileData worst;
static uint8_t     big[160 * 1024];
static char        json[256 * 1024];
static ProfileCodec json_codec;

using Bytes = std::vector<uint8_t>;

// ----- Helpers -----
static bool same_track(const TrackData& a, const TrackData& b) {
    if (a.sample != b.sample || a.gain != b.gain || a.pitch != b.pitch || a.pattern != b.pattern || a.quantize != b.quantize ||
        a.width != b.width || a.balance != b.balance || a.attack != b.attack || a.decay != b.decay || a.sustain != b.sustain ||
        a.release != b.release)
        return false;
    for (int s = 0; s < cfg::MAX_STEPS; ++s) {
        if (a.step_velocity[s] != b.step_velocity[s] || a.step_pitch[s] != b.step_pitch[s] || a.step_pan[s] != b.step_pan[s] ||
            a.step_offset[s] != b.step_offset[s] || a.extra_count[s] != b.extra_count[s])
            return false;
        for (int h = 0; h < a.extra_count[s]; ++h) {
            const Hit &x = a.extra[s][h], &y = b.extra[s][h];
            if (x.offset != y.offset || x.velocity != y.velocity || x.pitch != y.pitch || x.pan != y.pan) return false;
        }
    }
    return true;
}

static bool same(const ProfileData& a, const ProfileData& b) {
    if (std::strcmp(a.name, b.name) != 0 || a.sample_count != b.sample_count || a.loop_count != b.loop_count) return false;
    for (int i = 0; i < a.sample_count; ++i)
        if (std::strcmp(a.samples[i], b.samples[i]) != 0) return false;
    for (int l = 0; l < a.loop_count; ++l) {
        if (a.loops[l].bpm != b.loops[l].bpm || a.loops[l].track_count != b.loops[l].track_count) return false;
        for (int t = 0; t < a.loops[l].track_count; ++t)
            if (!same_track(a.loops[l].tracks[t], b.loops[l].tracks[t])) return false;
    }
    return true;
}

static Bytes encode(const ProfileData& p) {
    MemorySink sink(big, sizeof big);
    if (!BinaryCodec::write(p, sink)) return {};
    return Bytes(big, big + sink.size());
}

static ProfileStatus decode(const Bytes& b, ProfileData& out) {
    MemorySource src(b.data(), b.size());
    return BinaryCodec::read(src, out);
}

// Source that hands out at most `chunk` bytes per read: exercises the buffer boundaries of the reader.
class ChunkSource : public ByteSource {
public:
    ChunkSource(const Bytes& b, std::size_t chunk) : b_(b), chunk_(chunk) {}
    std::size_t read(uint8_t* dst, std::size_t capacity) override {
        std::size_t n = capacity < chunk_ ? capacity : chunk_;
        if (n > b_.size() - pos_) n = b_.size() - pos_;
        std::memcpy(dst, b_.data() + pos_, n);
        pos_ += n;
        return n;
    }

private:
    const Bytes& b_;
    std::size_t  chunk_;
    std::size_t  pos_ = 0;
};

// Deterministic pseudo-random numbers.
static uint32_t rng_state = 12345;
static uint32_t rnd() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}
static int rnd_range(int lo, int hi) { return lo + static_cast<int>(rnd() % static_cast<uint32_t>(hi - lo + 1)); }

// A track with every field away from its default value.
static void fill_track(TrackData& t, int sample, bool extras) {
    t         = TrackData{};
    t.sample  = static_cast<uint8_t>(sample);
    t.gain    = 0.25f + static_cast<float>(rnd_range(0, 300)) / 100.0f;
    t.pitch   = 0.5f + static_cast<float>(rnd_range(0, 150)) / 100.0f;
    t.pattern = static_cast<uint16_t>(rnd_range(1, 65535));
    for (int s = 0; s < cfg::MAX_STEPS; ++s) {
        t.step_velocity[s] = static_cast<uint8_t>(rnd_range(cfg::VELOCITY_MIN, cfg::VELOCITY_MAX - 1));
        t.step_pitch[s]    = static_cast<int8_t>(rnd_range(cfg::STEP_PITCH_MIN, cfg::STEP_PITCH_MAX));
        t.step_pan[s]      = static_cast<int8_t>(rnd_range(cfg::PAN_MIN, cfg::PAN_MAX));
        t.step_offset[s]   = static_cast<int8_t>(rnd_range(cfg::OFFSET_MIN, cfg::OFFSET_MAX));
        t.extra_count[s]   = extras ? static_cast<uint8_t>(cfg::MAX_EXTRA_HITS) : static_cast<uint8_t>(rnd_range(0, 2));
        for (int h = 0; h < t.extra_count[s]; ++h) {
            Hit& hit     = t.extra[s][h];
            hit.offset   = static_cast<int8_t>(rnd_range(cfg::OFFSET_MIN, cfg::OFFSET_MAX));
            hit.velocity = static_cast<uint8_t>(rnd_range(cfg::VELOCITY_MIN, cfg::VELOCITY_MAX));
            hit.pitch    = static_cast<int8_t>(rnd_range(cfg::STEP_PITCH_MIN, cfg::STEP_PITCH_MAX));
            hit.pan      = static_cast<int8_t>(rnd_range(cfg::PAN_MIN, cfg::PAN_MAX));
        }
    }
    t.quantize = static_cast<uint8_t>(rnd_range(1, cfg::QUANTIZE_MAX));
    t.width    = static_cast<int8_t>(rnd_range(1, cfg::WIDTH_MAX));
    t.balance  = static_cast<int8_t>(rnd_range(cfg::PAN_MIN, -1));
    t.attack   = static_cast<uint16_t>(rnd_range(1, cfg::ENV_MS_MAX));
    t.decay    = static_cast<uint16_t>(rnd_range(1, cfg::ENV_MS_MAX));
    t.sustain  = static_cast<uint8_t>(rnd_range(0, 99));
    t.release  = static_cast<uint16_t>(rnd_range(cfg::ENV_DEFAULT_RELEASE_MS + 1, cfg::ENV_MS_MAX));
}

static void fill_profile(ProfileData& p, int loops, int tracks, int samples, bool extras) {
    p = ProfileData{};
    std::strcpy(p.name, "Test profile");
    for (int i = 0; i < samples; ++i) {
        char name[SAMPLE_NAME_LEN + 1];
        std::snprintf(name, sizeof name, "Sample %d \xC3\xA9.wav", i);  // with a UTF-8 character
        p.add_sample(name);
    }
    p.loop_count = static_cast<uint8_t>(loops);
    for (int l = 0; l < loops; ++l) {
        p.loops[l].bpm         = 80.0f + static_cast<float>(l) * 7.5f;
        p.loops[l].track_count = static_cast<uint8_t>(tracks);
        for (int t = 0; t < tracks; ++t) fill_track(p.loops[l].tracks[t], (l + t) % samples, extras);
    }
}

// Builds a file by hand, with a valid CRC (or not), to test what the writer never produces.
class FileBuilder {
public:
    FileBuilder() {
        bytes_ = {'D', 'M', 'P', 'B', 1};
    }
    FileBuilder& version(uint8_t v) {
        bytes_[4] = v;
        return *this;
    }
    FileBuilder& rec(uint8_t tag_id, const Bytes& payload) {
        bytes_.push_back(tag_id);
        bytes_.push_back(static_cast<uint8_t>(payload.size()));
        bytes_.insert(bytes_.end(), payload.begin(), payload.end());
        return *this;
    }
    FileBuilder& rec(uint8_t tag_id, const char* text) { return rec(tag_id, Bytes(text, text + std::strlen(text))); }
    Bytes        finish(bool good_crc = true, const Bytes& after = {}, std::size_t end_extra = 0) const {
        Bytes out = bytes_;
        out.push_back(tag::END);
        out.push_back(static_cast<uint8_t>(4 + end_extra));
        Crc32 crc;
        crc.update(out.data(), out.size());
        uint32_t c = crc.value();
        if (!good_crc) c ^= 1u;
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(c >> (8 * i)));
        for (std::size_t i = 0; i < end_extra; ++i) out.push_back(0xEE);  // room for future fields, after the CRC
        out.insert(out.end(), after.begin(), after.end());
        return out;
    }

private:
    Bytes bytes_;
};

static Bytes f32(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    return {static_cast<uint8_t>(bits), static_cast<uint8_t>(bits >> 8), static_cast<uint8_t>(bits >> 16), static_cast<uint8_t>(bits >> 24)};
}

// File with one sample, one loop, one track; `track_fields` adds records after the TRACK record.
static Bytes simple_file(const std::vector<std::pair<uint8_t, Bytes>>& track_fields, bool good_crc = true) {
    FileBuilder b;
    b.rec(tag::PROFILE_NAME, "P").rec(tag::SAMPLE, "a.wav").rec(tag::LOOP, f32(100.0f)).rec(tag::TRACK, Bytes{0});
    for (const auto& f : track_fields) b.rec(f.first, f.second);
    return b.finish(good_crc);
}

static ProfileStatus status_of(const std::vector<std::pair<uint8_t, Bytes>>& track_fields) {
    return decode(simple_file(track_fields), parsed);
}

int main() {
    // ----- CRC-32 -----
    {
        Crc32 c;
        c.update(reinterpret_cast<const uint8_t*>("123456789"), 9);
        CHECK(c.value() == 0xCBF43926u, "CRC-32: standard check value of \"123456789\"");
        Crc32 d;
        d.update(reinterpret_cast<const uint8_t*>("1234"), 4);
        d.update(reinterpret_cast<const uint8_t*>("56789"), 5);
        CHECK(d.value() == 0xCBF43926u, "CRC-32: the same result when fed in pieces");
        Crc32 e;
        CHECK(e.value() == 0u, "CRC-32 of nothing is 0");

        const char* fox = "The quick brown fox jumps over the lazy dog";
        Crc32       f;
        f.update(reinterpret_cast<const uint8_t*>(fox), std::strlen(fox));
        CHECK(f.value() == 0x414FA339u, "CRC-32: second known value (the quick brown fox)");

        // Against a bitwise reference, on data that uses every table entry and every byte value.
        uint8_t all[512];
        for (int i = 0; i < 512; ++i) all[i] = static_cast<uint8_t>(i * 7 + (i >> 8));
        uint32_t ref = 0xFFFFFFFFu;
        for (int i = 0; i < 512; ++i) {
            ref ^= all[i];
            for (int k = 0; k < 8; ++k) ref = (ref & 1u) ? (ref >> 1) ^ 0xEDB88320u : ref >> 1;
        }
        Crc32 g;
        g.update(all, 512);
        CHECK(g.value() == ~ref, "CRC-32: same as a bitwise reference on 512 varied bytes");
    }

    // ----- Round trip -----
    {
        fill_profile(source, 3, 5, 4, false);
        const Bytes b = encode(source);
        CHECK(!b.empty() && std::memcmp(b.data(), "DMPB", 4) == 0 && b[4] == BINARY_FORMAT_VERSION, "header: magic and version");
        CHECK(decode(b, parsed) == ProfileStatus::Ok && same(source, parsed), "round trip: every field comes back");
        CHECK(encode(parsed) == b, "stable: reading then writing gives the same bytes");

        // Hand-picked values at the ends of their ranges.
        source = ProfileData{};
        source.add_sample("x.wav");
        source.loop_count         = 1;
        source.loops[0].bpm       = 300.0f;
        source.loops[0].track_count = 1;
        TrackData& t = source.loops[0].tracks[0];
        t.step_velocity[0] = 1;
        t.step_pitch[1]    = cfg::STEP_PITCH_MIN;
        t.step_pitch[2]    = cfg::STEP_PITCH_MAX;
        t.step_pan[3]      = cfg::PAN_MIN;
        t.step_pan[4]      = cfg::PAN_MAX;
        t.step_offset[5]   = cfg::OFFSET_MIN;
        t.step_offset[6]   = cfg::OFFSET_MAX;
        t.pattern          = 0x8001;
        t.width            = cfg::WIDTH_MIN;
        t.balance          = cfg::PAN_MAX;
        t.attack           = cfg::ENV_MS_MAX;
        t.sustain          = 0;
        t.release          = 0;
        t.quantize         = cfg::QUANTIZE_MAX;
        t.extra_count[15]  = 1;
        t.extra[15][0]     = Hit{cfg::OFFSET_MIN, cfg::VELOCITY_MIN, cfg::STEP_PITCH_MIN, cfg::PAN_MIN};
        CHECK(decode(encode(source), parsed) == ProfileStatus::Ok && same(source, parsed), "round trip: values at the ends of their ranges");
    }

    // ----- Defaults are not written -----
    {
        source = ProfileData{};
        Bytes b = encode(source);
        CHECK(b.size() == 5 + (2 + 7) + (2 + 4), "empty profile: header, name, END (nothing else)");
        CHECK(decode(b, parsed) == ProfileStatus::Ok && same(source, parsed), "empty profile: round trip");

        source.add_sample("a.wav");
        source.loop_count           = 1;
        source.loops[0].track_count = 1;
        b                           = encode(source);
        CHECK(b.size() == 5 + 9 + 7 + 6 + 3 + 6, "a default track is just a LOOP and a TRACK record");
        CHECK(decode(b, parsed) == ProfileStatus::Ok && same(source, parsed), "default track: round trip");
    }

    // ----- Worst case: 12 loops of 16 full tracks -----
    {
        fill_profile(worst, MAX_LOOPS, cfg::MAX_TRACKS, 40, true);
        MemorySink sink(big, sizeof big);
        CHECK(BinaryCodec::write(worst, sink), "worst case: written");
        std::printf("worst case binary profile: %zu bytes\n", sink.size());
        CHECK(sink.size() < 100000, "worst case: under 100 KB (the JSON is about 234 KB)");
        const Bytes b(big, big + sink.size());
        CHECK(decode(b, parsed) == ProfileStatus::Ok && same(worst, parsed), "worst case: round trip");

        // Reader buffer boundaries: data delivered in chunks of every awkward size.
        bool all_same = true;
        for (std::size_t chunk : {1u, 2u, 3u, 5u, 7u, 127u, 128u, 129u, 255u, 1000u}) {
            ChunkSource cs(b, chunk);
            all_same = all_same && BinaryCodec::read(cs, parsed) == ProfileStatus::Ok && same(worst, parsed);
        }
        CHECK(all_same, "reads identically whatever the chunk size of the source");
    }

    // ----- Compatibility with the JSON model: JSON -> data -> binary -> data -> JSON -----
    {
        fill_profile(source, 2, 4, 3, false);
        const int len = ProfileCodec::write(source, json, sizeof json);
        CHECK(len > 0, "JSON written");
        CHECK(json_codec.parse(json, static_cast<std::size_t>(len), parsed) == ProfileStatus::Ok, "JSON read");
        static ProfileData from_binary;
        CHECK(decode(encode(parsed), from_binary) == ProfileStatus::Ok && same(parsed, from_binary), "JSON -> binary -> data keeps everything");
        static char again[256 * 1024];
        CHECK(ProfileCodec::write(from_binary, again, sizeof again) == len && std::strcmp(again, json) == 0,
              "binary -> JSON gives the same JSON text");
    }

    // ----- Nothing is left from a previous profile -----
    {
        CHECK(decode(encode(worst), parsed) == ProfileStatus::Ok && parsed.loop_count == MAX_LOOPS, "a big profile is read first");
        CHECK(decode(simple_file({}), parsed) == ProfileStatus::Ok && parsed.sample_count == 1 && parsed.loop_count == 1 &&
                  parsed.loops[0].track_count == 1 && same_track(parsed.loops[0].tracks[0], TrackData{}),
              "reading a small profile over a big one: the track, the loop and the tables are fully reset");
        CHECK(std::strcmp(parsed.name, "P") == 0, "...and the name too");
        CHECK(decode(FileBuilder().finish(), parsed) == ProfileStatus::Ok && std::strcmp(parsed.name, "Default") == 0 && parsed.sample_count == 0 &&
                  parsed.loop_count == 0,
              "an empty file resets name and counts");
        // A second loop starts from defaults, whatever the first one held.
        CHECK(decode(FileBuilder().rec(tag::SAMPLE, "a").rec(tag::LOOP, f32(100)).rec(tag::TRACK, Bytes{0}).rec(tag::LOOP, f32(100)).finish(), parsed) == ProfileStatus::Ok &&
                  parsed.loops[1].track_count == 0,
              "a new loop starts with no track");
    }

    // ----- Header errors -----
    {
        CHECK(decode({}, parsed) == ProfileStatus::Truncated, "empty file: truncated");
        CHECK(decode({'D', 'M'}, parsed) == ProfileStatus::Truncated, "two good bytes only: truncated");
        CHECK(decode({'X', 'M', 'P', 'B', 1}, parsed) == ProfileStatus::NotAProfile, "wrong magic: not a profile");
        CHECK(decode({'{', '"', 'f'}, parsed) == ProfileStatus::NotAProfile, "a JSON file is not a binary profile");
        CHECK(decode({'D', 'M', 'P', 'B'}, parsed) == ProfileStatus::Truncated, "no version byte: truncated");
        CHECK(decode(FileBuilder().version(0).finish(), parsed) == ProfileStatus::BadField, "version 0: invalid");
        CHECK(decode(FileBuilder().version(2).finish(), parsed) == ProfileStatus::TooNew, "version 2: written by a newer program");
        CHECK(decode(FileBuilder().version(255).finish(), parsed) == ProfileStatus::TooNew, "version 255: too new");
        CHECK(decode(FileBuilder().finish(), parsed) == ProfileStatus::Ok && parsed.loop_count == 0 && std::strcmp(parsed.name, "Default") == 0,
              "no records at all: an empty profile with the default name");
    }

    // ----- Checksum and trailing data -----
    {
        CHECK(decode(simple_file({}, false), parsed) == ProfileStatus::BadChecksum, "wrong CRC");
        CHECK(decode(FileBuilder().rec(tag::SAMPLE, "a.wav").finish(true, Bytes{0}), parsed) == ProfileStatus::BadField, "a byte after END is refused");
        Bytes b = simple_file({});
        b.pop_back();
        CHECK(decode(b, parsed) == ProfileStatus::Truncated, "CRC cut short: truncated");
        b = simple_file({});
        b.resize(b.size() - 6);  // END tag, length and CRC all gone
        CHECK(decode(b, parsed) == ProfileStatus::Truncated, "no END record: truncated");
        // An END record with a longer payload (room for future fields): the CRC is the first 4 bytes.
        Bytes ok = FileBuilder().rec(tag::SAMPLE, "a.wav").finish();
        ok[ok.size() - 5] = 7;  // length 7 instead of 4: the CRC no longer matches the bytes it covers
        ok.insert(ok.end(), {9, 9, 9});
        CHECK(decode(ok, parsed) != ProfileStatus::Ok, "END with a changed length: refused (the CRC covers the length)");
        CHECK(decode(FileBuilder().rec(tag::SAMPLE, "a.wav").finish(true, {}, 2), parsed) == ProfileStatus::Ok && parsed.sample_count == 1,
              "END with 2 extra payload bytes after the CRC (a future version): accepted");
        CHECK(decode(FileBuilder().rec(tag::SAMPLE, "a.wav").finish(true, {}, 2), parsed) == ProfileStatus::Ok &&
                  decode(FileBuilder().rec(tag::SAMPLE, "a.wav").finish(true, Bytes{1}, 2), parsed) == ProfileStatus::BadField,
              "...but data after those bytes is refused");
        {
            // Trailing data that starts exactly where the reader's buffer is empty: one byte per read.
            Bytes t = FileBuilder().rec(tag::SAMPLE, "a.wav").finish(true, Bytes{0});
            ChunkSource cs(t, 1);
            CHECK(BinaryCodec::read(cs, parsed) == ProfileStatus::BadField, "a trailing byte is found even when the buffer was empty");
            Bytes u = FileBuilder().rec(tag::SAMPLE, "a.wav").finish();
            ChunkSource cu(u, 1);
            CHECK(BinaryCodec::read(cu, parsed) == ProfileStatus::Ok, "a file with nothing after END is accepted one byte at a time");
        }
        Bytes short_end = FileBuilder().finish();
        short_end[short_end.size() - 5] = 3;
        short_end.pop_back();
        CHECK(decode(short_end, parsed) == ProfileStatus::BadField, "END shorter than a CRC: invalid");
    }

    // ----- Forward compatibility -----
    {
        FileBuilder b;
        b.rec(tag::PROFILE_NAME, "P").rec(0x7E, Bytes(10, 0xAB)).rec(tag::SAMPLE, "a.wav").rec(0x03, Bytes{}).rec(tag::LOOP, f32(90.0f));
        b.rec(0xFE, Bytes(255, 1)).rec(tag::TRACK, Bytes{0}).rec(0x30, Bytes{1, 2, 3}).rec(tag::GAIN, f32(0.5f));
        CHECK(decode(b.finish(), parsed) == ProfileStatus::Ok && parsed.sample_count == 1 && parsed.loops[0].bpm == 90.0f &&
                  parsed.loops[0].tracks[0].gain == 0.5f,
              "unknown tags are skipped (including a 255-byte one and an empty one)");

        Bytes gain = f32(2.0f);
        gain.insert(gain.end(), {1, 2, 3});
        CHECK(status_of({{tag::GAIN, gain}}) == ProfileStatus::Ok && parsed.loops[0].tracks[0].gain == 2.0f,
              "a known record with a longer payload is accepted (extra bytes ignored)");
        Bytes loop = f32(111.0f);
        loop.push_back(5);
        FileBuilder lb;
        lb.rec(tag::LOOP, loop);
        CHECK(decode(lb.finish(), parsed) == ProfileStatus::Ok && parsed.loops[0].bpm == 111.0f, "LOOP with a longer payload is accepted");
    }

    // ----- Order and structure errors -----
    {
        CHECK(decode(FileBuilder().rec(tag::TRACK, Bytes{0}).finish(), parsed) == ProfileStatus::BadField, "TRACK before any LOOP");
        CHECK(decode(FileBuilder().rec(tag::SAMPLE, "a").rec(tag::LOOP, f32(100)).rec(tag::GAIN, f32(1.5f)).finish(), parsed) == ProfileStatus::BadField,
              "track field before any TRACK (loop without track)");
        CHECK(decode(FileBuilder().rec(tag::GAIN, f32(1.5f)).finish(), parsed) == ProfileStatus::BadField, "track field at the very beginning");
        CHECK(decode(FileBuilder().rec(tag::LOOP, f32(100)).rec(tag::SAMPLE, "a").finish(), parsed) == ProfileStatus::BadField, "SAMPLE after a LOOP");
        CHECK(decode(FileBuilder().rec(tag::LOOP, f32(100)).rec(tag::PROFILE_NAME, "n").finish(), parsed) == ProfileStatus::BadField, "name after a LOOP");
        // The second LOOP closes the first one's tracks: a field right after it has no track.
        CHECK(decode(FileBuilder().rec(tag::SAMPLE, "a").rec(tag::LOOP, f32(100)).rec(tag::TRACK, Bytes{0}).rec(tag::LOOP, f32(100)).rec(tag::PATTERN, Bytes{1, 0}).finish(), parsed) ==
                  ProfileStatus::BadField,
              "a field right after a new LOOP does not go to the previous loop's track");
        CHECK(decode(FileBuilder().rec(tag::SAMPLE, "a").rec(tag::LOOP, f32(100)).rec(tag::TRACK, Bytes{1}).finish(), parsed) == ProfileStatus::BadField,
              "track pointing outside the table of samples");
        CHECK(decode(FileBuilder().rec(tag::LOOP, f32(100)).rec(tag::TRACK, Bytes{0}).finish(), parsed) == ProfileStatus::BadField,
              "track with an empty table of samples");
        CHECK(decode(FileBuilder().rec(tag::SAMPLE, "a").rec(tag::LOOP, f32(100)).rec(tag::TRACK, Bytes{}).finish(), parsed) == ProfileStatus::BadField,
              "TRACK with no payload");
        CHECK(decode(FileBuilder().rec(tag::LOOP, Bytes{0, 0, 0}).finish(), parsed) == ProfileStatus::BadField, "LOOP with a payload too short");
        CHECK(status_of({{tag::GAIN, Bytes{0, 0, 0}}}) == ProfileStatus::BadField, "GAIN with a payload too short");
        CHECK(status_of({{tag::PATTERN, Bytes{1}}}) == ProfileStatus::BadField, "PATTERN with a payload too short");
        CHECK(status_of({{tag::VELOCITY, Bytes(15, 100)}}) == ProfileStatus::BadField, "VELOCITY with 15 values");
        CHECK(status_of({{tag::EXTRA_HIT, Bytes{0, 0, 100, 0}}}) == ProfileStatus::BadField, "EXTRA_HIT with a payload too short");
        CHECK(status_of({{tag::ATTACK, Bytes{1}}}) == ProfileStatus::BadField, "ATTACK with a payload too short");
    }

    // ----- Limits -----
    {
        auto many = [](uint8_t tag_id, int count, bool with_sample) {
            FileBuilder b;
            if (with_sample) b.rec(tag::SAMPLE, "a");
            for (int i = 0; i < count; ++i) {
                if (tag_id == tag::SAMPLE) b.rec(tag::SAMPLE, "s");
                if (tag_id == tag::LOOP) b.rec(tag::LOOP, f32(100));
                if (tag_id == tag::TRACK) {
                    if (i == 0) b.rec(tag::LOOP, f32(100));
                    b.rec(tag::TRACK, Bytes{0});
                }
            }
            return b.finish();
        };
        CHECK(decode(many(tag::SAMPLE, MAX_SAMPLES, false), parsed) == ProfileStatus::Ok && parsed.sample_count == MAX_SAMPLES, "64 samples accepted");
        CHECK(decode(many(tag::SAMPLE, MAX_SAMPLES + 1, false), parsed) == ProfileStatus::TooBig, "65 samples: too big");
        CHECK(decode(many(tag::LOOP, MAX_LOOPS, false), parsed) == ProfileStatus::Ok && parsed.loop_count == MAX_LOOPS, "12 loops accepted");
        CHECK(decode(many(tag::LOOP, MAX_LOOPS + 1, false), parsed) == ProfileStatus::TooBig, "13 loops: too big");
        CHECK(decode(many(tag::TRACK, cfg::MAX_TRACKS, true), parsed) == ProfileStatus::Ok && parsed.loops[0].track_count == cfg::MAX_TRACKS, "16 tracks accepted");
        CHECK(decode(many(tag::TRACK, cfg::MAX_TRACKS + 1, true), parsed) == ProfileStatus::TooBig, "17 tracks: too big");

        // Names.
        std::string n47(47, 'a'), n48(48, 'a'), p23(23, 'p'), p24(24, 'p');
        CHECK(decode(FileBuilder().rec(tag::SAMPLE, n47.c_str()).finish(), parsed) == ProfileStatus::Ok && std::strlen(parsed.samples[0]) == 47, "47-character sample name accepted");
        CHECK(decode(FileBuilder().rec(tag::SAMPLE, n48.c_str()).finish(), parsed) == ProfileStatus::BadField, "48-character sample name refused");
        CHECK(decode(FileBuilder().rec(tag::PROFILE_NAME, p23.c_str()).finish(), parsed) == ProfileStatus::Ok && std::strlen(parsed.name) == 23, "23-character profile name accepted");
        CHECK(decode(FileBuilder().rec(tag::PROFILE_NAME, p24.c_str()).finish(), parsed) == ProfileStatus::BadField, "24-character profile name refused");
        CHECK(decode(FileBuilder().rec(tag::SAMPLE, Bytes{'a', 0, 'b'}).finish(), parsed) == ProfileStatus::BadField, "zero byte inside a name refused");
        CHECK(decode(FileBuilder().rec(tag::SAMPLE, Bytes{'a', '\n'}).finish(), parsed) == ProfileStatus::BadField, "control character in a name refused");
        CHECK(decode(FileBuilder().rec(tag::PROFILE_NAME, Bytes{0x7F}).finish(), parsed) == ProfileStatus::BadField, "DEL in a name refused");
        CHECK(decode(FileBuilder().rec(tag::SAMPLE, Bytes{'a', 0x1F}).finish(), parsed) == ProfileStatus::BadField, "unit separator (0x1F) in a name refused");
        CHECK(decode(FileBuilder().rec(tag::SAMPLE, "a b.wav").finish(), parsed) == ProfileStatus::Ok && std::strcmp(parsed.samples[0], "a b.wav") == 0,
              "a space in a name is accepted");
        // A shorter name read after a longer one leaves no trace of the old one.
        CHECK(decode(FileBuilder().rec(tag::PROFILE_NAME, "Longer name").rec(tag::SAMPLE, "long sample name.wav").finish(), parsed) == ProfileStatus::Ok, "long names");
        CHECK(decode(FileBuilder().rec(tag::PROFILE_NAME, "A").rec(tag::SAMPLE, "b").finish(), parsed) == ProfileStatus::Ok &&
                  std::strcmp(parsed.name, "A") == 0 && std::strcmp(parsed.samples[0], "b") == 0,
              "short names read over long ones are properly terminated");
        CHECK(decode(FileBuilder().rec(tag::SAMPLE, Bytes{0xC3, 0xA9}).finish(), parsed) == ProfileStatus::Ok, "UTF-8 bytes in a name accepted");
        CHECK(decode(FileBuilder().rec(tag::SAMPLE, Bytes{}).finish(), parsed) == ProfileStatus::Ok && parsed.sample_count == 1 && parsed.samples[0][0] == '\0',
              "empty sample name accepted (as JSON does)");
    }

    // ----- Range errors, field by field: the end of the range is accepted, one more is refused -----
    {
        using Field = std::pair<uint8_t, Bytes>;
        auto list = [](uint8_t tag_id, int index, int v, int fill) {
            Bytes b(16, static_cast<uint8_t>(fill));
            b[static_cast<std::size_t>(index)] = static_cast<uint8_t>(v);
            return Field{tag_id, b};
        };
        auto u16 = [](uint8_t tag_id, int v) { return Field{tag_id, Bytes{static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)}}; };
        auto b1 = [](uint8_t tag_id, int v) { return Field{tag_id, Bytes{static_cast<uint8_t>(v)}}; };
        auto hit = [](int step, int off, int vel, int pit, int pan) {
            return Field{tag::EXTRA_HIT, Bytes{static_cast<uint8_t>(step), static_cast<uint8_t>(off), static_cast<uint8_t>(vel), static_cast<uint8_t>(pit), static_cast<uint8_t>(pan)}};
        };
        auto ok = [&](const Field& f) { return status_of({f}) == ProfileStatus::Ok; };
        auto bad = [&](const Field& f) { return status_of({f}) == ProfileStatus::BadField; };

        CHECK(ok(list(tag::VELOCITY, 15, 1, 127)) && ok(list(tag::VELOCITY, 0, 127, 100)), "velocity 1 and 127 accepted");
        CHECK(bad(list(tag::VELOCITY, 0, 0, 100)) && bad(list(tag::VELOCITY, 15, 128, 100)) && bad(list(tag::VELOCITY, 7, 255, 100)), "velocity 0, 128, 255 refused");
        CHECK(ok(list(tag::STEP_PITCH, 3, static_cast<uint8_t>(-12), 0)) && ok(list(tag::STEP_PITCH, 3, 12, 0)), "step pitch -12 and 12 accepted");
        CHECK(bad(list(tag::STEP_PITCH, 3, static_cast<uint8_t>(-13), 0)) && bad(list(tag::STEP_PITCH, 15, 13, 0)), "step pitch -13 and 13 refused");
        CHECK(ok(list(tag::STEP_PAN, 3, static_cast<uint8_t>(-50), 0)) && ok(list(tag::STEP_PAN, 3, 50, 0)), "step pan -50 and 50 accepted");
        CHECK(bad(list(tag::STEP_PAN, 3, static_cast<uint8_t>(-51), 0)) && bad(list(tag::STEP_PAN, 15, 51, 0)), "step pan -51 and 51 refused");
        CHECK(ok(list(tag::STEP_OFFSET, 3, static_cast<uint8_t>(-64), 0)) && ok(list(tag::STEP_OFFSET, 3, 63, 0)), "step offset -64 and 63 accepted");
        CHECK(bad(list(tag::STEP_OFFSET, 3, static_cast<uint8_t>(-65), 0)) && bad(list(tag::STEP_OFFSET, 15, 64, 0)), "step offset -65 and 64 refused");

        CHECK(ok(hit(15, -64, 1, -12, -50)) && ok(hit(0, 63, 127, 12, 50)), "extra hit at the ends of its ranges accepted");
        // Short payloads right after a full record of zeros: a reader that forgot to check the length would read the
        // stale bytes left by the previous record and accept.
        CHECK(status_of({list(tag::STEP_PAN, 0, 0, 0), {tag::STEP_PITCH, Bytes(15, 0)}}) == ProfileStatus::BadField, "STEP_PITCH with 15 bytes refused");
        CHECK(status_of({list(tag::STEP_PITCH, 0, 0, 0), {tag::STEP_PAN, Bytes(15, 0)}}) == ProfileStatus::BadField, "STEP_PAN with 15 bytes refused");
        CHECK(status_of({list(tag::STEP_PITCH, 0, 0, 0), {tag::STEP_OFFSET, Bytes(15, 0)}}) == ProfileStatus::BadField, "STEP_OFFSET with 15 bytes refused");
        CHECK(status_of({hit(0, 0, 100, 0, 0), {tag::EXTRA_HIT, Bytes{1, 0, 100, 0}}}) == ProfileStatus::BadField, "EXTRA_HIT with 4 bytes refused");
        CHECK(bad(hit(16, 0, 100, 0, 0)), "extra hit on step 16 refused");
        CHECK(bad(hit(0, -65, 100, 0, 0)) && bad(hit(0, 64, 100, 0, 0)), "extra hit offset out of range refused");
        CHECK(bad(hit(0, 0, 0, 0, 0)) && bad(hit(0, 0, 128, 0, 0)), "extra hit velocity out of range refused");
        CHECK(bad(hit(0, 0, 100, -13, 0)) && bad(hit(0, 0, 100, 13, 0)), "extra hit pitch out of range refused");
        CHECK(bad(hit(0, 0, 100, 0, -51)) && bad(hit(0, 0, 100, 0, 51)), "extra hit pan out of range refused");
        CHECK(status_of({hit(2, 1, 100, 0, 0), hit(2, 2, 100, 0, 0), hit(2, 3, 100, 0, 0)}) == ProfileStatus::Ok && parsed.loops[0].tracks[0].extra_count[2] == 3,
              "three extra hits on a step accepted");
        CHECK(status_of({hit(2, 1, 100, 0, 0), hit(2, 2, 100, 0, 0), hit(2, 3, 100, 0, 0), hit(2, 4, 100, 0, 0)}) == ProfileStatus::BadField, "a fourth extra hit on a step refused");

        CHECK(ok(b1(tag::QUANTIZE, 100)) && bad(b1(tag::QUANTIZE, 101)) && bad(b1(tag::QUANTIZE, 255)), "quantize 100 accepted, 101 and 255 refused");
        CHECK(ok(b1(tag::WIDTH, static_cast<uint8_t>(-100))) && ok(b1(tag::WIDTH, 100)), "width -100 and 100 accepted");
        CHECK(bad(b1(tag::WIDTH, static_cast<uint8_t>(-101))) && bad(b1(tag::WIDTH, 101)), "width -101 and 101 refused");
        CHECK(ok(b1(tag::BALANCE, static_cast<uint8_t>(-50))) && ok(b1(tag::BALANCE, 50)), "balance -50 and 50 accepted");
        CHECK(bad(b1(tag::BALANCE, static_cast<uint8_t>(-51))) && bad(b1(tag::BALANCE, 51)), "balance -51 and 51 refused");
        CHECK(ok(b1(tag::SUSTAIN, 0)) && ok(b1(tag::SUSTAIN, 100)) && bad(b1(tag::SUSTAIN, 101)), "sustain 0 and 100 accepted, 101 refused");
        for (uint8_t id : {tag::ATTACK, tag::DECAY, tag::RELEASE})
            CHECK(ok(u16(id, 0)) && ok(u16(id, 2000)) && bad(u16(id, 2001)) && bad(u16(id, 65535)), "envelope time 0 and 2000 accepted, 2001 and 65535 refused");

        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        CHECK(bad({tag::GAIN, f32(nan)}) && bad({tag::GAIN, f32(inf)}) && bad({tag::PITCH, f32(nan)}) && bad({tag::PITCH, f32(-inf)}), "gain and pitch must be finite");
        CHECK(decode(FileBuilder().rec(tag::LOOP, f32(nan)).finish(), parsed) == ProfileStatus::BadField, "NaN bpm refused");
        CHECK(decode(FileBuilder().rec(tag::LOOP, f32(inf)).finish(), parsed) == ProfileStatus::BadField, "infinite bpm refused");
        CHECK(ok({tag::GAIN, f32(0.0f)}) && ok({tag::GAIN, f32(-1.0f)}), "gain 0 and negative gain accepted (as JSON does; the engine clamps)");
    }

    // ----- Writer: invalid profiles are refused, nothing written -----
    {
        fill_profile(source, 1, 2, 2, false);
        auto refused = [&](const char* msg) {
            MemorySink sink(big, sizeof big);
            CHECK(!BinaryCodec::write(source, sink) && sink.size() == 0 && !BinaryCodec::valid(source), msg);
        };
        const ProfileData good = source;

        source.loops[0].tracks[1].sample = 2;            refused("sample index outside the table");
        source = good; source.loops[0].tracks[0].gain = std::numeric_limits<float>::quiet_NaN(); refused("NaN gain");
        source = good; source.loops[0].bpm = std::numeric_limits<float>::infinity();             refused("infinite bpm");
        source = good; source.loops[0].tracks[0].step_velocity[3] = 0;   refused("velocity 0");
        source = good; source.loops[0].tracks[0].step_pitch[3] = 13;     refused("pitch 13");
        source = good; source.loops[0].tracks[0].extra_count[0] = 4;     refused("four extra hits on a step");
        source = good; source.loops[0].tracks[0].extra_count[0] = 1; source.loops[0].tracks[0].extra[0][0].velocity = 0; refused("extra hit with velocity 0");
        source = good; source.loops[0].tracks[0].pitch = std::numeric_limits<float>::quiet_NaN(); refused("NaN pitch");
        source = good; source.loops[0].tracks[0].pitch = std::numeric_limits<float>::infinity();  refused("infinite pitch");
        source = good; source.loops[0].tracks[0].width = 101;            refused("width 101");
        source = good; source.loops[0].tracks[0].width = -101;           refused("width -101");
        source = good; source.loops[0].tracks[0].balance = 51;           refused("balance 51");
        source = good; source.loops[0].tracks[0].balance = -51;          refused("balance -51");
        source = good; source.loops[0].tracks[0].attack = 2001;          refused("attack 2001");
        source = good; source.loops[0].tracks[0].decay = 2001;           refused("decay 2001");
        source = good; source.loops[0].tracks[0].step_pan[2] = -51;      refused("step pan -51");
        source = good; source.loops[0].tracks[0].step_pan[2] = 51;       refused("step pan 51");
        source = good; source.loops[0].tracks[0].step_offset[2] = 64;    refused("step offset 64");
        source = good; source.loops[0].tracks[0].step_offset[2] = -65;   refused("step offset -65");
        source = good; source.loops[0].tracks[0].step_velocity[2] = 128; refused("step velocity 128");
        source = good; source.loops[0].tracks[0].extra_count[1] = 1; source.loops[0].tracks[0].extra[1][0] = Hit{64, 100, 0, 0};  refused("extra hit offset 64");
        source = good; source.loops[0].tracks[0].extra_count[1] = 1; source.loops[0].tracks[0].extra[1][0] = Hit{0, 100, 13, 0};  refused("extra hit pitch 13");
        source = good; source.loops[0].tracks[0].extra_count[1] = 1; source.loops[0].tracks[0].extra[1][0] = Hit{0, 100, 0, 51};  refused("extra hit pan 51");
        source = good; source.loops[0].tracks[0].extra_count[1] = 1; source.loops[0].tracks[0].extra[1][0] = Hit{0, 128, 0, 0};  refused("extra hit velocity 128");
        source = good; source.name[0] = 0x1F;                            refused("0x1F in the profile name");
        source = good; source.samples[1][0] = '\x7F';                   refused("DEL in a sample name");
        source = good; source.loops[0].tracks[0].quantize = 101;         refused("quantize 101");
        source = good; source.loops[0].tracks[0].release = 2001;         refused("release 2001");
        source = good; source.loops[0].tracks[0].sustain = 101;          refused("sustain 101");
        source = good; source.name[3] = '\n';                             refused("control character in the profile name");
        source = good; std::memset(source.name, 'x', sizeof source.name); refused("profile name without a terminator");
        source = good; std::memset(source.samples[0], 'y', sizeof source.samples[0]); refused("sample name without a terminator");
        source = good; source.loop_count = MAX_LOOPS + 1;                 refused("too many loops");
        // Counts one over the limit, with harmless data in the memory just behind the array: the check on the count
        // itself must refuse, not the content that happens to be there.
        source = ProfileData{}; source.sample_count = MAX_SAMPLES + 1;    // samples[64] overlays loop_count (0): an empty name
        refused("too many samples");
        source = good; source.loop_count = 1; source.loops[0].track_count = cfg::MAX_TRACKS + 1;
        { const TrackData plain{}; std::memcpy(&source.loops[0].tracks[0] + cfg::MAX_TRACKS, &plain, sizeof plain); }
        refused("too many tracks");
        source = good;
        CHECK(BinaryCodec::valid(source), "the reference profile is valid");
    }

    // ----- Writer: a sink that fails (card full) -----
    {
        fill_profile(source, 2, 3, 3, false);
        const Bytes b = encode(source);
        bool all_failed = true;
        for (std::size_t cap = 0; cap < b.size(); ++cap) {
            static uint8_t small[8192];
            MemorySink     sink(small, cap);
            all_failed = all_failed && !BinaryCodec::write(source, sink);
        }
        CHECK(all_failed, "a sink that is one byte too small, or smaller: write fails");
        MemorySink exact(big, b.size());
        CHECK(BinaryCodec::write(source, exact) && exact.size() == b.size(), "a sink of exactly the right size works");
    }

    // ----- Truncation: no cut file is ever accepted -----
    {
        fill_profile(source, 2, 3, 3, true);
        const Bytes b = encode(source);
        bool never_ok = true;
        for (std::size_t n = 0; n < b.size(); ++n) never_ok = never_ok && decode(Bytes(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(n)), parsed) != ProfileStatus::Ok;
        CHECK(never_ok, "every prefix of a valid file is refused");
        bool exactly = true;
        for (std::size_t n = 0; n < b.size(); ++n) exactly = exactly && decode(Bytes(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(n)), parsed) == ProfileStatus::Truncated;
        CHECK(exactly, "every prefix of a valid file is reported as truncated (and nothing else)");

        bool all_truncated = true;
        for (std::size_t n = 5; n < b.size(); ++n)
            all_truncated = all_truncated && decode(Bytes(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(n)), parsed) != ProfileStatus::NotAProfile;
        CHECK(all_truncated, "a cut file is never mistaken for a foreign file");

        // The big profile, at a spread of cut points.
        MemorySink sink(big, sizeof big);
        BinaryCodec::write(worst, sink);
        const Bytes w(big, big + sink.size());
        bool wok = true;
        for (std::size_t n = 0; n < w.size(); n += 997) wok = wok && decode(Bytes(w.begin(), w.begin() + static_cast<std::ptrdiff_t>(n)), parsed) != ProfileStatus::Ok;
        CHECK(wok, "cuts of the worst-case file are refused");
    }

    // ----- Corruption: any changed byte is detected -----
    {
        fill_profile(source, 2, 3, 3, true);
        const Bytes b = encode(source);
        bool detected = true;
        for (std::size_t i = 0; i < b.size(); ++i) {
            for (uint8_t mask : {0x01, 0x80, 0xFF}) {
                Bytes c = b;
                c[i] ^= mask;
                detected = detected && decode(c, parsed) != ProfileStatus::Ok;
            }
        }
        CHECK(detected, "any byte changed (one bit, the top bit, all bits) is refused");

        // Two bytes swapped.
        bool swapped = true;
        for (std::size_t i = 5; i + 1 < b.size(); i += 3) {
            Bytes c = b;
            if (c[i] == c[i + 1]) continue;
            std::swap(c[i], c[i + 1]);
            swapped = swapped && decode(c, parsed) != ProfileStatus::Ok;
        }
        CHECK(swapped, "two neighbouring bytes swapped are refused");
    }

    // ----- Random input: never a crash, and whatever is accepted is valid -----
    {
        fill_profile(source, 2, 3, 3, true);
        const Bytes b = encode(source);
        int accepted = 0;
        bool consistent = true;
        for (int iter = 0; iter < 20000; ++iter) {
            Bytes c;
            const int kind = iter % 4;
            if (kind == 0) {  // pure noise
                c.resize(static_cast<std::size_t>(rnd_range(0, 400)));
                for (auto& x : c) x = static_cast<uint8_t>(rnd());
            } else if (kind == 1) {  // noise after a good header
                c = {'D', 'M', 'P', 'B', 1};
                const int n = rnd_range(0, 300);
                for (int i = 0; i < n; ++i) c.push_back(static_cast<uint8_t>(rnd()));
            } else {  // a valid file with a few damaged bytes, or cut
                c = b;
                const int hits = rnd_range(1, 4);
                for (int i = 0; i < hits; ++i) c[rnd() % c.size()] = static_cast<uint8_t>(rnd());
                if (kind == 3) c.resize(rnd() % c.size());
            }
            // Records of random tags with a correct CRC: reaches the field parsers, not just the CRC check.
            if (iter % 8 == 7) {
                FileBuilder fb;
                fb.rec(tag::SAMPLE, "a").rec(tag::LOOP, f32(100)).rec(tag::TRACK, Bytes{0});
                const int n = rnd_range(1, 8);
                for (int i = 0; i < n; ++i) {
                    Bytes payload(static_cast<std::size_t>(rnd_range(0, 20)));
                    for (auto& x : payload) x = static_cast<uint8_t>(rnd());
                    fb.rec(static_cast<uint8_t>(rnd_range(0x10, 0x31)), payload);
                }
                c = fb.finish();
            }
            if (decode(c, parsed) == ProfileStatus::Ok) {
                ++accepted;
                consistent = consistent && BinaryCodec::valid(parsed);
                static ProfileData again;
                consistent = consistent && decode(encode(parsed), again) == ProfileStatus::Ok && same(parsed, again);
            }
        }
        CHECK(consistent, "everything the reader accepts is valid, and survives a write and a read");
        CHECK(accepted > 100, "the random tests do reach accepted files (they are not all rejected early)");
    }

    if (failures == 0) std::printf("OK: binary profile codec\n");
    return failures == 0 ? 0 : 1;
}
