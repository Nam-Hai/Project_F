#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../core/profile/binary_codec.h"
#include "../platform/desktop/profile_store_file.h"

using namespace profile;
namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond, msg)                    \
    do {                                    \
        if (!(cond)) {                      \
            std::printf("FAIL: %s\n", msg); \
            ++failures;                     \
        }                                   \
    } while (0)

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

static ProfileData make_profile(float bpm) {
    ProfileData p;
    p.add_sample("Kick1.wav");
    p.loop_count                 = 1;
    p.loops[0].bpm               = bpm;
    p.loops[0].track_count       = 1;
    p.loops[0].tracks[0].pattern = 0x1111;
    return p;
}

static ProfileData work;
static ProfileData other;

// The bpm of the first loop of a binary profile held in `bytes` (-1 if it does not read).
static float bpm_of(const std::string& bytes) {
    MemorySource src(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    return BinaryCodec::read(src, other) == ProfileStatus::Ok && other.loop_count > 0 ? other.loops[0].bpm : -1.0f;
}

int main() {
    const std::string dir  = "test_profile_store_dir";
    const std::string path = dir + "/sub/default.dmp";
    fs::remove_all(dir);

    FileProfileStore store(path);

    // No file yet: a new profile, no error.
    CHECK(store.load(work) == LoadResult::Missing && work.loop_count == 0 && work.sample_count == 0 && std::strcmp(work.name, "Default") == 0,
          "missing file: new profile");

    // Save: the folder is created, the file is a binary profile, no .tmp left behind.
    CHECK(store.save(make_profile(120.0f)), "save into a folder that has to be created");
    CHECK(fs::exists(path) && !fs::exists(path + ".tmp") && !fs::exists(path + ".bak"), "file created, neither .tmp nor .bak the first time");
    CHECK(read_all(path).compare(0, 4, "DMPB") == 0, "binary content (magic DMPB)");
    CHECK(store.load(work) == LoadResult::Loaded && work.loop_count == 1 && work.loops[0].bpm == 120.0f && work.loops[0].tracks[0].pattern == 0x1111,
          "read back");

    // Second save: the old version is kept in .bak.
    CHECK(store.save(make_profile(140.0f)), "second save");
    CHECK(fs::exists(path + ".bak") && !fs::exists(path + ".tmp"), ".bak created, no .tmp");
    CHECK(bpm_of(read_all(path + ".bak")) == 120.0f && bpm_of(read_all(path)) == 140.0f, ".bak = old version, file = new one");

    // Write failure: if the .tmp cannot be created, the old file stays intact.
    {
        const std::string before = read_all(path);
        fs::create_directory(path + ".tmp");  // a folder where the temporary file should go
        CHECK(!store.save(make_profile(99.0f)), "write impossible: refused");
        CHECK(std::strlen(store.last_error()) > 0, "error message set");
        CHECK(read_all(path) == before, "the old file is intact after the failure");
        fs::remove_all(path + ".tmp");
        CHECK(store.save(make_profile(99.0f)) && std::strlen(store.last_error()) == 0, "the next save works and clears the error");
    }

    // A .tmp left by a crash is never read as a profile.
    {
        write_all(path + ".tmp", "garbage, interrupted write");
        CHECK(store.load(work) == LoadResult::Loaded && work.loops[0].bpm == 99.0f, "an orphan .tmp is ignored when loading");
        CHECK(store.save(make_profile(100.0f)), "and it is replaced by the next save");
        CHECK(!fs::exists(path + ".tmp"), "no .tmp after the save");
    }

    // Damaged file: refused with a message, and set aside (.bak) before being replaced.
    {
        const std::string good = read_all(path);

        write_all(path, good.substr(0, good.size() / 2));
        CHECK(store.load(work) == LoadResult::Failed && std::strlen(store.last_error()) > 0, "cut file: failure with a message");

        write_all(path, good.substr(0, good.size() - 1));
        CHECK(store.load(work) == LoadResult::Failed, "file missing its last byte: failure");

        std::string flipped = good;
        flipped[good.size() / 2] ^= 0x10;
        write_all(path, flipped);
        CHECK(store.load(work) == LoadResult::Failed, "one damaged byte: failure (CRC)");

        write_all(path, "this is not a profile");
        CHECK(store.load(work) == LoadResult::Failed, "any text: failure");
        write_all(path, "{\"format\":\"dmp\",\"version\":1}");
        CHECK(store.load(work) == LoadResult::Failed, "a JSON profile is no longer read: failure");
        write_all(path, "");
        CHECK(store.load(work) == LoadResult::Failed, "empty file: failure");

        std::string newer = good;
        newer[4] = 9;
        write_all(path, newer);
        CHECK(store.load(work) == LoadResult::Failed, "version too recent: failure");

        write_all(path, "damaged content to keep");
        CHECK(store.save(make_profile(111.0f)), "saving over a damaged file works");
        CHECK(read_all(path + ".bak") == "damaged content to keep", "the damaged file is kept in .bak");
        CHECK(store.load(work) == LoadResult::Loaded && work.loops[0].bpm == 111.0f, "the new profile reads back");
    }

    // Invalid data: refused without touching the file.
    {
        const std::string before = read_all(path);
        ProfileData       bad    = make_profile(120.0f);
        bad.loops[0].tracks[0].sample = 9;  // outside the table of samples
        CHECK(!store.save(bad) && read_all(path) == before && std::strlen(store.last_error()) > 0, "inconsistent profile: refused, file intact");
        CHECK(!fs::exists(path + ".tmp"), "...and no .tmp left");
    }

    // The biggest possible profile goes through the files too.
    {
        ProfileData big;
        for (int i = 0; i < 40; ++i) {
            char name[32];
            std::snprintf(name, sizeof name, "Sample %d.wav", i);
            big.add_sample(name);
        }
        big.loop_count = MAX_LOOPS;
        for (int l = 0; l < MAX_LOOPS; ++l) {
            big.loops[l].bpm         = 90.0f + static_cast<float>(l);
            big.loops[l].track_count = cfg::MAX_TRACKS;
            for (int t = 0; t < cfg::MAX_TRACKS; ++t) {
                TrackData& tr = big.loops[l].tracks[t];
                tr.sample     = static_cast<uint8_t>((l + t) % 40);
                tr.pattern    = static_cast<uint16_t>(0x1111 << (t % 4));
                tr.gain       = 0.5f;
                for (int s = 0; s < cfg::MAX_STEPS; ++s) {
                    tr.step_velocity[s] = static_cast<uint8_t>(60 + s);
                    tr.extra_count[s]   = cfg::MAX_EXTRA_HITS;
                    for (int h = 0; h < cfg::MAX_EXTRA_HITS; ++h) tr.extra[s][h] = Hit{static_cast<int8_t>(h * 10), 90, 0, 0};
                }
            }
        }
        CHECK(store.save(big) && store.load(work) == LoadResult::Loaded && work.loop_count == MAX_LOOPS &&
                  work.loops[11].bpm == 101.0f && work.loops[11].tracks[15].extra[15][2].offset == 20,
              "12 loops of 16 full tracks: saved and read back");
    }

    // Impossible path (a file where the folder should be).
    {
        write_all(dir + "/file", "x");
        FileProfileStore broken(dir + "/file/default.dmp");
        CHECK(!broken.save(make_profile(120.0f)), "folder impossible to create: refused");
        CHECK(std::strlen(broken.last_error()) > 0, "error set");
    }

    fs::remove_all(dir);
    if (failures == 0) std::printf("OK\n");
    return failures ? 1 : 0;
}
