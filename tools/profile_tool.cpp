#include "profile_tool.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "../core/profile/binary_codec.h"
#include "../core/profile/profile_codec.h"
#include "../platform/desktop/file_stream.h"

namespace fs = std::filesystem;
using namespace profile;

namespace profile_tool {

namespace {

// Big objects are static (the token table alone is about 100 KB).
ProfileCodec g_codec;
ProfileData  g_data;
char         g_json[256 * 1024];  // the worst-case JSON profile is about 234 KB

int usage(std::ostream& err) {
    err << "usage:\n"
           "  profile_tool bin2json <profile.dmp> [out.json]\n"
           "  profile_tool json2bin <profile.json> <out.dmp>\n"
           "  profile_tool check <profile.dmp>\n";
    return 2;
}

bool read_binary(const std::string& path, std::ostream& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err << "cannot open " << path << "\n";
        return false;
    }
    FileSource           source(in);
    const ProfileStatus status = BinaryCodec::read(source, g_data);
    if (status != ProfileStatus::Ok) {
        err << path << ": " << describe(status) << "\n";
        return false;
    }
    return true;
}

// Writes through "<path>.tmp" then renames: the destination is only replaced by a complete file.
template <typename Writer>
bool write_atomically(const std::string& path, std::ostream& err, Writer&& write) {
    const std::string tmp = path + ".tmp";
    std::error_code   ec;
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            err << "cannot write " << path << "\n";
            return false;
        }
        const bool ok = write(out);
        out.flush();
        if (!ok || !out) {
            out.close();
            fs::remove(tmp, ec);
            err << "writing " << path << " failed\n";
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        err << "cannot replace " << path << "\n";
        return false;
    }
    return true;
}

int bin2json(const std::string& in, const std::string* out_path, std::ostream& out, std::ostream& err) {
    if (!read_binary(in, err)) return 1;
    const int len = ProfileCodec::write(g_data, g_json, sizeof g_json);
    if (len < 0) {
        err << in << ": the profile cannot be written as JSON\n";
        return 1;
    }
    if (!out_path) {
        out.write(g_json, len);
        return out ? 0 : 1;
    }
    return write_atomically(*out_path, err, [&](std::ostream& f) { f.write(g_json, len); return static_cast<bool>(f); }) ? 0 : 1;
}

int json2bin(const std::string& in, const std::string& out_path, std::ostream& err) {
    std::ifstream file(in, std::ios::binary);
    if (!file) {
        err << "cannot open " << in << "\n";
        return 1;
    }
    const std::string   text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const ProfileStatus status = g_codec.parse(text.data(), text.size(), g_data);
    if (status != ProfileStatus::Ok) {
        err << in << ": " << describe(status) << "\n";
        return 1;
    }
    if (!BinaryCodec::valid(g_data)) {
        err << in << ": the profile cannot be stored in the binary format\n";
        return 1;
    }
    return write_atomically(out_path, err, [&](std::ostream& f) {
               FileSink sink(f);
               return BinaryCodec::write(g_data, sink);
           })
               ? 0
               : 1;
}

int check(const std::string& in, std::ostream& out, std::ostream& err) {
    if (!read_binary(in, err)) return 1;
    int tracks = 0;
    for (int l = 0; l < g_data.loop_count; ++l) tracks += g_data.loops[l].track_count;
    std::error_code ec;
    const auto      size = fs::file_size(in, ec);
    out << in << ": OK\n"
        << "  name:    " << g_data.name << "\n"
        << "  samples: " << static_cast<int>(g_data.sample_count) << "\n"
        << "  loops:   " << static_cast<int>(g_data.loop_count) << "\n"
        << "  tracks:  " << tracks << "\n"
        << "  size:    " << (ec ? 0 : static_cast<unsigned long long>(size)) << " bytes\n";
    return 0;
}

}  // namespace

int run(int argc, const char* const* argv, std::ostream& out, std::ostream& err) {
    if (argc < 2) return usage(err);
    const std::string command = argv[1];

    if (command == "bin2json" && (argc == 3 || argc == 4)) {
        const std::string out_path = argc == 4 ? argv[3] : "";
        return bin2json(argv[2], argc == 4 ? &out_path : nullptr, out, err);
    }
    if (command == "json2bin" && argc == 4) return json2bin(argv[2], argv[3], err);
    if (command == "check" && argc == 3) return check(argv[2], out, err);
    return usage(err);
}

}  // namespace profile_tool
