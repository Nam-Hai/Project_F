#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../core/profile/binary_codec.h"
#include "../core/profile/profile_codec.h"
#include "../platform/desktop/file_stream.h"
#include "../tools/profile_tool.h"

namespace fs = std::filesystem;
using namespace profile;

static int failures = 0;

#define CHECK(cond, msg)                    \
    do {                                    \
        if (!(cond)) {                      \
            std::printf("FAIL: %s\n", msg); \
            ++failures;                     \
        }                                   \
    } while (0)

struct Result {
    int         code;
    std::string out;
    std::string err;
};

static Result run(std::vector<std::string> args) {
    std::vector<const char*> argv{"profile_tool"};
    for (const auto& a : args) argv.push_back(a.c_str());
    std::ostringstream out, err;
    const int          code = profile_tool::run(static_cast<int>(argv.size()), argv.data(), out, err);
    return {code, out.str(), err.str()};
}

static std::string read_all(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void write_all(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}

static ProfileCodec codec;
static ProfileData  data;
static ProfileData  big;

int main() {
    const std::string dir = "test_profile_tool_dir";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string real_json = std::string(PROFILES_DIR) + "/default.json";

    // ----- Usage -----
    CHECK(run({}).code == 2 && !run({}).err.empty(), "no argument: usage, code 2");
    {
        const std::string u = run({}).err;
        CHECK(u.find("bin2json") != std::string::npos && u.find("json2bin") != std::string::npos && u.find("check") != std::string::npos,
              "the usage message lists the three commands");
    }
    CHECK(run({"nonsense"}).code == 2, "unknown command: code 2");
    CHECK(run({"check"}).code == 2 && run({"check", "a", "b"}).code == 2, "check: wrong number of arguments");
    CHECK(run({"json2bin", "a"}).code == 2 && run({"json2bin", "a", "b", "c"}).code == 2, "json2bin: wrong number of arguments");
    CHECK(run({"bin2json"}).code == 2 && run({"bin2json", "a", "b", "c"}).code == 2, "bin2json: wrong number of arguments");

    // ----- A real profile through the whole chain: JSON -> binary -> JSON -> binary -----
    {
        const std::string a = dir + "/a.dmp", b = dir + "/b.dmp", j = dir + "/a.json";
        Result r = run({"json2bin", real_json, a});
        CHECK(r.code == 0 && r.err.empty() && fs::exists(a), "json2bin on profiles/default.json");
        CHECK(read_all(a).compare(0, 4, "DMPB") == 0, "the output is a binary profile");
        CHECK(!fs::exists(a + ".tmp"), "no .tmp left behind");

        r = run({"bin2json", a, j});
        CHECK(r.code == 0 && r.out.empty() && fs::exists(j), "bin2json to a file");
        CHECK(read_all(j).find("\"format\": \"dmp\"") != std::string::npos, "the output is a readable JSON profile");

        r = run({"bin2json", a});
        CHECK(r.code == 0 && r.out == read_all(j), "bin2json to the standard output gives the same text");

        CHECK(run({"json2bin", j, b}).code == 0 && read_all(a) == read_all(b), "binary -> JSON -> binary gives identical bytes");

        // The summary agrees with what the JSON codec reads from the original.
        const std::string text = read_all(real_json);
        CHECK(codec.parse(text.data(), text.size(), data) == ProfileStatus::Ok, "the real profile parses");
        int tracks = 0;
        for (int l = 0; l < data.loop_count; ++l) tracks += data.loops[l].track_count;
        r = run({"check", a});
        CHECK(r.code == 0 && r.out.find(": OK") != std::string::npos && r.out.find(std::string("name:    ") + data.name) != std::string::npos &&
                  r.out.find("samples: " + std::to_string(data.sample_count)) != std::string::npos &&
                  r.out.find("loops:   " + std::to_string(data.loop_count)) != std::string::npos &&
                  r.out.find("tracks:  " + std::to_string(tracks)) != std::string::npos &&
                  r.out.find("size:    " + std::to_string(read_all(a).size()) + " bytes") != std::string::npos,
              "check prints name, counts and size");
    }

    // ----- The biggest profile -----
    {
        big = ProfileData{};
        for (int i = 0; i < 40; ++i) {
            char name[32];
            std::snprintf(name, sizeof name, "Sample %d.wav", i);
            big.add_sample(name);
        }
        big.loop_count = MAX_LOOPS;
        for (int l = 0; l < MAX_LOOPS; ++l) {
            big.loops[l].bpm         = 90.5f + static_cast<float>(l);
            big.loops[l].track_count = cfg::MAX_TRACKS;
            for (int t = 0; t < cfg::MAX_TRACKS; ++t) {
                TrackData& tr = big.loops[l].tracks[t];
                tr.sample     = static_cast<uint8_t>((l * 3 + t) % 40);
                tr.pattern    = static_cast<uint16_t>(0x1111 << (t % 4));
                tr.gain       = 0.75f;
                tr.pitch      = 1.5f;
                tr.quantize   = 30;
                tr.width      = -20;
                tr.balance    = 10;
                tr.attack     = 7;
                tr.decay      = 300;
                tr.sustain    = 60;
                tr.release    = 40;
                for (int s = 0; s < cfg::MAX_STEPS; ++s) {
                    tr.step_velocity[s] = static_cast<uint8_t>(60 + s);
                    tr.step_pitch[s]    = static_cast<int8_t>(s - 8);
                    tr.step_pan[s]      = static_cast<int8_t>(s * 3 - 20);
                    tr.step_offset[s]   = static_cast<int8_t>(s * 4 - 30);
                    tr.extra_count[s]   = cfg::MAX_EXTRA_HITS;
                    for (int h = 0; h < cfg::MAX_EXTRA_HITS; ++h) tr.extra[s][h] = Hit{static_cast<int8_t>(h * 10 - 10), static_cast<uint8_t>(90 + h), static_cast<int8_t>(h), static_cast<int8_t>(h * 5)};
                }
            }
        }
        const std::string a = dir + "/big.dmp", j = dir + "/big.json", b = dir + "/big2.dmp";
        {
            std::ofstream f(a, std::ios::binary | std::ios::trunc);
            FileSink      sink(f);
            CHECK(BinaryCodec::write(big, sink), "the biggest profile is written");
        }
        CHECK(run({"bin2json", a, j}).code == 0, "bin2json on the biggest profile");
        std::printf("worst case JSON profile: %zu bytes (binary: %zu)\n", read_all(j).size(), read_all(a).size());
        CHECK(run({"json2bin", j, b}).code == 0 && read_all(a) == read_all(b), "biggest profile: binary -> JSON -> binary, identical bytes");
    }

    // ----- Failures never damage the output -----
    {
        const std::string out = dir + "/keep.dmp";
        write_all(out, "precious content");

        write_all(dir + "/bad.json", "{ this is not json");
        Result r = run({"json2bin", dir + "/bad.json", out});
        CHECK(r.code == 1 && !r.err.empty(), "invalid JSON: code 1 with a message");
        CHECK(read_all(out) == "precious content" && !fs::exists(out + ".tmp"), "...and the existing output is untouched, no .tmp");

        write_all(dir + "/notprofile.json", "{\"format\":\"other\",\"version\":1}");
        CHECK(run({"json2bin", dir + "/notprofile.json", out}).code == 1 && read_all(out) == "precious content", "JSON that is not a profile: refused");

        write_all(dir + "/toolong.json", "{\"format\":\"dmp\",\"version\":1,\"profile\":\"this name is much too long to fit\"}");
        CHECK(run({"json2bin", dir + "/toolong.json", out}).code == 1 && read_all(out) == "precious content", "a profile name that is too long: refused");

        {
            const Result m = run({"json2bin", dir + "/missing.json", out});
            CHECK(m.code == 1 && read_all(out) == "precious content" && m.err.find("cannot open") != std::string::npos, "missing input file: code 1, \"cannot open\"");
        }

        // Valid JSON, but a number that does not fit a float: the JSON codec reads it (as infinity), the binary format refuses it.
        write_all(dir + "/inf.json", "{\"format\":\"dmp\",\"version\":1,\"loops\":[{\"bpm\":1e39,\"tracks\":[]}]}");
        {
            const Result i = run({"json2bin", dir + "/inf.json", out});
            CHECK(i.code == 1 && i.err.find("binary format") != std::string::npos && read_all(out) == "precious content" && !fs::exists(out + ".tmp"),
                  "a bpm of 1e39 (infinite as a float): refused with a clear message, output untouched");
        }
        CHECK(run({"json2bin", real_json, dir + "/no/such/dir/x.dmp"}).code == 1, "output in a folder that does not exist: code 1");
        CHECK(!fs::exists(dir + "/no"), "...and nothing is created");

        // The output is an existing folder: the rename fails, the temporary file is removed.
        fs::create_directory(dir + "/adir");
        CHECK(run({"json2bin", real_json, dir + "/adir"}).code == 1 && fs::is_directory(dir + "/adir") && !fs::exists(dir + "/adir.tmp"),
              "output path is a folder: refused, no .tmp left");
        CHECK(run({"json2bin", real_json, dir + "/ref.dmp"}).code == 0, "reference binary profile for the next checks");
        CHECK(run({"bin2json", dir + "/ref.dmp", dir + "/adir"}).code == 1 && fs::is_directory(dir + "/adir") && !fs::exists(dir + "/adir.tmp"),
              "bin2json onto a folder: code 1, no .tmp left");

        // A folder where the .tmp should go.
        fs::create_directory(out + ".tmp");
        CHECK(run({"json2bin", real_json, out}).code == 1 && read_all(out) == "precious content", "temporary file impossible to create: refused, output intact");
        fs::remove_all(out + ".tmp");
    }

    // ----- Damaged binary files -----
    {
        const std::string good = dir + "/good.dmp";
        CHECK(run({"json2bin", real_json, good}).code == 0, "reference binary profile");
        const std::string bytes = read_all(good);

        write_all(dir + "/cut.dmp", bytes.substr(0, bytes.size() / 2));
        Result r = run({"check", dir + "/cut.dmp"});
        CHECK(r.code == 1 && r.out.empty() && !r.err.empty(), "check on a cut file: code 1, message on the error stream");

        std::string flipped = bytes;
        flipped[bytes.size() / 2] ^= 0x04;
        write_all(dir + "/flipped.dmp", flipped);
        CHECK(run({"check", dir + "/flipped.dmp"}).code == 1, "check on a file with one damaged byte: code 1");
        CHECK(run({"bin2json", dir + "/flipped.dmp", dir + "/f.json"}).code == 1 && !fs::exists(dir + "/f.json"), "bin2json on a damaged file: code 1, no output");
        CHECK(run({"bin2json", dir + "/flipped.dmp"}).out.empty(), "...and nothing on the standard output either");

        write_all(dir + "/json_as_bin.dmp", read_all(real_json));
        CHECK(run({"check", dir + "/json_as_bin.dmp"}).code == 1, "a JSON file given as binary: code 1");
        write_all(dir + "/empty.dmp", "");
        CHECK(run({"check", dir + "/empty.dmp"}).code == 1, "empty file: code 1");
        CHECK(run({"check", dir + "/absent.dmp"}).code == 1, "missing file: code 1");
        CHECK(run({"bin2json", dir + "/absent.dmp"}).code == 1, "bin2json on a missing file: code 1");
    }

    fs::remove_all(dir);
    if (failures == 0) std::printf("OK: profile_tool\n");
    return failures == 0 ? 0 : 1;
}
