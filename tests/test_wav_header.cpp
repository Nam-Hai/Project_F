#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <vector>

#include "../config.h"
#include "../core/wav/wav_header.h"

using namespace wav;

static int failures = 0;

#define CHECK(cond, msg)                    \
    do {                                    \
        if (!(cond)) {                      \
            std::printf("FAIL: %s\n", msg); \
            ++failures;                     \
        }                                   \
    } while (0)

using Bytes = std::vector<uint8_t>;

static void put16(Bytes& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
static void put32(Bytes& b, uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}
static void tag4(Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); }

// A 'fmt ' chunk body (16 bytes, or 40 for WAVE_FORMAT_EXTENSIBLE with the given sub-format).
static Bytes fmt_body(int tag, int channels, uint32_t rate, int bits, int extensible_sub = 0) {
    Bytes b;
    put16(b, static_cast<uint32_t>(extensible_sub ? 0xFFFE : tag));
    put16(b, static_cast<uint32_t>(channels));
    put32(b, rate);
    put32(b, rate * static_cast<uint32_t>(channels * bits / 8));
    put16(b, static_cast<uint32_t>(channels * bits / 8));
    put16(b, static_cast<uint32_t>(bits));
    if (extensible_sub) {
        put16(b, 22);                      // cbSize
        put16(b, static_cast<uint32_t>(bits));  // valid bits
        put32(b, 3);                       // channel mask
        put16(b, static_cast<uint32_t>(extensible_sub));  // sub-format: the first 2 bytes of the GUID
        const uint8_t guid_rest[14] = {0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71};
        b.insert(b.end(), guid_rest, guid_rest + 14);
    }
    return b;
}

static void chunk(Bytes& b, const char* id, const Bytes& body, int declared_size = -1, bool pad = true) {
    tag4(b, id);
    put32(b, declared_size < 0 ? static_cast<uint32_t>(body.size()) : static_cast<uint32_t>(declared_size));
    b.insert(b.end(), body.begin(), body.end());
    if (pad && (body.size() & 1)) b.push_back(0);  // padding byte
}

static Bytes riff(const Bytes& chunks) {
    Bytes b;
    tag4(b, "RIFF");
    put32(b, static_cast<uint32_t>(4 + chunks.size()));
    tag4(b, "WAVE");
    b.insert(b.end(), chunks.begin(), chunks.end());
    return b;
}

// fmt + data in one call.
static Bytes simple(int tag, int channels, uint32_t rate, int bits, std::size_t data_bytes, int sub = 0) {
    Bytes c;
    chunk(c, "fmt ", fmt_body(tag, channels, rate, bits, sub));
    chunk(c, "data", Bytes(data_bytes, 0x11));
    return riff(c);
}

static Status probe(const Bytes& b, Info& info) {
    MemoryFile f(b.data(), b.size());
    return read_header(f, info);
}

// Counts the bytes asked of the file, to show that a header never reads the data.
class CountingFile : public FileReader {
public:
    explicit CountingFile(const Bytes& b) : inner_(b.data(), b.size()) {}
    uint32_t size() const override { return inner_.size(); }
    std::size_t read_at(uint32_t offset, uint8_t* dst, std::size_t length) override {
        requested += length;
        ++calls;
        return inner_.read_at(offset, dst, length);
    }
    std::size_t requested = 0;
    int         calls     = 0;

private:
    MemoryFile inner_;
};

// A file that fails (reads nothing) after a given number of calls: an I/O error.
class FailingFile : public FileReader {
public:
    FailingFile(const Bytes& b, int ok_calls) : inner_(b.data(), b.size()), ok_calls_(ok_calls) {}
    uint32_t size() const override { return inner_.size(); }
    std::size_t read_at(uint32_t offset, uint8_t* dst, std::size_t length) override {
        if (calls_++ >= ok_calls_) return 0;
        return inner_.read_at(offset, dst, length);
    }

private:
    MemoryFile inner_;
    int        ok_calls_;
    int        calls_ = 0;
};

static uint32_t rng_state = 99;
static uint32_t rnd() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

// What must hold whenever the status is Ok.
static bool consistent(const Info& i, uint32_t file_size) {
    const uint32_t frame = (i.format == Format::Pcm16 ? 2u : 4u) * i.channels;
    return (i.format == Format::Pcm16 || i.format == Format::Float32) && (i.channels == 1 || i.channels == 2) &&
           i.sample_rate == static_cast<uint32_t>(cfg::SAMPLE_RATE) && i.data_offset >= 12 + 8 &&
           static_cast<uint64_t>(i.data_offset) + i.data_bytes <= file_size && i.data_bytes == i.frames * frame;
}

int main() {
    const uint32_t R = static_cast<uint32_t>(cfg::SAMPLE_RATE);
    Info           info;

    // ----- Accepted formats -----
    {
        CHECK(probe(simple(1, 1, R, 16, 10), info) == Status::Ok && info.format == Format::Pcm16 && info.channels == 1 && info.sample_rate == R &&
                  info.bits == 16 && info.tag == 1 && info.frames == 5 && info.data_bytes == 10 && info.data_offset == 12 + 8 + 16 + 8,
              "PCM 16-bit mono: format, fields, 5 frames, data right after the two chunk headers");
        CHECK(probe(simple(1, 2, R, 16, 12), info) == Status::Ok && info.channels == 2 && info.frames == 3 && info.data_bytes == 12, "PCM 16-bit stereo: 3 frames");
        CHECK(probe(simple(3, 1, R, 32, 16), info) == Status::Ok && info.format == Format::Float32 && info.tag == 3 && info.frames == 4 && info.data_bytes == 16,
              "float 32-bit mono: 4 frames");
        CHECK(probe(simple(3, 2, R, 32, 16), info) == Status::Ok && info.format == Format::Float32 && info.channels == 2 && info.frames == 2, "float 32-bit stereo: 2 frames");
        CHECK(probe(simple(1, 1, R, 16, 0), info) == Status::Ok && info.frames == 0 && info.data_bytes == 0, "no sound data: accepted, 0 frames");
        CHECK(probe(simple(0, 2, R, 32, 16, 3), info) == Status::Ok && info.format == Format::Float32 && info.tag == 3 && info.channels == 2 && info.data_offset == 12 + 8 + 40 + 8,
              "extensible with the float sub-format");
        CHECK(probe(simple(0, 1, R, 16, 4, 1), info) == Status::Ok && info.format == Format::Pcm16 && info.tag == 1 && info.frames == 2, "extensible with the PCM sub-format");
    }

    // ----- Distinct values in every field of 'fmt ': each one is read from the right place -----
    {
        Bytes c;
        Bytes body = fmt_body(1, 2, R, 16);
        chunk(c, "fmt ", body);
        chunk(c, "data", Bytes(8, 1));
        probe(riff(c), info);
        CHECK(info.tag == 1 && info.channels == 2 && info.sample_rate == R && info.bits == 16, "fmt fields read from their own positions");

        // Unsupported file with odd values: the fields still come back, for the error message.
        Bytes d;
        chunk(d, "fmt ", fmt_body(2, 3, 22050, 12));
        chunk(d, "data", Bytes(8, 1));
        CHECK(probe(riff(d), info) == Status::Unsupported && info.tag == 2 && info.channels == 3 && info.sample_rate == 22050 && info.bits == 12,
              "unsupported file: the declared format is reported");
    }

    // ----- Refused formats -----
    {
        CHECK(probe(simple(1, 3, R, 16, 12), info) == Status::Unsupported && info.channels == 3, "3 channels refused");
        CHECK(probe(simple(1, 0, R, 16, 12), info) == Status::Unsupported, "0 channels refused");
        CHECK(probe(simple(1, 1, 48000, 16, 12), info) == Status::Unsupported && info.sample_rate == 48000, "48 kHz refused");
        CHECK(probe(simple(1, 1, R - 1, 16, 12), info) == Status::Unsupported, "44099 Hz refused");
        CHECK(probe(simple(1, 1, R + 1, 16, 12), info) == Status::Unsupported, "44101 Hz refused");
        CHECK(probe(simple(1, 1, R, 24, 12), info) == Status::Unsupported, "PCM 24-bit refused");
        CHECK(probe(simple(1, 1, R, 8, 12), info) == Status::Unsupported, "PCM 8-bit refused");
        CHECK(probe(simple(1, 1, R, 32, 12), info) == Status::Unsupported, "PCM 32-bit refused");
        CHECK(probe(simple(3, 1, R, 64, 16), info) == Status::Unsupported, "float 64-bit refused");
        CHECK(probe(simple(3, 1, R, 16, 16), info) == Status::Unsupported, "float declared on 16 bits refused");
        CHECK(probe(simple(2, 1, R, 16, 16), info) == Status::Unsupported, "compressed format (ADPCM) refused");
        CHECK(probe(simple(0xFFFE, 1, R, 16, 16), info) == Status::Unsupported, "extensible tag with no extension (16-byte fmt) refused");
        CHECK(probe(simple(0, 1, R, 24, 12, 1), info) == Status::Unsupported, "extensible PCM 24-bit refused");
        CHECK(info.format == Format::Unknown && info.frames == 0 && info.data_offset == 0, "a refused file has no data fields");
    }

    // ----- Structure errors -----
    {
        CHECK(probe({}, info) == Status::NotWav, "empty file");
        CHECK(probe(Bytes(11, 0), info) == Status::NotWav, "11 bytes");
        Bytes b = simple(1, 1, R, 16, 4);
        Bytes x = b;
        x[0] = 'X';
        CHECK(probe(x, info) == Status::NotWav, "wrong RIFF magic");
        x = b;
        x[3] = 'X';
        CHECK(probe(x, info) == Status::NotWav, "RIFF magic wrong in the last byte");
        x = b;
        x[8] = 'X';
        CHECK(probe(x, info) == Status::NotWav, "wrong WAVE magic");
        x = b;
        x[11] = 'X';
        CHECK(probe(x, info) == Status::NotWav, "WAVE magic wrong in the last byte");
        CHECK(probe(riff({}), info) == Status::NoFmt, "RIFF/WAVE and nothing else");

        Bytes only_fmt;
        chunk(only_fmt, "fmt ", fmt_body(1, 1, R, 16));
        CHECK(probe(riff(only_fmt), info) == Status::NoData && info.channels == 1, "no 'data' chunk");

        Bytes data_first;
        chunk(data_first, "data", Bytes(8, 1));
        chunk(data_first, "fmt ", fmt_body(1, 1, R, 16));
        CHECK(probe(riff(data_first), info) == Status::DataBeforeFmt, "'data' before 'fmt '");

        Bytes small_fmt;  // a 'fmt ' chunk of 14 bytes is not usable: skipped
        chunk(small_fmt, "fmt ", Bytes(14, 0));
        chunk(small_fmt, "data", Bytes(8, 1));
        CHECK(probe(riff(small_fmt), info) == Status::DataBeforeFmt, "'fmt ' chunk too small: as if absent");
        Bytes small_only;
        chunk(small_only, "fmt ", Bytes(14, 0));
        CHECK(probe(riff(small_only), info) == Status::NoFmt, "only a too-small 'fmt ' chunk");

        // A 'fmt ' that announces 16 bytes but is cut: skipped.
        Bytes cut = simple(1, 1, R, 16, 4);
        cut.resize(12 + 8 + 10);
        CHECK(probe(cut, info) == Status::NoFmt, "'fmt ' cut by the end of the file");
    }

    // ----- Chunks in between -----
    {
        Bytes c;
        chunk(c, "LIST", Bytes(3, 7));  // odd size: one padding byte
        chunk(c, "fmt ", fmt_body(1, 2, R, 16));
        chunk(c, "bext", Bytes(100, 9));
        chunk(c, "JUNK", Bytes(5, 9));
        chunk(c, "data", Bytes(8, 1));
        CHECK(probe(riff(c), info) == Status::Ok && info.frames == 2 && info.data_offset == 12 + (8 + 4) + (8 + 16) + (8 + 100) + (8 + 6) + 8,
              "chunks before and between, odd sizes padded: data found at the right position");

        // Chunk after data: ignored.
        Bytes d;
        chunk(d, "fmt ", fmt_body(1, 1, R, 16));
        chunk(d, "data", Bytes(4, 1));
        chunk(d, "LIST", Bytes(20, 1));
        CHECK(probe(riff(d), info) == Status::Ok && info.frames == 2, "a chunk after 'data' does not matter");

        // The last 'fmt ' wins.
        Bytes e;
        chunk(e, "fmt ", fmt_body(1, 1, 48000, 16));
        chunk(e, "fmt ", fmt_body(1, 2, R, 16));
        chunk(e, "data", Bytes(8, 1));
        CHECK(probe(riff(e), info) == Status::Ok && info.channels == 2, "two 'fmt ' chunks: the last one counts");
    }

    // ----- Sizes that lie -----
    {
        // data cut by the end of the file.
        Bytes a;
        chunk(a, "fmt ", fmt_body(1, 1, R, 16));
        chunk(a, "data", Bytes(7, 1), 100, false);
        CHECK(probe(riff(a), info) == Status::Ok && info.frames == 3 && info.data_bytes == 6, "mono 16-bit, 100 announced, 7 present: 3 whole frames");
        Bytes b;
        chunk(b, "fmt ", fmt_body(3, 2, R, 32));
        chunk(b, "data", Bytes(10, 1), 1000, false);
        CHECK(probe(riff(b), info) == Status::Ok && info.frames == 1 && info.data_bytes == 8, "float stereo, 10 bytes present: 1 whole frame");
        Bytes c;
        chunk(c, "fmt ", fmt_body(1, 2, R, 16));
        chunk(c, "data", Bytes(3, 1), 8, false);
        CHECK(probe(riff(c), info) == Status::Ok && info.frames == 0 && info.data_bytes == 0, "less than one frame present: 0 frames");

        // A streaming writer left 0xFFFFFFFF.
        Bytes d;
        chunk(d, "fmt ", fmt_body(1, 1, R, 16));
        chunk(d, "data", Bytes(20, 1), -1 + 0);
        d[d.size() - 20 - 4] = 0xFF;
        d[d.size() - 20 - 3] = 0xFF;
        d[d.size() - 20 - 2] = 0xFF;
        d[d.size() - 20 - 1] = 0xFF;
        CHECK(probe(riff(d), info) == Status::Ok && info.frames == 10, "data size 0xFFFFFFFF: the whole rest of the file");

        // A chunk of size 0xFFFFFFFF before the data: the position would overflow 32 bits.
        Bytes e;
        chunk(e, "fmt ", fmt_body(1, 1, R, 16));
        chunk(e, "LIST", Bytes(4, 1));
        e[e.size() - 4 - 4] = 0xFF;
        e[e.size() - 4 - 3] = 0xFF;
        e[e.size() - 4 - 2] = 0xFF;
        e[e.size() - 4 - 1] = 0xFF;
        chunk(e, "data", Bytes(8, 1));
        CHECK(probe(riff(e), info) == Status::NoData, "a chunk of 4 GB before 'data': no data found, and no wrap-around");
        for (uint32_t huge : {0xFFFFFFF0u, 0xFFFFFFFEu, 0x80000000u, 0x7FFFFFFFu}) {
            Bytes f;
            chunk(f, "fmt ", fmt_body(1, 1, R, 16));
            tag4(f, "LIST");
            put32(f, huge);
            chunk(f, "data", Bytes(8, 1));
            CHECK(probe(riff(f), info) == Status::NoData, "huge LIST sizes: no data found");
        }

        // The RIFF size field is not trusted: wrong values do not matter.
        Bytes g = simple(1, 1, R, 16, 8);
        g[4] = g[5] = g[6] = g[7] = 0;
        CHECK(probe(g, info) == Status::Ok && info.frames == 4, "the RIFF size field is ignored");
    }

    // ----- Chunk identifiers and sizes are matched exactly -----
    {
        // A chunk whose name only starts like 'fmt ' or 'data' is not one of them.
        Bytes a;
        chunk(a, "fmt ", fmt_body(1, 1, R, 16));
        chunk(a, "fmtX", fmt_body(1, 2, 48000, 16));
        chunk(a, "datX", Bytes(8, 1));
        chunk(a, "data", Bytes(12, 1));
        CHECK(probe(riff(a), info) == Status::Ok && info.channels == 1 && info.sample_rate == R && info.frames == 6, "'fmtX' and 'datX' are skipped like any other chunk");

        // A 'fmt ' chunk of 15 bytes is too small, even if its first 15 bytes look right (the 16th would be the padding).
        Bytes b;
        Bytes body = fmt_body(1, 1, R, 16);
        body.pop_back();  // 15 bytes: the high byte of 'bits' is missing
        chunk(b, "fmt ", body);
        chunk(b, "data", Bytes(8, 1));
        CHECK(probe(riff(b), info) == Status::DataBeforeFmt, "'fmt ' of 15 bytes: too small, as if absent");

        // WAVE_FORMAT_EXTENSIBLE needs a 'fmt ' of 26 bytes at least; 25 is not enough, even if the 26th byte (padding) is there.
        Bytes c;
        Bytes ext = fmt_body(0, 1, R, 16, 1);
        ext.resize(25);
        chunk(c, "fmt ", ext);
        chunk(c, "data", Bytes(8, 1));
        CHECK(probe(riff(c), info) == Status::Unsupported && info.tag == 0xFFFE, "extensible 'fmt ' of 25 bytes: the sub-format is not read");

        // An extensible 'fmt ' cut after 25 bytes by the end of the file: found (16 bytes are there), sub-format unreadable, no data.
        Bytes dc;
        chunk(dc, "fmt ", fmt_body(0, 1, R, 16, 1));
        Bytes d = riff(dc);
        d.resize(12 + 8 + 25);
        CHECK(probe(d, info) == Status::NoData && info.channels == 1 && info.tag == 0xFFFE, "'fmt ' cut after 25 bytes: the 16 bytes are used");

        // A chunk of exactly 16 MB before the data: its size needs all four bytes (the top one is 0x01).
        Bytes e;
        chunk(e, "LIST", Bytes(0x01000000, 0));
        chunk(e, "fmt ", fmt_body(1, 1, R, 16));
        chunk(e, "data", Bytes(8, 1));
        CHECK(probe(riff(e), info) == Status::Ok && info.frames == 4 && info.data_offset == 12 + 8 + 0x01000000 + 8 + 16 + 8, "a 16 MB chunk is jumped over (the size has four bytes)");
    }

    // ----- Number of chunks walked -----
    {
        auto with_junk = [&](int k) {
            Bytes c;
            for (int i = 0; i < k; ++i) chunk(c, "junk", Bytes());
            chunk(c, "fmt ", fmt_body(1, 1, R, 16));
            chunk(c, "data", Bytes(8, 1));
            return riff(c);
        };
        CHECK(probe(with_junk(62), info) == Status::Ok, "'data' as the 64th chunk: found");
        CHECK(probe(with_junk(63), info) == Status::NoData, "'data' as the 65th chunk: not looked for");
        CHECK(probe(with_junk(64), info) == Status::NoFmt, "'fmt ' as the 65th chunk: not looked for");
        CHECK(probe(with_junk(1000), info) == Status::NoFmt, "a thousand chunks: gives up");
    }

    // ----- A header never reads the sound data -----
    {
        Bytes big = simple(1, 2, R, 16, 2 * 1024 * 1024);
        CountingFile f(big);
        CHECK(read_header(f, info) == Status::Ok && info.frames == 2u * 1024 * 1024 / 4, "2 MB of sound data: 524288 frames");
        CHECK(f.requested < 100 && f.calls <= 4, "...read from fewer than 100 bytes");

        Bytes c;
        chunk(c, "LIST", Bytes(500 * 1024, 3));
        chunk(c, "fmt ", fmt_body(3, 2, R, 32));
        chunk(c, "data", Bytes(1024 * 1024, 1));
        Bytes      with_list = riff(c);
        CountingFile g(with_list);
        CHECK(read_header(g, info) == Status::Ok && info.frames == 131072, "a 500 KB LIST chunk, then 1 MB of data");
        CHECK(g.requested < 200, "...the LIST chunk is jumped over, not read");
    }

    // ----- I/O errors -----
    {
        Bytes b = simple(1, 1, R, 16, 10);
        bool  never_ok_when_failing = true, never_crash = true;
        for (int ok_calls = 0; ok_calls < 6; ++ok_calls) {
            FailingFile f(b, ok_calls);
            const Status s = read_header(f, info);
            never_crash = never_crash && (s == Status::NotWav || s == Status::NoFmt || s == Status::NoData || s == Status::Ok);
            if (ok_calls < 3) never_ok_when_failing = never_ok_when_failing && s != Status::Ok;
        }
        CHECK(never_crash && never_ok_when_failing, "a file that stops answering: refused, never a crash");
    }

    // ----- Every prefix of valid files; mutated headers; random bytes -----
    {
        const Bytes samples[] = {simple(1, 2, R, 16, 40), simple(3, 2, R, 32, 40), simple(0, 2, R, 32, 40, 3)};
        bool        ok = true, never_wrong = true;
        for (const Bytes& b : samples) {
            Info full;
            probe(b, full);
            for (std::size_t n = 0; n <= b.size(); ++n) {
                const Bytes cut(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(n));
                const Status s = probe(cut, info);
                if (s == Status::Ok) {
                    ok = ok && consistent(info, static_cast<uint32_t>(n));
                    never_wrong = never_wrong && info.frames <= full.frames && info.data_offset == full.data_offset;
                }
                // Cut before the end of the 'data' header: never accepted.
                if (n < full.data_offset) never_wrong = never_wrong && s != Status::Ok;
            }
        }
        CHECK(ok, "every prefix: when accepted, the numbers are consistent with the file size");
        CHECK(never_wrong, "every prefix: never accepted before the data starts, never more frames than the whole file");

        bool fuzz_ok = true;
        int  accepted = 0;
        for (int iter = 0; iter < 30000; ++iter) {
            Bytes b = samples[iter % 3];
            const int hits = 1 + static_cast<int>(rnd() % 4);
            for (int i = 0; i < hits; ++i) b[rnd() % std::min<std::size_t>(b.size(), 64)] = static_cast<uint8_t>(rnd());
            if (iter % 5 == 0) b.resize(rnd() % b.size());
            if (iter % 7 == 0) {  // plain noise
                b.assign(rnd() % 100, 0);
                for (auto& x : b) x = static_cast<uint8_t>(rnd());
            }
            const Status s = probe(b, info);
            if (s == Status::Ok) {
                ++accepted;
                fuzz_ok = fuzz_ok && consistent(info, static_cast<uint32_t>(b.size()));
            }
        }
        CHECK(fuzz_ok, "30000 damaged headers: nothing accepted is inconsistent");
        CHECK(accepted > 500, "the damaged headers include accepted ones (the test goes through the whole parser)");
    }

    if (failures == 0) std::printf("OK: wav header\n");
    return failures == 0 ? 0 : 1;
}
