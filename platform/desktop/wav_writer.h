#pragma once

#include <cstddef>

// Écrit un WAV PCM 16 bits stéréo à partir de buffers float planaires (valeurs clampées à [-1, 1]).
bool write_wav_stereo16(const char* path, const float* left, const float* right,
                        std::size_t frames, int sample_rate);
