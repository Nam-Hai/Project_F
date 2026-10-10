#pragma once

#include "dsp_utils.h"

// Interface commune des voies percussives (synthés et lecteur de samples).
class Voice {
public:
    virtual ~Voice() = default;

    virtual void trigger(float velocity) = 0;

    // Déclenche avec un rapport de pitch propre à cette note (1.0 = inchangé), qui se multiplie
    // avec le pitch de la voix. Par défaut le pitch de la note est ignoré (voix sans pitch).
    virtual void trigger_pitched(float velocity, float pitch_ratio) {
        (void)pitch_ratio;
        trigger(velocity);
    }

    // Note avec pitch et balance (-50..50, fractionnaire possible) : fixée pour toute la durée de la note.
    virtual void trigger_panned(float velocity, float pitch_ratio, float pan) {
        set_pan(pan);
        trigger_pitched(velocity, pitch_ratio);
    }

    // Ajoute `n` échantillons mono dans `out` (accumulation, pas d'écrasement).
    virtual void render(float* out, int n) = 0;

    // Ajoute `n` échantillons stéréo dans `left` et `right` (accumulation). Par défaut : le signal mono placé
    // selon le pan de la note.
    virtual void render_stereo(float* left, float* right, int n) {
        float tmp[64];
        for (int done = 0; done < n;) {
            const int m = n - done < 64 ? n - done : 64;
            for (int i = 0; i < m; ++i) tmp[i] = 0.0f;
            render(tmp, m);
            for (int i = 0; i < m; ++i) {
                left[done + i] += tmp[i] * pan_left_;
                right[done + i] += tmp[i] * pan_right_;
            }
            done += m;
        }
    }

protected:
    void set_pan(float pan) { dsp::pan_gains(pan, pan_left_, pan_right_); }

    float pan_left_  = 1.0f;
    float pan_right_ = 1.0f;
};
