#pragma once

#include <cmath>

// Clic du pré-compte : une courte sinusoïde à décroissance exponentielle, plus aiguë sur le premier temps.
// Pas de sample : aucune allocation, aucun fichier.
class ClickVoice {
public:
    explicit ClickVoice(float sample_rate) : sample_rate_(sample_rate) {}

    void trigger(bool accent) {
        step_  = 2.0f * 3.14159265f * (accent ? 1600.0f : 1000.0f) / sample_rate_;
        phase_ = 0.0f;
        amp_   = 1.0f;
        left_  = static_cast<int>(sample_rate_ * 0.03f);  // 30 ms
    }

    bool active() const { return left_ > 0; }

    // Ajoute `n` échantillons dans `out`.
    void render(float* out, int n) {
        for (int i = 0; i < n && left_ > 0; ++i, --left_) {
            out[i] += GAIN * amp_ * std::sin(phase_);
            phase_ += step_;
            amp_ *= 0.9985f;
        }
    }

    static constexpr float GAIN = 0.5f;

private:
    float sample_rate_;
    float step_  = 0.0f;
    float phase_ = 0.0f;
    float amp_   = 0.0f;
    int   left_  = 0;
};
