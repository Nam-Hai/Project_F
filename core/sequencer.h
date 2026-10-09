#pragma once

#include <cstdint>

#include "../config.h"

// Grille de pas : on[piste][pas] != 0 -> trig.
struct Pattern {
    uint8_t on[cfg::MAX_TRACKS][cfg::MAX_STEPS] = {};
};

// Horloge de pas pilotée par le compteur d'échantillons (pas par un timer).
// Un pas = une double croche (1/16) : durée = SR * 60 / BPM / 4 échantillons, fractionnaire.
// Le compte à rebours garde le reste fractionnaire, donc aucune dérive à long terme.
class Sequencer {
public:
    explicit Sequencer(float sample_rate) : sample_rate_(sample_rate) {}

    void  set_bpm(float bpm);
    float bpm() const { return bpm_; }

    void play();  // repart du pas 0, le premier trig tombe immédiatement
    void stop();
    bool playing() const { return playing_; }

    Pattern&       pattern() { return pattern_; }
    const Pattern& pattern() const { return pattern_; }

    // Nombre d'échantillons à rendre avant le prochain pas (0 = dû maintenant, INT32_MAX = à l'arrêt).
    int32_t frames_until_due() const;

    bool due() const { return playing_ && countdown_ <= 0.0; }

    // Consomme le pas dû : retourne son indice et programme le suivant.
    int fire();

    void advance(int frames) {
        if (playing_) countdown_ -= frames;
    }

private:
    double step_length() const { return static_cast<double>(sample_rate_) * 60.0 / bpm_ / 4.0; }

    float   sample_rate_;
    float   bpm_       = 120.0f;
    bool    playing_   = false;
    int     step_      = 0;
    double  countdown_ = 0.0;
    Pattern pattern_;
};
