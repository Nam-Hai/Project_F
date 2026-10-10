#pragma once

#include <cmath>

namespace dsp {

constexpr float PI     = 3.14159265358979f;
constexpr float TWO_PI = 2.0f * PI;

// Coefficient par échantillon d'une décroissance exponentielle de constante de temps tau (s).
inline float decay_coeff(float tau_seconds, float sample_rate) {
    return std::exp(-1.0f / (sample_rate * tau_seconds));
}

inline float soft_clip(float x) { return std::tanh(x); }

// Rapport de vitesse de lecture pour un décalage en demi-tons : 12 = une octave plus haut (x2).
inline float semitone_ratio(int semitones) { return std::exp2(static_cast<float>(semitones) / 12.0f); }

// Gains gauche / droite d'un pan (-50..50, borné) : balance linéaire, gain 1 des deux côtés au centre, donc un
// sample mono au centre sonne comme sans pan. Du côté opposé, le gain descend linéairement jusqu'à 0.
inline void pan_gains(float pan, float& left, float& right) {
    if (pan < -50.0f) pan = -50.0f;
    if (pan > 50.0f) pan = 50.0f;
    left  = pan > 0.0f ? 1.0f - pan / 50.0f : 1.0f;
    right = pan < 0.0f ? 1.0f + pan / 50.0f : 1.0f;
}

// Balance finale d'une note : `note` (-50..50) est une position entre la balance de la piste `track` (0 = elle la
// suit) et les extrémités (-50 tout à gauche, +50 tout à droite, quelle que soit la piste). Linéaire entre les deux.
inline float combine_balance(int track, int note) {
    if (track < -50) track = -50;
    if (track > 50) track = 50;
    if (note < -50) note = -50;
    if (note > 50) note = 50;
    const float t = static_cast<float>(track);
    const float n = static_cast<float>(note);
    return n < 0.0f ? t + n * (t + 50.0f) / 50.0f : t + n * (50.0f - t) / 50.0f;
}

}  // namespace dsp
