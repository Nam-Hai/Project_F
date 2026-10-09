#pragma once

#include <cstdint>

#include "voice.h"

// Lecteur de sample mono 16 bits (44,1 kHz) déjà en RAM, avec pitch par interpolation linéaire.
// Ne possède pas les données : l'appelant garantit qu'elles survivent au lecteur
// (tableau statique sur Teensy, buffer chargé avant le démarrage audio sur PC).
class SamplePlayer : public Voice {
public:
    void set_sample(const int16_t* data, uint32_t length);

    // Pitch de la piste : 1.0 = vitesse normale, 0.5 = une octave plus bas, 2.0 = une octave plus haut.
    void set_pitch(float ratio);

    void trigger(float velocity) override;

    // Note avec son propre rapport de pitch, multiplié par celui de la piste (borné à 0,05 - 8).
    void trigger_pitched(float velocity, float pitch_ratio) override;

    void render(float* out, int n) override;

    bool active() const { return active_; }

private:
    void update_effective();

    static constexpr float MIN_PITCH = 0.05f;
    static constexpr float MAX_PITCH = 8.0f;

    const int16_t* data_   = nullptr;
    uint32_t       length_ = 0;
    float          pitch_  = 1.0f;  // pitch de la piste
    float          note_ratio_ = 1.0f;  // rapport propre à la note en cours
    float          effective_  = 1.0f;  // pitch réellement utilisé : piste x note, borné

    bool     active_ = false;
    uint32_t index_  = 0;     // échantillon entier courant
    float    frac_   = 0.0f;  // partie fractionnaire, dans [0, 1[
    float    gain_   = 0.0f;
};
