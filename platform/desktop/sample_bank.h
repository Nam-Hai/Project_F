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
    // Charge un WAV (mono, 16 bits, 44,1 kHz). Hors callback audio uniquement.
    bool load(const char* path, const int16_t*& data, uint32_t& length, std::string& error);

    // Libère un buffer rendu par le moteur. Ignore un pointeur inconnu.
    void release(const int16_t* data);

    std::size_t count() const { return buffers_.size(); }

private:
    std::vector<std::unique_ptr<std::vector<int16_t>>> buffers_;
};
