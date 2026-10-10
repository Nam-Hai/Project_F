#pragma once

#include <cstdint>

#include "voice.h"

// Lecteur de sample 16 bits (44,1 kHz), mono ou stéréo entrelacé (G, D, G, D...), déjà en RAM, avec pitch
// par interpolation linéaire. `length` est un nombre d'images (une image = un échantillon par canal).
// Ne possède pas les données : l'appelant garantit qu'elles survivent au lecteur
// (tableau statique sur Teensy, buffer chargé avant le démarrage audio sur PC).
class SamplePlayer : public Voice {
public:
    void set_sample(const int16_t* data, uint32_t length, int channels = 1);

    // Pitch de la piste : 1.0 = vitesse normale, 0.5 = une octave plus bas, 2.0 = une octave plus haut.
    void set_pitch(float ratio);

    void trigger(float velocity) override;

    // Note avec son propre rapport de pitch, multiplié par celui de la piste (borné à 0,05 - 8).
    void trigger_pitched(float velocity, float pitch_ratio) override;

    // Largeur stéréo de la piste (-100..100), appliquée à chaque image avant la balance de la note, modifiable en
    // direct. Le signal est séparé en milieu M = (G + D) / 2 et côtés S = (G - D) / 2, puis G = M + w S et
    // D = M - w S avec w = 1 + v / 100 : -100 = mono (w = 0, les deux canaux deviennent la moyenne), 0 = inchangé
    // (w = 1), +100 = côtés doublés (w = 2). Sans effet sur un sample mono (pas de côtés).
    void set_stereo_width(int width);

    // Mono : un sample stéréo est mélangé (moyenne des canaux), sans pan.
    void render(float* out, int n) override;

    // Stéréo : un sample mono est placé selon le pan de la note ; un sample stéréo garde son image, le pan agit
    // comme une balance.
    void render_stereo(float* left, float* right, int n) override;

    bool active() const { return active_; }

private:
    void update_effective();
    void next_frame(float& left, float& right);  // image courante interpolée ; avance la lecture

    static constexpr float MIN_PITCH = 0.05f;
    static constexpr float MAX_PITCH = 8.0f;

    const int16_t* data_   = nullptr;
    uint32_t       length_ = 0;
    int            channels_ = 1;
    // Matrice de la largeur : G' = ll G + lr D ; D' = rl G + rr D (identité à 0).
    float          fold_ll_ = 1.0f, fold_lr_ = 0.0f, fold_rl_ = 0.0f, fold_rr_ = 1.0f;
    float          pitch_  = 1.0f;  // pitch de la piste
    float          note_ratio_ = 1.0f;  // rapport propre à la note en cours
    float          effective_  = 1.0f;  // pitch réellement utilisé : piste x note, borné

    bool     active_ = false;
    uint32_t index_  = 0;     // échantillon entier courant
    float    frac_   = 0.0f;  // partie fractionnaire, dans [0, 1[
    float    gain_   = 0.0f;
};
