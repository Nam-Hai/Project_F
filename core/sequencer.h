#pragma once

#include <cstdint>

#include "../config.h"

// Une note supplémentaire d'un pas (roulement) : décalage, vélocité, pitch.
struct Hit {
    int8_t  offset   = 0;                 // en 1/128 de pas, -64..63
    uint8_t velocity = cfg::VELOCITY_MAX;
    int8_t  pitch    = 0;                 // demi-tons, -12..12
};

// Grille de pas : on[piste][pas] != 0 -> trig. Chaque pas a une note principale (vélocité 1..127, pitch en
// demi-tons -12..12, décalage `offset` en 1/128 de pas) et jusqu'à 3 notes supplémentaires. Ces valeurs
// sont conservées même quand le pas est éteint. Un pas allumé sans décalage ni note supplémentaire joue
// une seule note pile sur le pas. `quantize[piste]` (0..100 %) rapproche les décalages de la grille.
struct Pattern {
    uint8_t on[cfg::MAX_TRACKS][cfg::MAX_STEPS]       = {};
    uint8_t velocity[cfg::MAX_TRACKS][cfg::MAX_STEPS];
    int8_t  pitch[cfg::MAX_TRACKS][cfg::MAX_STEPS]    = {};
    int8_t  offset[cfg::MAX_TRACKS][cfg::MAX_STEPS]   = {};
    uint8_t extra_count[cfg::MAX_TRACKS][cfg::MAX_STEPS] = {};
    Hit     extra[cfg::MAX_TRACKS][cfg::MAX_STEPS][cfg::MAX_EXTRA_HITS];
    uint8_t quantize[cfg::MAX_TRACKS]                 = {};

    Pattern() {
        for (int t = 0; t < cfg::MAX_TRACKS; ++t) reset_track(t);
    }

    // Piste vide, valeurs par défaut (vélocité maximale, pitch 0, pile sur la grille, quantification 0).
    void reset_track(int t) {
        for (int s = 0; s < cfg::MAX_STEPS; ++s) {
            on[t][s]          = 0;
            velocity[t][s]    = static_cast<uint8_t>(cfg::VELOCITY_MAX);
            pitch[t][s]       = 0;
            offset[t][s]      = 0;
            extra_count[t][s] = 0;
            for (Hit& h : extra[t][s]) h = Hit{};
        }
        quantize[t] = 0;
    }
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

    // Retard sur la frontière du pas dû, en échantillons (<= 0) : l'instant exact du pas tombe entre
    // deux échantillons, et on le traite à l'échantillon suivant.
    double countdown() const { return countdown_; }

    // Durée du pas, en échantillons (fractionnaire).
    double step_length() const { return static_cast<double>(sample_rate_) * 60.0 / bpm_ / 4.0; }

    // Consomme le pas dû : retourne son indice et programme le suivant.
    int fire();

    void advance(int frames) {
        if (playing_) countdown_ -= frames;
    }

private:
    float   sample_rate_;
    float   bpm_       = 120.0f;
    bool    playing_   = false;
    int     step_      = 0;
    double  countdown_ = 0.0;
    Pattern pattern_;
};
