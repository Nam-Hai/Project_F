#pragma once

#include "../config.h"
#include "command.h"
#include "click_voice.h"
#include "engine_state.h"
#include "sample_player.h"
#include "sequencer.h"
#include "step_event.h"
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
class Engine : public CommandSink, public StepEventSource {
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

    // Récupère un changement de pas fait par l'enregistrement (thread principal / UI). false s'il n'y en a plus.
    bool pop_step_event(StepEvent& ev) override { return step_events_.pop(ev); }

    // État lisible depuis n'importe quel thread.
    const EngineState& state() const { return state_; }

    // Déclenche une piste immédiatement. Appel réservé au thread audio (ou hors audio actif).
    void trigger(int track, float velocity = 1.0f, float pitch_ratio = 1.0f, int pan = 0);

    // Produit `frames` échantillons stéréo (planaires), taille quelconque. Thread audio.
    void render(float* left, float* right, int frames);

private:
    void apply(const Command& cmd);
    void fire_step();
    void play_step(int track, int step);
    void schedule(double countdown, int track, int step, int velocity, int pitch, int pan);
    void start_record(int track);
    void end_record();
    void record_note(int velocity, int pitch, int pan);
    void record_hit(int step, const Hit& hit);
    void send_step_event(int step);
    void fire_pending();
    void release_sample(int track);
    void clear_track(int track);
    void release_pointer(const int16_t*& data);

    // Rend des échantillons sans s'occuper du séquenceur, par blocs d'au plus cfg::BLOCK.
    void render_audio(float* left, float* right, int frames);
    void render_block(float* left, float* right, int frames);

    // Notes en attente : une note décalée par rapport à son pas part à son instant exact, à l'échantillon près.
    struct Pending {
        double  countdown = 0.0;  // échantillons avant le départ
        uint8_t track     = 0;
        uint8_t step      = 0;  // pas auquel la note appartient (une prise qui le remplace la retire)
        uint8_t velocity  = 0;
        int8_t  pitch     = 0;
        int8_t  pan       = 0;
    };
    static constexpr int MAX_PENDING = 256;  // au plus 16 pistes x 4 notes x 2 pas à programmer par frontière
    Pending pending_[MAX_PENDING];
    int     pending_count_ = 0;
    bool    just_started_  = false;  // la lecture vient de démarrer : les notes en avance du pas 0 partent tout de suite

    // Enregistrement. Phases : repos, pré-compte (16 pas muets, avec des clics), prise (la lecture continue
    // et les frappes sont rangées dans le pattern de la piste enregistrée).
    enum class Phase : uint8_t { Idle, Precount, Recording };
    static constexpr double MAX_LATENCY_MS = 200.0;
    static constexpr int MAX_EARLY = cfg::MAX_EXTRA_HITS + 1;
    Phase  phase_          = Phase::Idle;
    int    record_track_   = 0;
    int    precount_left_  = 0;      // pas de pré-compte restant à jouer
    int    take_steps_     = 0;      // pas de la prise déjà joués
    int    cur_step_       = 0;      // dernier pas tiré
    double latency_samples_ = 0.0;   // compensation retirée de chaque frappe
    bool   touched_[cfg::MAX_STEPS] = {};  // pas déjà frappé pendant ce tour (la première frappe remplace)
    Hit    early_[MAX_EARLY];        // frappes juste avant le premier pas de la prise, rangées à son début
    int    early_count_    = 0;

    ClickVoice  click_;
    SpscQueue<StepEvent, 64> step_events_;  // audio -> UI

    SynthKick   kick_;
    Sequencer   seq_;
    EngineState state_;

    SpscQueue<Command, 64>          commands_;  // UI -> audio
    SpscQueue<const int16_t*, 64>   released_;  // audio -> UI

    SamplePlayer   preview_;                      // écoute d'un sample avant de l'ajouter (hors pistes)
    const int16_t* preview_sample_ = nullptr;
    SamplePlayer   players_[cfg::MAX_TRACKS];
    int8_t         track_balance_[cfg::MAX_TRACKS] = {};  // balance de base de chaque piste, -50..50

    // Enveloppe ADSR de chaque piste (A, D, R en ms, S en %) : une commande en change un, le lecteur reçoit les quatre.
    struct Envelope {
        int attack  = 0;
        int decay   = 0;
        int sustain = 100;
        int release = cfg::ENV_DEFAULT_RELEASE_MS;
    };
    Envelope env_[cfg::MAX_TRACKS];
    void     set_envelope(int track, int Envelope::*field, float value, int max);
    const int16_t* track_sample_[cfg::MAX_TRACKS] = {};  // buffer actuellement lu par la piste
    Voice*         voices_[cfg::MAX_TRACKS] = {};
    float          gains_[cfg::MAX_TRACKS];
    float          scratch_[cfg::BLOCK];        // voie en cours, canal gauche
    float          scratch_right_[cfg::BLOCK];  // voie en cours, canal droit
};
