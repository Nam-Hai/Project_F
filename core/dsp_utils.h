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

}  // namespace dsp
