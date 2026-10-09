#pragma once

#include "voice.h"

// Kick : sinus à fréquence décroissante (pitch bend exponentiel) x enveloppe d'amplitude exponentielle.
class SynthKick : public Voice {
public:
    struct Params {
        float f_start   = 160.0f;  // Hz
        float f_target  = 50.0f;   // Hz
        float tau_pitch = 0.04f;   // s
        float tau_amp   = 0.15f;   // s
    };

    explicit SynthKick(float sample_rate);
    SynthKick(float sample_rate, const Params& params);

    void trigger(float velocity) override;
    void render(float* out, int n) override;

    bool active() const { return active_; }

private:
    static constexpr float SILENCE = 1e-4f;

    Params p_;
    float  sample_rate_;
    float  pitch_alpha_;
    float  amp_alpha_;

    bool  active_    = false;
    float phase_     = 0.0f;
    float pitch_env_ = 0.0f;  // (f - f_target) / (f_start - f_target), décroît de 1 vers 0
    float amp_       = 0.0f;
};
