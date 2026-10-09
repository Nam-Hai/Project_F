#pragma once

#include "../config.h"
#include "command.h"
#include "engine_state.h"
#include "sample_player.h"
#include "sequencer.h"
#include "synth_kick.h"
#include "voice.h"

// Moteur audio portable : aucune allocation ni dépendance OS dans render().
//
// Pistes : chaque piste peut recevoir un sample (Command::SetTrackSample) ; le moteur possède
// un SamplePlayer par piste. La piste 0 a un kick synthétique par défaut (en sommeil) tant qu'aucun
// sample ne la remplace.
//
// Propriété des buffers de samples : l'appelant fournit un pointeur via SetTrackSample et il
// reste valide tant que le moteur ne l'a pas rendu par pop_released() (remplacement ou
// suppression de la piste). L'appelant peut alors le libérer ou le réutiliser.
class Engine : public CommandSink {
public:
    Engine();

    // Branche une voix externe sur une piste (remplace la précédente). L'appelant garde la
    // propriété de la voix, qui doit survivre à l'Engine. À appeler avant le démarrage audio.
    void set_voice(int track, Voice* voice);

    // Envoie une commande au moteur (thread principal / UI). false si la file est pleine.
    // Les commandes sont appliquées au début du prochain render().
    bool push(const Command& cmd) override { return commands_.push(cmd); }

    // Récupère un buffer de sample que le moteur n'utilise plus (thread principal / UI).
    // À appeler régulièrement : false quand il n'y en a plus.
    bool pop_released(const int16_t*& data) { return released_.pop(data); }

    // État lisible depuis n'importe quel thread.
    const EngineState& state() const { return state_; }

    // Déclenche une piste immédiatement. Appel réservé au thread audio (ou hors audio actif).
    void trigger(int track, float velocity = 1.0f, float pitch_ratio = 1.0f);

    // Produit `frames` échantillons stéréo (planaires), taille quelconque. Thread audio.
    void render(float* left, float* right, int frames);

private:
    void apply(const Command& cmd);
    void fire_step();
    void play_step(int track, int step);
    void release_sample(int track);
    void clear_track(int track);
    void release_pointer(const int16_t*& data);

    // Rend des échantillons sans s'occuper du séquenceur, par blocs d'au plus cfg::BLOCK.
    void render_audio(float* left, float* right, int frames);
    void render_block(float* left, float* right, int frames);

    SynthKick   kick_;
    Sequencer   seq_;
    EngineState state_;

    SpscQueue<Command, 64>          commands_;  // UI -> audio
    SpscQueue<const int16_t*, 64>   released_;  // audio -> UI

    SamplePlayer   preview_;                      // écoute d'un sample avant de l'ajouter (hors pistes)
    const int16_t* preview_sample_ = nullptr;
    SamplePlayer   players_[cfg::MAX_TRACKS];
    const int16_t* track_sample_[cfg::MAX_TRACKS] = {};  // buffer actuellement lu par la piste
    Voice*         voices_[cfg::MAX_TRACKS] = {};
    float          gains_[cfg::MAX_TRACKS];
    float          scratch_[cfg::BLOCK];
};
