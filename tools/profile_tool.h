#pragma once

#include <iosfwd>

// Command-line tool for the binary profiles (the format stored on the SD card and used by the emulator):
//
//   profile_tool bin2json <profile.dmp> [out.json]   binary profile -> readable JSON (standard output if no file)
//   profile_tool json2bin <profile.json> <out.dmp>   JSON profile -> binary profile
//   profile_tool check <profile.dmp>                 checks the file (format, CRC, bounds) and prints a summary
//
// Exit codes: 0 success, 1 the operation failed (message on `err`), 2 wrong usage.
// The output file is written to "<file>.tmp" and renamed, so a failure never leaves a half-written or overwritten file.
namespace profile_tool {

int run(int argc, const char* const* argv, std::ostream& out, std::ostream& err);

}  // namespace profile_tool
