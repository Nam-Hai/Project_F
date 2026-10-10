#pragma once

#include <cstdint>

#include "byte_stream.h"
#include "profile_data.h"
#include "profile_status.h"

// Binary profile format (the one stored on the SD card and used by the emulator; the JSON codec only serves the PC tool).
//
//   file    = "DMPB" version(1 byte) record* END
//   record  = tag(1 byte) length(1 byte) payload(length bytes)
//
// Everything is little-endian. The records are a FLAT stream, with no nesting: a LOOP record opens a loop, a TRACK
// record opens a track of that loop, and every track field that follows applies to the current track. The writer
// therefore never needs to know a length in advance. A field equal to its default value is not written (exactly like
// the optional keys of the JSON format). A reader skips any tag it does not know, using the length, and accepts a
// known record whose payload is longer than expected (future versions may append fields), so files written by a
// newer program stay readable. The END record carries the CRC-32 of every byte that precedes its payload.
//
// Order: PROFILE_NAME and SAMPLE records come first (the table of samples must exist before a track refers to it),
// then LOOP, TRACK and the fields of each track.
namespace profile {

constexpr uint8_t BINARY_FORMAT_VERSION = 1;

namespace tag {

constexpr uint8_t PROFILE_NAME = 0x01;  // payload: the name (no terminator)
constexpr uint8_t SAMPLE       = 0x02;  // payload: a file name (no terminator), appended to the table of samples
constexpr uint8_t LOOP         = 0x10;  // payload: bpm (float32)
constexpr uint8_t TRACK        = 0x20;  // payload: sample index (uint8)
constexpr uint8_t GAIN         = 0x21;  // float32, default 1.0
constexpr uint8_t PITCH        = 0x22;  // float32, default 1.0
constexpr uint8_t PATTERN      = 0x23;  // uint16, default 0
constexpr uint8_t VELOCITY     = 0x24;  // 16 x uint8 (1..127), default all 127
constexpr uint8_t STEP_PITCH   = 0x25;  // 16 x int8 (-12..12), default all 0
constexpr uint8_t STEP_PAN     = 0x26;  // 16 x int8 (-50..50), default all 0
constexpr uint8_t STEP_OFFSET  = 0x27;  // 16 x int8 (-64..63), default all 0
constexpr uint8_t EXTRA_HIT    = 0x28;  // step(uint8), offset(int8), velocity(uint8), pitch(int8), pan(int8); one per extra note
constexpr uint8_t QUANTIZE     = 0x29;  // uint8 (0..100), default 0
constexpr uint8_t WIDTH        = 0x2A;  // int8 (-100..100), default 0
constexpr uint8_t BALANCE      = 0x2B;  // int8 (-50..50), default 0
constexpr uint8_t ATTACK       = 0x2C;  // uint16 ms (0..2000), default 0
constexpr uint8_t DECAY        = 0x2D;  // uint16 ms (0..2000), default 0
constexpr uint8_t SUSTAIN      = 0x2E;  // uint8 % (0..100), default 100
constexpr uint8_t RELEASE      = 0x2F;  // uint16 ms (0..2000), default cfg::ENV_DEFAULT_RELEASE_MS
constexpr uint8_t END          = 0xFF;  // payload: CRC-32 (uint32); always the last record

}  // namespace tag

// Stateless: nothing to keep between calls and no big temporary (a few hundred bytes of stack).
class BinaryCodec {
public:
    // Reads a profile. On failure `out` is invalid: read into a working object, not into the one in use.
    static ProfileStatus read(ByteSource& source, ProfileData& out);

    // Writes the profile. false if it is invalid (see valid()) or if the sink fails. Nothing is written when the
    // profile is invalid; with a failing sink the output is incomplete (the caller writes to a temporary file).
    static bool write(const ProfileData& profile, ByteSink& sink);

    // Same checks as the reader applies: counts, indexes into the table of samples, ranges, finite numbers and
    // names without control characters.
    static bool valid(const ProfileData& profile);
};

}  // namespace profile
