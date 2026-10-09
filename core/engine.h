#pragma once

#include "../config.h"
#include "command.h"
#include "engine_state.h"
#include "sequencer.h"
#include "synth_kick.h"
#include "voice.h"

// Moteur audio portable : aucune allocation ni dépendance OS dans render().
// Piste 0 = kick synthétique par défaut ; set_voice() remplace la voix d'une piste.
class Engine {
public:
    Engine();

    // Branche une voix sur une piste (remplace la précédente). L'appelant garde la propriété
    // de la voix, qui doit survivre à l'Engine. À appeler avant le démarrage audio.
    void set_voice(int track, Voice* voice);

    // Envoie une commande au moteur (thread principal / UI). false si la file est pleine.
    // Les commandes sont appliquées au début du prochain render().
    bool push(const Command& cmd) { return commands_.push(cmd); }

    // État lisible depuis n'importe quel thread.
    const EngineState& state() const { return state_; }

    // Déclenche une piste immédiatement. Appel réservé au thread audio (ou hors audio actif).
    void trigger(int track, float velocity = 1.0f);

    // Produit `frames` échantillons stéréo (planaires), taille quelconque. Thread audio.
    void render(float* left, float* right, int frames);

private:
    void apply(const Command& cmd);
    void fire_step();

    // Rend des échantillons sans s'occuper du séquenceur, par blocs d'au plus cfg::BLOCK.
    void render_audio(float* left, float* right, int frames);
    void render_block(float* left, float* right, int frames);

    SynthKick  kick_;
    Sequencer  seq_;
    EngineState state_;
    SpscQueue<Command, 64> commands_;

    Voice* voices_[cfg::MAX_TRACKS] = {};
    float  gains_[cfg::MAX_TRACKS];
    float  scratch_[cfg::BLOCK];
};
