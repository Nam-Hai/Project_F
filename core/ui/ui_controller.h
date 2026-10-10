#pragma once

#include <cstdint>

#include "../../config.h"
#include "../command.h"
#include "../engine_state.h"
#include "../hit.h"
#include "../step_event.h"
#include "../profile/profile_data.h"
#include "../profile/profile_store.h"
#include "canvas.h"
#include "input_event.h"
#include "sample_library.h"

// Logique de l'interface : reçoit des InputEvent, envoie des Command, dessine dans un Canvas.
// Portable (PC et Teensy) : tableaux de taille fixe, pas d'allocation, aucune dépendance graphique.
// L'objet pèse plus de 100 Ko (il contient le profil) : à déclarer static ou global sur Teensy.
//
// L'UI garde sa propre copie du modèle (pistes, patterns, profil) : elle est la seule à les
// modifier, donc elle n'a pas besoin de relire l'état du thread audio.
//
// Vues :
//   Tracks  - liste des pistes + « Ajouter piste ». En montant au-dessus de la première piste, la
//             ligne de titre devient sélectionnable : Menu y ouvre le menu de la loop.
//   Pattern - les 16 pas de la piste choisie, en quatre modes (bouton Mode) : Edit (un pas bascule),
//             Velocity, Pitch et Timing (un pas se sélectionne, la roue règle sa valeur, pour toutes
//             les notes du pas ensemble)
//   Add     - « Samples » : crée une piste, ou remplace le sample d'une piste ; le sample survolé
//             est joué en écoute après un court délai
//   Menu    - menu contextuel (voir MenuKind)
//   Loops   - la liste des loops du profil, puis « + Nouvelle loop »
//   Keyboard - clavier de piano : les boutons jouent la piste à différents pitchs (voir keyboard_layout.h).
//             On y entre avec la première touche noire, depuis la vue Pistes ou Pattern.
//
// Le bouton Menu ouvre le menu de la vue courante ; Back (ou Menu de nouveau) le referme.
class UiController {
public:
    enum class View : uint8_t { Tracks, Pattern, Add, Menu, Loops, Keyboard };
    enum class MenuKind : uint8_t { Track, Loop, LoopItem, Keyboard };
    enum class EditMode : uint8_t { Edit, Velocity, Pitch, Timing };

    static constexpr int NAME_LEN      = 19;   // caractères du nom affiché (sans « .wav »)
    static constexpr int LIST_ROWS     = 7;    // lignes de liste sous l'en-tête
    static constexpr int BPM_MIN       = 60;
    static constexpr int BPM_MAX       = 200;
    static constexpr uint32_t LIT_MS            = 120;   // durée de l'indicateur de jeu d'une piste
    static constexpr uint32_t ERROR_MS          = 2000;  // durée des messages
    static constexpr uint32_t PREVIEW_DELAY_MS  = 150;   // la roue doit s'arrêter avant l'écoute
    static constexpr int      KEYBOARD_VELOCITY  = 127;    // les boutons n'ont pas de force de frappe
    static constexpr int      VELOCITY_PER_NOTCH = 4;    // vélocité : 4 points par cran de roue
    static constexpr int      OFFSET_PER_NOTCH   = 2;    // décalage : 2/128 de pas par cran de roue
    static constexpr int      QUANTIZE_PER_NOTCH = 5;    // force de quantification : 5 % par cran de roue
    static constexpr uint32_t AUDITION_MIN_MS    = 60;   // une note d'essai au plus toutes les 60 ms

    // `library` et `store` peuvent être nuls : les fonctions qui en dépendent ne font alors rien.
    explicit UiController(CommandSink& sink, SampleLibrary* library = nullptr,
                          profile::ProfileStore* store = nullptr)
        : sink_(sink), library_(library), store_(store) {}

    // Déclare une piste du moteur (déjà configurée par l'appelant). Retourne son index dans
    // l'UI, ou -1 si plein. Le nom d'affichage perd son extension « .wav ».
    // `step_velocity` / `step_pitch` (16 valeurs chacun, ou nul = valeurs par défaut) sont les réglages
    // par pas déjà appliqués à la piste du moteur.
    int add_track(const char* file, uint8_t engine_track, uint16_t pattern_bits = 0, float gain = 1.0f,
                  float pitch = 1.0f, const uint8_t* step_velocity = nullptr, const int8_t* step_pitch = nullptr);

    // Lit le profil dans le stockage (à appeler au démarrage). false si le fichier est illisible :
    // un profil vide est alors utilisé et un message s'affiche.
    bool load_profile();

    // Source des changements de pas faits par l'enregistrement (le moteur) ; sans elle, la prise n'est pas lue.
    void set_event_source(StepEventSource* source) { events_ = source; }

    void handle(const InputEvent& e);

    // À appeler régulièrement avec l'état du moteur et l'horloge (millisecondes).
    void update(const EngineState& state, uint32_t now_ms);

    void render(Canvas& canvas) const;

    // Lecture de l'état (tests, affichage).
    View     view() const { return view_; }
    MenuKind menu_kind() const { return menu_kind_; }
    EditMode edit_mode() const { return edit_mode_; }
    int      track_count() const { return track_count_; }
    int      cursor() const { return cursor_; }
    int      scroll() const { return scroll_; }
    int      add_cursor() const { return add_cursor_; }
    int      add_scroll() const { return add_scroll_; }
    bool     replacing() const { return add_replace_; }
    int      menu_cursor() const { return menu_cursor_; }
    bool     confirming() const { return confirm_; }
    bool     confirming_delete() const { return confirm_; }
    int      selected_track() const { return selected_; }
    int      keyboard_track() const { return selected_; }  // la piste jouée dans la vue Clavier
    int      selected_step() const { return step_cursor_; }
    int      bpm() const { return bpm_; }
    bool     playing() const { return playing_; }
    int      engine_track(int track) const { return tracks_[track].engine_track; }
    uint16_t pattern_bits(int track) const { return tracks_[track].pattern; }
    int      step_velocity(int track, int step) const { return tracks_[track].step_velocity[step]; }
    int      step_pitch(int track, int step) const { return tracks_[track].step_pitch[step]; }
    int      step_offset(int track, int step) const { return tracks_[track].step_offset[step]; }
    int      extra_count(int track, int step) const { return tracks_[track].extra_count[step]; }
    Hit      extra_hit(int track, int step, int i) const { return tracks_[track].extra[step][i]; }
    int      quantize(int track) const { return tracks_[track].quantize; }
    bool     editing_quantize() const { return quantize_edit_; }
    int      record_phase() const { return record_phase_; }  // 0 repos, 1 pré-compte, 2 prise
    bool     can_undo_take() const { return have_snapshot_; }
    const char* track_name(int track) const { return tracks_[track].name; }
    const char* track_file(int track) const { return tracks_[track].file; }
    bool     error_shown() const { return static_cast<int32_t>(error_until_ - now_ms_) > 0; }
    const char* notice() const { return notice_; }

    // Loop courante : emplacement d'où elle vient (-1 = jamais sauvegardée) et modifications
    // non sauvegardées. `loading()` : un chargement de loop est en cours (les commandes partent
    // par petits paquets ; les entrées sont ignorées pendant ce temps, moins d'une seconde).
    int  current_loop() const { return current_loop_; }
    bool dirty() const { return dirty_; }
    bool loading() const { return loading_; }
    int  loops_cursor() const { return loops_cursor_; }
    const profile::ProfileData& profile() const { return profile_; }

private:
    struct Track {
        char     name[NAME_LEN + 1] = "";
        char     file[profile::SAMPLE_NAME_LEN + 1] = "";
        uint8_t  engine_track       = 0;
        uint16_t pattern            = 0;  // bit i = pas i
        // Réglages par pas (conservés même si le pas est éteint) : vélocité 1..127, pitch -12..12.
        uint8_t  step_velocity[cfg::MAX_STEPS] = {127, 127, 127, 127, 127, 127, 127, 127,
                                             127, 127, 127, 127, 127, 127, 127, 127};
        int8_t   step_pitch[cfg::MAX_STEPS]    = {};
        // Micro-timing : décalage de la note principale (1/128 de pas), notes supplémentaires du pas
        // (roulements), force de quantification de la piste (0..100 %).
        int8_t   step_offset[cfg::MAX_STEPS]   = {};
        uint8_t  extra_count[cfg::MAX_STEPS]   = {};
        Hit      extra[cfg::MAX_STEPS][cfg::MAX_EXTRA_HITS];
        uint8_t  quantize           = 0;
        float    gain               = 1.0f;
        float    pitch              = 1.0f;
        uint32_t last_triggers      = 0;
        bool     seen               = false;
        uint32_t lit_until          = 0;
    };

    // Piste d'une loop en cours de chargement : son sample est déjà en mémoire.
    struct PlannedTrack {
        int            loop_track = 0;
        const int16_t* data       = nullptr;
        uint32_t       length     = 0;
        int            first_cmd  = 0;  // numéro de sa première commande dans le chargement
        int            extra      = 0;  // commandes en plus des 4 de base : une par réglage de pas non défaut
    };

    void on_wheel(int delta);
    void on_ok();
    void on_back();
    void on_menu();
    void on_mode();
    void adjust_step_value(int delta);
    bool shift_step_notes(Track& t, int step, int delta);
    void adjust_quantize(int delta);
    void toggle_record();
    void apply_step_event(const StepEvent& ev);
    void undo_take();
    void pump_restore();
    bool restore_command(int index, Command& out) const;
    void audition_selected_step();
    void on_play();
    void on_black(int index);
    void on_white_key(int index);
    void enter_keyboard(int track);
    void play_key(int pitch, int key_id);
    void on_step(int index);
    void on_pot_bpm(float v);

    bool can_add() const;
    int  free_engine_track() const;
    void open_pattern(int track);
    void open_add(bool replace);
    void leave_add();
    bool send_sample(int engine_track);
    void confirm_add();
    void confirm_replace();
    void delete_selected();

    void open_menu(MenuKind kind);
    void close_menu();
    int  menu_item_count() const;
    void on_menu_ok();

    void open_loops();
    void on_loops_ok();
    void load_loop(int slot);
    void new_loop();
    void save_loop();
    void delete_loop(int slot);
    bool build_loop(profile::LoopData& out);
    void reset_profile();
    void pump_load();
    bool next_load_command(int index, Command& out) const;
    static int  count_extras(const profile::TrackData& td);
    static bool extra_command(const profile::TrackData& td, int index, int track, Command& out);

    void notify(const char* text);
    void mark_dirty() { dirty_ = true; }

    void schedule_preview();
    void play_preview(int index);
    void stop_preview();

    void render_header(Canvas& c, const char* title, bool show_loop, char mode_letter = 0, int max_title = 16) const;
    void render_tracks(Canvas& c) const;
    void render_pattern(Canvas& c) const;
    void render_add(Canvas& c) const;
    void render_menu(Canvas& c) const;
    void render_loops(Canvas& c) const;
    void render_keyboard(Canvas& c) const;
    void render_notice(Canvas& c) const;
    bool is_lit(const Track& t) const { return static_cast<int32_t>(t.lit_until - now_ms_) > 0; }

    CommandSink&           sink_;
    StepEventSource*       events_ = nullptr;
    SampleLibrary*         library_;
    profile::ProfileStore* store_;

    Track tracks_[cfg::MAX_TRACKS];
    int   track_count_ = 0;

    View view_        = View::Tracks;
    int  cursor_      = 0;   // dans la liste : -1 (ligne de titre), 0..track_count_ (le dernier = « Ajouter piste »)
    int  scroll_      = 0;   // premier élément visible de la liste
    int  selected_    = 0;   // piste ouverte dans la vue Pattern
    int  step_cursor_ = -1;  // dernier pas touché dans la vue Pattern
    EditMode edit_mode_ = EditMode::Edit;  // mode de la vue Pattern (Edit à chaque ouverture)
    uint32_t last_audition_ms_ = 0;        // dernière note d'essai envoyée
    bool     audition_sent_    = false;    // y en a-t-il déjà eu une
    bool     audition_pending_ = false;    // une note d'essai a été retenue par la limitation de cadence
    int  add_cursor_  = 0;   // sample choisi dans la vue Samples
    int  add_scroll_  = 0;
    View keyboard_origin_ = View::Tracks;  // vue à laquelle Back rend la main depuis le clavier
    int      pressed_key_   = -1;   // dernière touche jouée : blanches 0..13, noires 14..23
    uint32_t pressed_until_ = 0;    // elle reste inversée jusqu'à cet instant
    bool add_replace_ = false;  // la vue Samples remplace le sample de la piste sélectionnée

    MenuKind menu_kind_   = MenuKind::Track;
    View     menu_origin_ = View::Tracks;  // vue à laquelle le menu rend la main
    int      menu_cursor_ = 0;
    bool     confirm_     = false;  // une action destructrice attend sa seconde validation
    bool     quantize_edit_ = false;  // menu de la piste : la roue règle la force de quantification

    // Profil et loop courante.
    profile::ProfileData profile_;
    int  current_loop_ = -1;
    bool dirty_        = false;
    int  loops_cursor_ = 0;
    int  loops_scroll_ = 0;
    int  loop_item_    = 0;            // loop visée par le menu « Supprimer »

    // Enregistrement : phase publiée par le moteur, pas du pré-compte, copie de la piste prise au lancement
    // de la prise (pour l'annuler), pertes déjà signalées.
    int      record_phase_   = 0;
    int      precount_step_  = -1;
    uint32_t lost_seen_      = 0;
    Track    snapshot_;
    bool     have_snapshot_  = false;
    bool     restoring_      = false;  // la piste est remise dans l'état de la copie (commandes envoyées par paquets)
    int      restore_next_   = 0;
    int      restore_total_  = 0;

    // Chargement d'une loop, envoyé en plusieurs fois (la file de commandes est petite).
    bool         loading_       = false;
    int          load_slot_     = 0;
    int          load_next_     = 0;   // prochaine commande à envoyer
    int          load_total_    = 0;
    int          plan_count_    = 0;
    PlannedTrack plan_[cfg::MAX_TRACKS];

    int      preview_pending_ = -1;  // sample à écouter quand la roue s'arrête
    uint32_t preview_due_     = 0;
    bool     preview_active_  = false;

    int      bpm_         = 120;
    bool     playing_     = false;
    int      play_step_   = -1;  // pas en cours de lecture (-1 à l'arrêt)
    uint32_t now_ms_      = 0;
    uint32_t error_until_ = 0;   // le message s'affiche jusqu'à cet instant
    char     notice_[22]  = "";
};
