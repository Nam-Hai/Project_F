#pragma once

#include <string>

#include "../../core/profile/profile_store.h"

// Profile in a binary file on disk (the format of core/profile/binary_codec.h, the same one the Teensy reads and writes).
//
// Safe write: the new content is written to "<file>.tmp", the old version is copied to "<file>.bak", then the .tmp
// replaces the file (rename). A failure while writing therefore always leaves a complete version, and a damaged file
// is never overwritten without having been set aside. (The recovery at load time, which falls back to the .tmp and
// the .bak, is the job of the card code of the Teensy, see backlog B-060.)
class FileProfileStore : public profile::ProfileStore {
public:
    explicit FileProfileStore(std::string path) : path_(std::move(path)) {}

    profile::LoadResult load(profile::ProfileData& out) override;
    bool                save(const profile::ProfileData& profile) override;
    const char*         last_error() const override { return error_.c_str(); }

private:
    bool fail(const std::string& message);

    std::string path_;
    std::string error_;
};
