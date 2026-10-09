#include "sample_player.h"

void SamplePlayer::set_sample(const int16_t* data, uint32_t length) {
    active_ = false;
    data_   = data;
    length_ = length;
}

void SamplePlayer::set_pitch(float ratio) {
    if (ratio < MIN_PITCH) ratio = MIN_PITCH;
    if (ratio > MAX_PITCH) ratio = MAX_PITCH;
    pitch_ = ratio;
    update_effective();
}

void SamplePlayer::update_effective() {
    float v = pitch_ * note_ratio_;
    if (v < MIN_PITCH) v = MIN_PITCH;
    if (v > MAX_PITCH) v = MAX_PITCH;
    effective_ = v;
}

void SamplePlayer::trigger(float velocity) {
    trigger_pitched(velocity, 1.0f);
}

void SamplePlayer::trigger_pitched(float velocity, float pitch_ratio) {
    if (!data_ || length_ == 0) return;
    note_ratio_ = pitch_ratio;
    update_effective();
    active_ = true;
    index_  = 0;
    frac_   = 0.0f;
    gain_   = velocity;
}

void SamplePlayer::render(float* out, int n) {
    if (!active_) return;

    constexpr float TO_FLOAT = 1.0f / 32768.0f;

    for (int i = 0; i < n; ++i) {
        const float s0 = data_[index_] * TO_FLOAT;
        // Après le dernier échantillon, on interpole vers 0 pour éviter un clic.
        const float s1 = (index_ + 1 < length_) ? data_[index_ + 1] * TO_FLOAT : 0.0f;

        out[i] += gain_ * (s0 + frac_ * (s1 - s0));

        // Index entier et fraction séparés : pas de perte de précision sur les longs samples.
        frac_ += effective_;
        const uint32_t step = static_cast<uint32_t>(frac_);
        frac_ -= static_cast<float>(step);
        index_ += step;

        if (index_ >= length_) {
            active_ = false;
            return;
        }
    }
}
