#include "sample_library.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

#include "sample_loader.h"

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

DesktopSampleLibrary::DesktopSampleLibrary(const std::string& directory, SampleBank& bank)
    : directory_(directory), bank_(bank) {
    refresh();
}

void DesktopSampleLibrary::refresh() {
    names_.clear();

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(directory_, ec)) {
        if (!entry.is_regular_file(ec)) continue;

        const std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        if (lower(entry.path().extension().string()) != ".wav") continue;

        // The format is checked right away, from the header alone (the sound data is not read): only usable files are offered.
        wav::Info   info;
        std::string error;
        if (!probe_wav(entry.path().string().c_str(), info, error) || info.frames == 0) {
            std::fprintf(stderr, "Sample ignoré : %s (%s)\n", name.c_str(), info.frames == 0 && error.empty() ? "aucune donnée" : error.c_str());
            continue;
        }
        names_.push_back(name);
    }

    std::sort(names_.begin(), names_.end(),
              [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
}

int DesktopSampleLibrary::find(const char* file) const {
    for (std::size_t i = 0; i < names_.size(); ++i)
        if (names_[i] == file) return static_cast<int>(i);
    return -1;
}

bool DesktopSampleLibrary::info(int index, uint32_t& frames, int& channels) const {
    if (index < 0 || index >= count()) return false;

    const std::string path = directory_ + "/" + names_[static_cast<std::size_t>(index)];
    wav::Info         header;
    std::string       error;
    if (!probe_wav(path.c_str(), header, error)) return false;  // header only: the sound data is not read
    frames   = header.frames;
    channels = header.channels;
    return true;
}

bool DesktopSampleLibrary::load(int index, const int16_t*& data, uint32_t& length) {
    if (index < 0 || index >= count()) return false;

    const std::string path = directory_ + "/" + names_[static_cast<std::size_t>(index)];
    std::string       error;
    if (!bank_.load(path.c_str(), data, length, error)) {
        std::fprintf(stderr, "%s : %s\n", path.c_str(), error.c_str());
        return false;
    }
    return true;
}
