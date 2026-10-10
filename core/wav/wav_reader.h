#pragma once

#include <cstdint>

#include "wav_header.h"

// Reading the sound data of a WAV file in small pieces, converted on the fly to 16-bit and written where the caller says
// (the sample arena on the Teensy, a vector on the PC). The file is never held as a whole in memory: each call reads a
// few kilobytes into a small buffer on the stack, converts them, and goes on. Portable, no allocation.
//
// The output is always 16-bit, interleaved (L, R, L, R... for stereo), whatever the file holds.
namespace wav {

// Converts the raw bits of a float32 sample (-1..1) to 16-bit: rounded to the nearest, clipped at the ends, and 0 for
// a value that is not a number. 1.0 gives 32767, -1.0 gives -32767.
int16_t float_bits_to_int16(uint32_t bits);

class SampleReader {
public:
    // `info` comes from read_header and must have been accepted (status Ok); anything else gives a reader that
    // delivers nothing and reports failed().
    SampleReader(FileReader& file, const Info& info);

    // Frames not yet delivered.
    uint32_t frames_left() const { return left_; }

    // Converts the next frames into `dst` (room for max_frames * channels samples) and returns how many frames were
    // written: max_frames, or fewer at the end of the data. Returns 0 once everything was delivered.
    uint32_t read(int16_t* dst, uint32_t max_frames);

    // The file stopped answering before the frames announced by the header were all read (the file shrank, the card
    // was removed...). The frames delivered before that are good; the reader delivers nothing more.
    bool failed() const { return failed_; }

private:
    FileReader& file_;
    Info        info_;
    uint32_t    next_;  // index of the next frame to deliver
    uint32_t    left_;
    bool        failed_;
};

}  // namespace wav
