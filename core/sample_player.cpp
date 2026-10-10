#include "sample_player.h"

#include "../config.h"

void SamplePlayer::set_sample(const int16_t* data, uint32_t length, int channels) {
    active_   = false;
    data_     = data;
    length_   = length;
    channels_ = channels == 2 ? 2 : 1;
}

void SamplePlayer::set_stereo_width(int width) {
    if (width < cfg::WIDTH_MIN) width = cfg::WIDTH_MIN;
    if (width > cfg::WIDTH_MAX) width = cfg::WIDTH_MAX;
    const float w = 1.0f + static_cast<float>(width) / 100.0f;  // 0 = mono, 1 = inchangé, 2 = côtés doublés
    fold_ll_ = fold_rr_ = (1.0f + w) * 0.5f;
    fold_lr_ = fold_rl_ = (1.0f - w) * 0.5f;
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

// Image courante, interpolée linéairement. Après la dernière image, on interpole vers 0 pour éviter un clic.
// La lecture avance ensuite du pitch effectif et s'arrête à la fin du sample.
void SamplePlayer::next_frame(float& left, float& right) {
    constexpr float TO_FLOAT = 1.0f / 32768.0f;

    const bool     more = index_ + 1 < length_;
    const uint32_t i0   = index_ * static_cast<uint32_t>(channels_);
    const float    l0   = data_[i0] * TO_FLOAT;
    const float    l1   = more ? data_[i0 + static_cast<uint32_t>(channels_)] * TO_FLOAT : 0.0f;
    left                = l0 + frac_ * (l1 - l0);
    if (channels_ == 2) {
        const float r0 = data_[i0 + 1] * TO_FLOAT;
        const float r1 = more ? data_[i0 + 3] * TO_FLOAT : 0.0f;
        right          = r0 + frac_ * (r1 - r0);
    } else {
        right = left;
    }

    // Index entier et fraction séparés : pas de perte de précision sur les longs samples.
    frac_ += effective_;
    const uint32_t step = static_cast<uint32_t>(frac_);
    frac_ -= static_cast<float>(step);
    index_ += step;
    if (index_ >= length_) active_ = false;
}

void SamplePlayer::render(float* out, int n) {
    for (int i = 0; i < n && active_; ++i) {
        float l, r;
        next_frame(l, r);
        out[i] += gain_ * (channels_ == 2 ? 0.5f * (l + r) : l);
    }
}

void SamplePlayer::render_stereo(float* left, float* right, int n) {
    for (int i = 0; i < n && active_; ++i) {
        float l, r;
        next_frame(l, r);
        const float folded_l = fold_ll_ * l + fold_lr_ * r;
        const float folded_r = fold_rl_ * l + fold_rr_ * r;
        left[i] += gain_ * pan_left_ * folded_l;
        right[i] += gain_ * pan_right_ * folded_r;
    }
}
