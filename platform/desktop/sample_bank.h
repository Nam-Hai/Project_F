#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Possède les buffers de samples chargés pendant que le moteur les lit (côté PC ; sur Teensy,
// l'équivalent sera un tampon en RAM2 / PSRAM). Les pointeurs restent stables jusqu'à release().
//
// Cycle de vie : load() -> Command::SetTrackSample -> ... -> Engine::pop_released() -> release().
class SampleBank {
public:
    // Charge un WAV (mono ou stéréo, 16 bits ou flottant, 44,1 kHz). `length` est un nombre d'images ; un sample
    // stéréo est entrelacé (G, D, G, D...). Hors callback audio uniquement.
    bool load(const char* path, const int16_t*& data, uint32_t& length, std::string& error);

    // Nombre de canaux d'un buffer de la banque (1 pour un pointeur inconnu).
    int channels(const int16_t* data) const;

    // Libère un buffer rendu par le moteur. Ignore un pointeur inconnu.
    void release(const int16_t* data);

    std::size_t count() const { return buffers_.size(); }

private:
    struct Buffer {
        std::vector<int16_t> samples;
        int                  channels = 1;
    };
    std::vector<std::unique_ptr<Buffer>> buffers_;
};
