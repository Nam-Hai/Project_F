#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Charge un WAV (hors callback audio) : PCM 16 bits ou flottant 32 bits, 1 ou 2 canaux, 44,1 kHz.
// Le résultat est converti en 16 bits, entrelacé (G, D, G, D...) pour la stéréo ; `channels` vaut 1 ou 2.
// Retourne false et remplit `error` si le format n'est pas pris en charge.
bool load_wav(const char* path, std::vector<int16_t>& out, int& channels, std::string& error);

// Passe un tampon entrelacé à `channels` canaux en mono (moyenne des canaux). Sans effet si channels == 1.
void downmix_to_mono(std::vector<int16_t>& samples, int channels);

// load_wav suivi de downmix_to_mono : provisoire, tant que le moteur ne lit pas la stéréo (plan_stereo.md, S2).
bool load_wav_mono(const char* path, std::vector<int16_t>& out, std::string& error);
