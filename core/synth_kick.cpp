#include "synth_kick.h"

#include "dsp_utils.h"

SynthKick::SynthKick(float sample_rate) : SynthKick(sample_rate, Params{}) {}

SynthKick::SynthKick(float sample_rate, const Params& params)
    : p_(params),
      sample_rate_(sample_rate),
      pitch_alpha_(dsp::decay_coeff(params.tau_pitch, sample_rate)),
      amp_alpha_(dsp::decay_coeff(params.tau_amp, sample_rate)) {}

void SynthKick::trigger(float velocity) {
    active_    = true;
    phase_     = 0.0f;
    pitch_env_ = 1.0f;
    amp_       = velocity;
}

void SynthKick::render(float* out, int n) {
    if (!active_) return;

    const float f_delta = p_.f_start - p_.f_target;

    for (int i = 0; i < n; ++i) {
        const float freq = p_.f_target + f_delta * pitch_env_;

        out[i] += amp_ * std::sin(phase_);

        phase_ += dsp::TWO_PI_F * freq / sample_rate_;
        if (phase_ >= dsp::TWO_PI_F) phase_ -= dsp::TWO_PI_F;

        pitch_env_ *= pitch_alpha_;
        amp_       *= amp_alpha_;

        if (amp_ < SILENCE) {
            active_ = false;
            return;
        }
    }
}
