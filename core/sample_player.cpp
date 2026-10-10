#include "sample_player.h"

void SamplePlayer::set_sample(const int16_t* data, uint32_t length, int channels) {
    head_.active = false;
    for (Playback& t : tails_) t.active = false;  // les queues liraient l'ancien buffer, qui va être rendu
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

void SamplePlayer::set_envelope(int attack_ms, int decay_ms, int sustain_percent, int release_ms) {
    auto clamp_ms = [](int ms) { return ms < 0 ? 0 : ms > cfg::ENV_MS_MAX ? cfg::ENV_MS_MAX : ms; };
    if (sustain_percent < 0) sustain_percent = 0;
    if (sustain_percent > 100) sustain_percent = 100;
    constexpr float PER_MS = cfg::SAMPLE_RATE / 1000.0f;
    attack_n_  = static_cast<float>(clamp_ms(attack_ms)) * PER_MS;
    decay_n_   = static_cast<float>(clamp_ms(decay_ms)) * PER_MS;
    sustain_   = static_cast<float>(sustain_percent) / 100.0f;
    release_n_ = static_cast<float>(clamp_ms(release_ms)) * PER_MS;
}

float SamplePlayer::effective_for(const Playback& p) const {
    float v = pitch_ * p.note_ratio;
    if (v < MIN_PITCH) v = MIN_PITCH;
    if (v > MAX_PITCH) v = MAX_PITCH;
    return v;
}

void SamplePlayer::set_pitch(float ratio) {
    if (ratio < MIN_PITCH) ratio = MIN_PITCH;
    if (ratio > MAX_PITCH) ratio = MAX_PITCH;
    pitch_ = ratio;
    head_.effective = effective_for(head_);  // la note en cours suit le pitch de la piste ; les queues gardent le leur
}

bool SamplePlayer::active() const {
    if (head_.active) return true;
    for (const Playback& t : tails_)
        if (t.active) return true;
    return false;
}

void SamplePlayer::trigger(float velocity) {
    trigger_pitched(velocity, 1.0f);
}

// Passe une lecture en Release : le niveau atteint descend jusqu'à 0 en release_n_ échantillons.
void SamplePlayer::start_release(Playback& p) const {
    p.stage        = Stage::Release;
    p.release_step = p.level / release_n_;
}

// Début du Decay (niveau 1) : sans decay, ou avec un sustain de 100 %, le niveau tenu est atteint tout de suite.
void SamplePlayer::enter_decay(Playback& p) const {
    p.level = 1.0f;
    p.stage = Stage::Decay;
    if (sustain_ >= 1.0f) {
        p.stage = Stage::Sustain;
    } else if (decay_n_ < 1.0f) {
        p.level = sustain_;
        p.stage = Stage::Sustain;
    }
}

void SamplePlayer::trigger_pitched(float velocity, float pitch_ratio) {
    if (!data_ || length_ == 0) return;

    // La note précédente, encore en cours et déjà audible, finit en Release (sauf Release nul : coupée net, comme avant).
    if (head_.active && head_.rendered && release_n_ >= 1.0f && head_.level > 0.0f) {
        Playback* slot = nullptr;
        for (Playback& t : tails_)
            if (!t.active) {
                slot = &t;
                break;
            }
        if (!slot) {  // toutes les queues sont prises : on reprend la plus basse (la plus ancienne, donc presque éteinte)
            slot = &tails_[0];
            for (Playback& t : tails_)
                if (t.level < slot->level) slot = &t;
        }
        *slot = head_;
        start_release(*slot);
    }

    Playback& p  = head_;
    p            = Playback{};
    p.active     = true;
    p.note_ratio = pitch_ratio;
    p.effective  = effective_for(p);
    p.gain       = velocity;
    p.pan_left   = pan_left_;
    p.pan_right  = pan_right_;
    p.stage      = Stage::Attack;
    p.level      = 0.0f;
    if (attack_n_ < 1.0f) enter_decay(p);
}

// Image courante, interpolée linéairement, multipliée par l'enveloppe ; la lecture avance du pitch effectif.
// Après la dernière image, on interpole vers 0 pour éviter un clic. Retourne false quand la lecture est finie.
bool SamplePlayer::next_frame(Playback& p, float& left, float& right) {
    constexpr float TO_FLOAT = 1.0f / 32768.0f;

    const bool     more = p.index + 1 < length_;
    const uint32_t i0   = p.index * static_cast<uint32_t>(channels_);
    const float    l0   = data_[i0] * TO_FLOAT;
    const float    l1   = more ? data_[i0 + static_cast<uint32_t>(channels_)] * TO_FLOAT : 0.0f;
    left                = l0 + p.frac * (l1 - l0);
    if (channels_ == 2) {
        const float r0 = data_[i0 + 1] * TO_FLOAT;
        const float r1 = more ? data_[i0 + 3] * TO_FLOAT : 0.0f;
        right          = r0 + p.frac * (r1 - r0);
    } else {
        right = left;
    }

    // Enveloppe : le niveau de cet échantillon, puis un pas vers le suivant.
    p.rendered        = true;
    const float level = p.level;
    switch (p.stage) {
        case Stage::Attack:
            p.level += attack_n_ >= 1.0f ? 1.0f / attack_n_ : 1.0f;  // (le réglage a pu passer à 0 en cours de route)
            if (p.level >= 1.0f) enter_decay(p);
            break;
        case Stage::Decay:
            if (decay_n_ >= 1.0f && sustain_ < p.level) {
                p.level -= (1.0f - sustain_) / decay_n_;
                if (p.level <= sustain_) {
                    p.level = sustain_;
                    p.stage = Stage::Sustain;
                }
            } else {
                p.level = sustain_ < p.level ? sustain_ : p.level;
                p.stage = Stage::Sustain;
            }
            break;
        case Stage::Sustain:
            break;
        case Stage::Release:
            p.level -= p.release_step;
            if (p.level <= 0.0f) {
                p.level  = 0.0f;
                p.active = false;
            }
            break;
    }
    left *= level;
    right *= level;

    // Index entier et fraction séparés : pas de perte de précision sur les longs samples.
    p.frac += p.effective;
    const uint32_t step = static_cast<uint32_t>(p.frac);
    p.frac -= static_cast<float>(step);
    p.index += step;
    if (p.index >= length_) p.active = false;
    return p.active;
}

// Ajoute `n` échantillons d'une lecture dans `left` / `right` (stéréo, avec pan et largeur) ou dans `mono`.
void SamplePlayer::mix(Playback& p, float* left, float* right, float* mono, int n) {
    for (int i = 0; i < n && p.active; ++i) {
        float l, r;
        next_frame(p, l, r);
        if (mono) {
            mono[i] += p.gain * (channels_ == 2 ? 0.5f * (l + r) : l);
        } else {
            const float folded_l = fold_ll_ * l + fold_lr_ * r;
            const float folded_r = fold_rl_ * l + fold_rr_ * r;
            left[i] += p.gain * p.pan_left * folded_l;
            right[i] += p.gain * p.pan_right * folded_r;
        }
    }
}

void SamplePlayer::render(float* out, int n) {
    mix(head_, nullptr, nullptr, out, n);
    for (Playback& t : tails_) mix(t, nullptr, nullptr, out, n);
}

void SamplePlayer::render_stereo(float* left, float* right, int n) {
    mix(head_, left, right, nullptr, n);
    for (Playback& t : tails_) mix(t, left, right, nullptr, n);
}
