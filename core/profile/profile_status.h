#pragma once

#include <cstdint>

namespace profile {

// Result of reading a profile, shared by the JSON codec (PC only) and the binary codec.
enum class ProfileStatus : uint8_t {
    Ok,
    Truncated,    // the data stops before the end (cut file)
    InvalidJson,  // invalid syntax or inconsistent structure (JSON only)
    NotAProfile,  // valid data, but not a profile of this application
    TooNew,       // written by a more recent version of the program
    TooBig,       // too many loops, tracks, samples or tokens for the fixed arrays
    BadField,     // wrong type, out of range, too long, malformed or misplaced field
    BadChecksum,  // the CRC32 of a binary profile does not match (damaged card or file)
};

// Message (in French, shown on the device screen) to display or log.
const char* describe(ProfileStatus status);

}  // namespace profile
