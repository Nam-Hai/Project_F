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

}  // namespace dsp
