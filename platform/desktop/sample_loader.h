#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Charge un WAV PCM 16 bits, mono, 44,1 kHz (hors callback audio).
// Retourne false et remplit `error` si le format n'est pas celui attendu.
bool load_wav_mono16(const char* path, std::vector<int16_t>& out, std::string& error);
