#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "../config.h"
#include "../core/wav/wav_header.h"
#include "../core/wav/wav_reader.h"

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

// A complete WAV file around `data`; `extra_before_data` bytes of a LIST chunk shift the position of the data.
static Bytes make_wav(int tag, int channels, int bits, const Bytes& data, std::size_t list_bytes = 0, int declared_data = -1) {
    Bytes fmt;
    put16(fmt, static_cast<uint32_t>(tag));
    put16(fmt, static_cast<uint32_t>(channels));
    put32(fmt, static_cast<uint32_t>(cfg::SAMPLE_RATE));
    put32(fmt, static_cast<uint32_t>(cfg::SAMPLE_RATE * channels * bits / 8));
    put16(fmt, static_cast<uint32_t>(channels * bits / 8));
    put16(fmt, static_cast<uint32_t>(bits));

    Bytes chunks;
    if (list_bytes) {
        tag4(chunks, "LIST");
        put32(chunks, static_cast<uint32_t>(list_bytes));
        chunks.insert(chunks.end(), list_bytes, 0x55);
        if (list_bytes & 1) chunks.push_back(0);
    }
    tag4(chunks, "fmt ");
    put32(chunks, 16);
    chunks.insert(chunks.end(), fmt.begin(), fmt.end());
    tag4(chunks, "data");
    put32(chunks, declared_data < 0 ? static_cast<uint32_t>(data.size()) : static_cast<uint32_t>(declared_data));
    chunks.insert(chunks.end(), data.begin(), data.end());

    Bytes b;
    tag4(b, "RIFF");
    put32(b, static_cast<uint32_t>(4 + chunks.size()));
    tag4(b, "WAVE");
    b.insert(b.end(), chunks.begin(), chunks.end());
    return b;
}

static uint32_t bits_of(float v) {
    uint32_t b;
    std::memcpy(&b, &v, 4);
    return b;
}

// Records every request made to the file.
class SpyFile : public FileReader {
public:
    explicit SpyFile(const Bytes& b) : inner_(b.data(), b.size()) {}
    uint32_t size() const override { return inner_.size(); }
    std::size_t read_at(uint32_t offset, uint8_t* dst, std::size_t length) override {
        requested += length;
        biggest = std::max(biggest, length);
        ++calls;
        return inner_.read_at(offset, dst, length);
    }
    std::size_t requested = 0, biggest = 0;
    int         calls = 0;

private:
    MemoryFile inner_;
};

// A file that answers normally up to `limit` bytes (as if it had shrunk), and not at all beyond.
class ShrinkingFile : public FileReader {
public:
    ShrinkingFile(const Bytes& b, uint32_t limit) : inner_(b.data(), b.size()), limit_(limit) {}
    uint32_t size() const override { return inner_.size(); }
    std::size_t read_at(uint32_t offset, uint8_t* dst, std::size_t length) override {
        if (offset >= limit_) return 0;
        const std::size_t n = std::min<std::size_t>(length, limit_ - offset);
        return inner_.read_at(offset, dst, n);
    }

private:
    MemoryFile inner_;
    uint32_t   limit_;
};

static uint32_t rng_state = 7;
static uint32_t rnd() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

// The conversion written independently, for the reference.
static int16_t reference_float(float v) {
    if (v != v) return 0;
    const double x = static_cast<double>(v) * 32767.0;
    if (x >= 32767.0) return 32767;
    if (x <= -32768.0) return -32768;
    return static_cast<int16_t>(x < 0 ? -static_cast<long>(-x + 0.5) : static_cast<long>(x + 0.5));
}

int main() {
    // ----- Float to 16-bit conversion -----
    {
        CHECK(float_bits_to_int16(bits_of(0.0f)) == 0 && float_bits_to_int16(bits_of(-0.0f)) == 0, "0 and -0 give 0");
        CHECK(float_bits_to_int16(bits_of(0.5f)) == 16384 && float_bits_to_int16(bits_of(-0.5f)) == -16384, "0.5 -> 16384, -0.5 -> -16384 (rounded)");
        CHECK(float_bits_to_int16(bits_of(0.25f)) == 8192 && float_bits_to_int16(bits_of(-0.25f)) == -8192, "0.25 -> 8192");
        CHECK(float_bits_to_int16(bits_of(1.0f)) == 32767 && float_bits_to_int16(bits_of(-1.0f)) == -32767, "1 -> 32767, -1 -> -32767");
        CHECK(float_bits_to_int16(bits_of(1.0001f)) == 32767 && float_bits_to_int16(bits_of(2.0f)) == 32767 && float_bits_to_int16(bits_of(1e30f)) == 32767, "above 1: clipped at 32767");
        CHECK(float_bits_to_int16(bits_of(-1.00004f)) == -32768 && float_bits_to_int16(bits_of(-2.0f)) == -32768 && float_bits_to_int16(bits_of(-1e30f)) == -32768, "below -1: clipped at -32768");
        const float inf = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(float_bits_to_int16(bits_of(inf)) == 32767 && float_bits_to_int16(bits_of(-inf)) == -32768, "infinities are clipped");
        CHECK(float_bits_to_int16(bits_of(nan)) == 0 && float_bits_to_int16(0x7FC00001u) == 0 && float_bits_to_int16(0xFFC00000u) == 0, "not a number: 0");
        CHECK(float_bits_to_int16(bits_of(1.0f / 65534.0f)) == 1 && float_bits_to_int16(bits_of(0.4f / 32767.0f)) == 0 && float_bits_to_int16(bits_of(0.6f / 32767.0f)) == 1, "rounds to the nearest");
        CHECK(float_bits_to_int16(bits_of(-0.6f / 32767.0f)) == -1 && float_bits_to_int16(bits_of(-0.4f / 32767.0f)) == 0, "rounds to the nearest (negative side)");
        CHECK(float_bits_to_int16(bits_of(1e-45f)) == 0, "a denormal: 0");
        CHECK(float_bits_to_int16(bits_of(32767.0f / 32767.0f)) == 32767 && float_bits_to_int16(bits_of(32766.0f / 32767.0f)) == 32766, "the top steps are exact");
        CHECK(float_bits_to_int16(bits_of(-32767.0f / 32767.0f)) == -32767 && float_bits_to_int16(bits_of(-32766.0f / 32767.0f)) == -32766, "the bottom steps are exact");

        // The conversion multiplies in float, so a value within about 0.001 of a rounding tie may go either way (the
        // reference computes in double): equal everywhere else, and never more than one step apart.
        auto agrees = [](float v) {
            const int16_t r   = float_bits_to_int16(bits_of(v));
            const int16_t ref = reference_float(v);
            const double  x   = static_cast<double>(v) * 32767.0;
            const double  frac = std::fabs(x - std::floor(x) - 0.5);
            return std::abs(r - ref) <= 1 && (frac > 0.01 || x >= 32767.0 || x <= -32768.0 ? r == ref : true);
        };
        bool same = true, monotone = true;
        int16_t previous = -32768;
        for (int i = -2000; i <= 2000; ++i) {
            const float v = static_cast<float>(i) / 1900.0f;  // from a bit below -1 to a bit above 1
            const int16_t r = float_bits_to_int16(bits_of(v));
            same = same && agrees(v);
            monotone = monotone && r >= previous;
            previous = r;
        }
        for (int i = 0; i < 50000; ++i) {
            const float v = (static_cast<float>(rnd() % 2000001) - 1000000.0f) / 1000000.0f;
            same = same && agrees(v);
        }
        CHECK(same, "same as an independent reference on 54000 values (up to a tie)");
        CHECK(monotone, "never decreases when the input grows");
    }

    // ----- PCM 16-bit: the samples come back unchanged -----
    {
        Bytes data;
        std::vector<int16_t> expect;
        for (int i = 0; i < 6000; ++i) {
            const int16_t v = static_cast<int16_t>(rnd());
            expect.push_back(v);
            put16(data, static_cast<uint16_t>(v));
        }
        for (int channels : {1, 2}) {
            const Bytes   file = make_wav(1, channels, 16, data);
            MemoryFile    f(file.data(), file.size());
            Info          info;
            CHECK(read_header(f, info) == Status::Ok, "header of a PCM file");
            SampleReader  r(f, info);
            std::vector<int16_t> out(6000 + 8, 0x1234);
            CHECK(r.frames_left() == 6000u / channels, "frames to read announced");
            const uint32_t n = r.read(out.data(), 100000);
            CHECK(n == 6000u / channels && std::equal(expect.begin(), expect.end(), out.begin()) && out[6000] == 0x1234, "PCM 16-bit: unchanged, and nothing written past the end");
            CHECK(r.frames_left() == 0 && !r.failed() && r.read(out.data(), 10) == 0, "all delivered: nothing more, no failure");
        }
    }

    // ----- Float 32-bit: converted -----
    {
        const float values[] = {0.5f, -0.5f, 1.0f, -1.0f, 2.0f, -2.0f, std::numeric_limits<float>::quiet_NaN(), 0.25f};
        Bytes       data;
        for (float v : values) put32(data, bits_of(v));
        const Bytes   file = make_wav(3, 2, 32, data);
        MemoryFile    f(file.data(), file.size());
        Info          info;
        read_header(f, info);
        SampleReader  r(f, info);
        int16_t       out[8] = {};
        CHECK(r.read(out, 4) == 4, "four stereo frames read");
        CHECK(out[0] == 16384 && out[1] == -16384 && out[2] == 32767 && out[3] == -32767 && out[4] == 32767 && out[5] == -32768 && out[6] == 0 && out[7] == 8192,
              "float stereo: every value converted and interleaved");
    }

    // ----- Reading in pieces gives the same result as in one go, whatever the sizes -----
    {
        Bytes data;
        for (int i = 0; i < 40000; ++i) put32(data, bits_of(static_cast<float>(static_cast<int>(rnd() % 20001) - 10000) / 10000.0f));
        struct Case { int tag, channels, bits; const char* name; };
        for (const Case& c : {Case{3, 2, 32, "float stereo"}, Case{3, 1, 32, "float mono"}, Case{1, 2, 16, "PCM stereo"}, Case{1, 1, 16, "PCM mono"}}) {
            const Bytes  file = make_wav(c.tag, c.channels, c.bits, data, 10);  // data not at the very start
            MemoryFile   f(file.data(), file.size());
            Info         info;
            read_header(f, info);
            std::vector<int16_t> whole(static_cast<std::size_t>(info.frames) * info.channels);
            SampleReader all(f, info);
            const uint32_t total = all.read(whole.data(), info.frames);
            CHECK(total == info.frames && !all.failed(), c.name);

            bool equal = true;
            for (uint32_t step : {1u, 2u, 3u, 7u, 100u, 511u, 512u, 513u, 1000u, 2047u, 2048u, 2049u, 4095u, 4096u, 4097u, 9999u}) {
                SampleReader         pieces(f, info);
                std::vector<int16_t> got(whole.size(), 0x4242);
                uint32_t             done = 0;
                while (done < info.frames) {
                    const uint32_t n = pieces.read(got.data() + static_cast<std::size_t>(done) * info.channels, step);
                    if (n == 0) break;
                    done += n;
                }
                equal = equal && done == info.frames && got == whole && pieces.frames_left() == 0;
            }
            CHECK(equal, "pieces of 1 to 9999 frames give the same samples as one big read");
        }
    }

    // ----- Requests to the file: small, and each byte of data read once -----
    {
        Bytes data(3 * 1024 * 1024 / 2, 0x20);
        for (int tag_bits = 0; tag_bits < 2; ++tag_bits) {
            const Bytes file = tag_bits == 0 ? make_wav(1, 2, 16, data) : make_wav(3, 2, 32, data);
            SpyFile     f(file);
            Info        info;
            read_header(f, info);
            const std::size_t header_bytes = f.requested;
            SampleReader      r(f, info);
            std::vector<int16_t> out(static_cast<std::size_t>(info.frames) * info.channels);
            r.read(out.data(), info.frames);
            CHECK(f.biggest <= 4096, "no request to the file is bigger than 4 KB");
            CHECK(f.requested - header_bytes == info.data_bytes, "every byte of data is asked for once, and nothing else");
        }
    }

    // ----- Partial reads (the first seconds of a sample, for the preview) -----
    {
        Bytes data;
        for (int i = 0; i < 1000; ++i) put16(data, static_cast<uint16_t>(i));
        const Bytes file = make_wav(1, 1, 16, data);
        MemoryFile  f(file.data(), file.size());
        Info        info;
        read_header(f, info);
        SampleReader r(f, info);
        int16_t      out[1000];
        CHECK(r.read(out, 0) == 0 && r.frames_left() == 1000, "reading 0 frames: nothing, nothing consumed");
        CHECK(r.read(out, 10) == 10 && out[0] == 0 && out[9] == 9 && r.frames_left() == 990, "the first 10 frames");
        CHECK(r.read(out, 5) == 5 && out[0] == 10 && out[4] == 14, "then the next 5, from the right place");
        CHECK(r.read(out, 2000) == 985 && out[0] == 15 && out[984] == 999 && r.frames_left() == 0, "asking for more than is left: what is left");
        CHECK(r.read(out, 1) == 0, "then nothing");
    }

    // ----- Files that lie or shrink -----
    {
        // The header announces more than the file holds: the header already counts only what is there.
        Bytes data(2000, 0x21);
        const Bytes cut = make_wav(1, 2, 16, data, 0, 100000);
        MemoryFile  f(cut.data(), cut.size());
        Info        info;
        read_header(f, info);
        SampleReader r(f, info);
        std::vector<int16_t> out(1000);
        CHECK(info.frames == 500 && r.read(out.data(), 1000) == 500 && !r.failed(), "announced size beyond the file: the frames that exist are read, no failure");

        // The file shrinks after the header was read (card pulled): the reader reports it and delivers whole frames only.
        const Bytes full = make_wav(1, 2, 16, Bytes(8000, 0x33));
        for (uint32_t cut_at : {uint32_t(44), uint32_t(45), uint32_t(46), uint32_t(47), uint32_t(48), uint32_t(1000), uint32_t(1001), uint32_t(44 + 4096), uint32_t(44 + 4097), uint32_t(44 + 4099), uint32_t(44 + 5000)}) {
            ShrinkingFile sf(full, cut_at);
            MemoryFile    mf(full.data(), full.size());
            Info          i2;
            read_header(mf, i2);
            SampleReader  rr(sf, i2);
            std::vector<int16_t> o(4000, 0x5555);
            const uint32_t n = rr.read(o.data(), 2000);
            const uint32_t expect_frames = (cut_at - 44) / 4;
            CHECK(n == expect_frames && rr.failed() && rr.read(o.data(), 10) == 0, "a shrunk file: only the whole frames before the cut, then failure");
            CHECK(n * 2 + 1 >= 0 && (n == 2000 || o[static_cast<std::size_t>(n) * 2] == 0x5555), "...and nothing written after them");
        }

        // A reader built on a header that was refused delivers nothing.
        Info bad;
        SampleReader nothing(f, bad);
        int16_t      o[4] = {};
        CHECK(nothing.failed() && nothing.read(o, 4) == 0 && nothing.frames_left() == 0, "a reader without an accepted header delivers nothing");
        Info unknown = info;
        unknown.format = Format::Unknown;  // channels and frames look fine, but the format was never accepted
        SampleReader unk(f, unknown);
        CHECK(unk.failed() && unk.read(o, 4) == 0 && unk.frames_left() == 0, "an unknown format: nothing");
        Info three = info;
        three.channels = 3;
        SampleReader odd(f, three);
        CHECK(odd.failed() && odd.read(o, 4) == 0, "3 channels: nothing");
    }

    // ----- After a failure the reader stays failed, even if the file answers again -----
    {
        const Bytes full = make_wav(1, 2, 16, Bytes(8000, 0x33));
        class Flaky : public FileReader {
        public:
            explicit Flaky(const Bytes& b) : inner_(b.data(), b.size()) {}
            uint32_t size() const override { return inner_.size(); }
            std::size_t read_at(uint32_t offset, uint8_t* dst, std::size_t length) override {
                if (broken) {  // answers up to byte 144 only (100 bytes of data)
                    if (offset >= 44 + 100) return 0;
                    if (length > 44 + 100 - offset) length = 44 + 100 - offset;
                }
                return inner_.read_at(offset, dst, length);
            }
            bool broken = true;

        private:
            MemoryFile inner_;
        } flaky(full);
        MemoryFile mf(full.data(), full.size());
        Info       info;
        read_header(mf, info);
        SampleReader         r(flaky, info);
        std::vector<int16_t> o(4000, 0);
        const uint32_t       first = r.read(o.data(), 2000);
        CHECK(first == 25 && r.failed(), "the file stops answering after 100 bytes of data: 25 frames, failure");
        flaky.broken = false;  // the card is back...
        CHECK(r.read(o.data(), 2000) == 0 && r.failed(), "...but the reader does not carry on after a failure (the frames would no longer follow)");
    }

    // ----- Maximum size: a long sample through the same code -----
    {
        Bytes data;
        data.reserve(5u * 44100 * 8);
        for (std::size_t i = 0; i < 5u * 44100 * 2; ++i) put32(data, bits_of(std::sin(static_cast<float>(i) * 0.01f)));
        const Bytes file = make_wav(3, 2, 32, data);
        MemoryFile  f(file.data(), file.size());
        Info        info;
        read_header(f, info);
        SampleReader         r(f, info);
        std::vector<int16_t> out(static_cast<std::size_t>(info.frames) * 2);
        CHECK(r.read(out.data(), info.frames) == info.frames && info.frames == 5u * 44100, "5 seconds of stereo float: 220500 frames");
        bool ok = true;
        for (std::size_t i = 0; i < out.size(); i += 997) ok = ok && out[i] == reference_float(std::sin(static_cast<float>(i) * 0.01f));
        CHECK(ok, "the converted samples match the reference all along");
    }

    if (failures == 0) std::printf("OK: wav reader\n");
    return failures == 0 ? 0 : 1;
}
