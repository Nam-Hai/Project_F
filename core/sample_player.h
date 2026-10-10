#pragma once

#include <cstdint>

#include "../config.h"
#include "voice.h"

// Lecteur de sample 16 bits (44,1 kHz), mono ou stéréo entrelacé (G, D, G, D...), déjà en RAM, avec pitch
// par interpolation linéaire. `length` est un nombre d'images (une image = un échantillon par canal).
// Ne possède pas les données : l'appelant garantit qu'elles survivent au lecteur
// (tableau statique sur Teensy, buffer chargé avant le démarrage audio sur PC).
//
// Enveloppe ADSR de la piste (voir set_envelope) : chaque note a la sienne. Une nouvelle note sur la piste
// remplace la précédente, mais celle-ci passe en Release au lieu d'être coupée net : elle finit de descendre
// pendant que la nouvelle démarre (jusqu'à MAX_TAILS queues en même temps).
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

    // Enveloppe ADSR : Attack (0 -> 1), Decay (1 -> Sustain), Sustain (niveau 0..100 %, tenu jusqu'à la fin du
    // sample), Release (du niveau atteint à 0, quand une nouvelle note remplace celle-ci). Temps en millisecondes
    // (0..ENV_MS_MAX ; 0 = immédiat, et un Release de 0 coupe l'ancienne note net, comme avant). Rampes linéaires.
    // Défaut : A 0, D 0, S 100 %, R ENV_DEFAULT_RELEASE_MS : le son d'une note seule est inchangé.
    void set_envelope(int attack_ms, int decay_ms, int sustain_percent, int release_ms);

    // Mono : un sample stéréo est mélangé (moyenne des canaux), sans pan.
    void render(float* out, int n) override;

    // Stéréo : un sample mono est placé selon le pan de la note ; un sample stéréo garde son image, le pan agit
    // comme une balance.
    void render_stereo(float* left, float* right, int n) override;

    // Une note (ou une queue en Release) sonne encore.
    bool active() const;

    static constexpr int MAX_TAILS = 4;

private:
    enum class Stage : uint8_t { Attack, Decay, Sustain, Release };

    // Une lecture : position, gain, balance, pitch et enveloppe d'une note.
    struct Playback {
        bool     active    = false;
        bool     rendered  = false; // au moins un échantillon a été produit
        uint32_t index     = 0;     // échantillon entier courant
        float    frac      = 0.0f;  // partie fractionnaire, dans [0, 1[
        float    gain      = 0.0f;
        float    note_ratio = 1.0f; // rapport de pitch propre à la note
        float    effective = 1.0f;  // pitch réellement utilisé : piste x note, borné
        float    pan_left  = 1.0f;
        float    pan_right = 1.0f;
        Stage    stage     = Stage::Sustain;
        float    level     = 1.0f;  // niveau de l'enveloppe, 0..1
        float    release_step = 0.0f;  // décrément par échantillon pendant le Release
    };

    float effective_for(const Playback& p) const;
    bool  next_frame(Playback& p, float& left, float& right);  // image interpolée, enveloppe appliquée ; false à la fin
    void  start_release(Playback& p) const;
    void  enter_decay(Playback& p) const;
    void  mix(Playback& p, float* left, float* right, float* mono, int n);

    static constexpr float MIN_PITCH = 0.05f;
    static constexpr float MAX_PITCH = 8.0f;

    const int16_t* data_   = nullptr;
    uint32_t       length_ = 0;
    int            channels_ = 1;
    // Matrice de la largeur : G' = ll G + lr D ; D' = rl G + rr D (identité à 0).
    float          fold_ll_ = 1.0f, fold_lr_ = 0.0f, fold_rl_ = 0.0f, fold_rr_ = 1.0f;
    float          pitch_  = 1.0f;  // pitch de la piste

    // Enveloppe, en échantillons (A, D, R) et en niveau (S).
    float attack_n_  = 0.0f;
    float decay_n_   = 0.0f;
    float sustain_   = 1.0f;
    float release_n_ = cfg::ENV_DEFAULT_RELEASE_MS * cfg::SAMPLE_RATE / 1000.0f;

    Playback head_;               // la note en cours
    Playback tails_[MAX_TAILS];   // les notes remplacées, en Release
};
