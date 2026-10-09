#pragma once

#include <cstdint>

#include "../../config.h"
#include "../command.h"
#include "../engine_state.h"
#include "canvas.h"
#include "input_event.h"
#include "sample_library.h"

// Logique de l'interface : reçoit des InputEvent, envoie des Command, dessine dans un Canvas.
// Portable (PC et Teensy) : tableaux de taille fixe, pas d'allocation, aucune dépendance graphique.
//
// L'UI garde sa propre copie du modèle (pistes, patterns) : elle est la seule à les modifier,
// donc elle n'a pas besoin de relire l'état du thread audio.
//
// Vues :
//   Tracks  - liste des pistes + « Ajouter piste »
//   Pattern - les 16 pas de la piste choisie
//   Add     - liste des samples disponibles : crée une piste, ou remplace le sample d'une piste ;
//             le sample sélectionné est joué en écoute après un court délai
//   Menu    - Changer le sample / Supprimer la piste / Annuler
//
// Le bouton Menu ouvre le menu contextuel de la vue courante : dans Pistes (sur une piste) ou
// dans Pattern, le menu de la piste. Back (ou Menu de nouveau) le referme, dans la vue d'origine.
class UiController {
public:
    enum class View : uint8_t { Tracks, Pattern, Add, Menu };

    static constexpr int NAME_LEN      = 19;   // caractères du nom affiché (sans « .wav »)
    static constexpr int LIST_ROWS     = 7;    // lignes de liste sous l'en-tête
    static constexpr int MENU_ITEMS    = 3;
    static constexpr int BPM_MIN       = 60;
    static constexpr int BPM_MAX       = 200;
    static constexpr uint32_t LIT_MS            = 120;   // durée de l'indicateur de jeu d'une piste
    static constexpr uint32_t ERROR_MS          = 2000;  // durée des messages d'erreur
    static constexpr uint32_t PREVIEW_DELAY_MS  = 150;   // la roue doit s'arrêter avant l'écoute

    // `library` peut être nul : « Ajouter piste » et « Changer le sample » ne font alors rien.
    explicit UiController(CommandSink& sink, SampleLibrary* library = nullptr)
        : sink_(sink), library_(library) {}

    // Déclare une piste du moteur (déjà configurée par l'appelant). Retourne son index dans
    // l'UI, ou -1 si plein. Le nom perd son extension « .wav ».
    int add_track(const char* name, uint8_t engine_track, uint16_t pattern_bits = 0);

    void handle(const InputEvent& e);

    // À appeler régulièrement avec l'état du moteur et l'horloge (millisecondes).
    void update(const EngineState& state, uint32_t now_ms);

    void render(Canvas& canvas) const;

    // Lecture de l'état (tests, affichage).
    View     view() const { return view_; }
    int      track_count() const { return track_count_; }
    int      cursor() const { return cursor_; }
    int      scroll() const { return scroll_; }
    int      add_cursor() const { return add_cursor_; }
    int      add_scroll() const { return add_scroll_; }
    bool     replacing() const { return add_replace_; }
    int      menu_cursor() const { return menu_cursor_; }
    bool     confirming_delete() const { return confirm_delete_; }
    int      selected_track() const { return selected_; }
    int      selected_step() const { return step_cursor_; }
    int      bpm() const { return bpm_; }
    bool     playing() const { return playing_; }
    int      engine_track(int track) const { return tracks_[track].engine_track; }
    uint16_t pattern_bits(int track) const { return tracks_[track].pattern; }
    const char* track_name(int track) const { return tracks_[track].name; }
    bool     error_shown() const { return static_cast<int32_t>(error_until_ - now_ms_) > 0; }

private:
    struct Track {
        char     name[NAME_LEN + 1] = "";
        uint8_t  engine_track       = 0;
        uint16_t pattern            = 0;  // bit i = pas i
        uint32_t last_triggers      = 0;
        bool     seen               = false;
        uint32_t lit_until          = 0;
    };

    void on_wheel(int delta);
    void on_ok();
    void on_back();
    void on_menu();
    void on_play();
    void on_step(int index);
    void on_pot_bpm(float v);

    bool can_add() const;
    int  free_engine_track() const;
    void open_pattern(int track);
    void open_menu();
    void close_menu();
    void open_add(bool replace);
    void leave_add();
    bool send_sample(int engine_track);
    void confirm_add();
    void confirm_replace();
    void delete_selected();
    void fail() { error_until_ = now_ms_ + ERROR_MS; }

    void schedule_preview();
    void play_preview(int index);
    void stop_preview();

    void render_header(Canvas& c, const char* title) const;
    void render_tracks(Canvas& c) const;
    void render_pattern(Canvas& c) const;
    void render_add(Canvas& c) const;
    void render_menu(Canvas& c) const;
    void render_error(Canvas& c, const char* text) const;
    bool is_lit(const Track& t) const { return static_cast<int32_t>(t.lit_until - now_ms_) > 0; }

    CommandSink&   sink_;
    SampleLibrary* library_;

    Track tracks_[cfg::MAX_TRACKS];
    int   track_count_ = 0;

    View view_        = View::Tracks;
    int  cursor_      = 0;   // dans la liste : 0..track_count_ (le dernier = « Ajouter piste »)
    int  scroll_      = 0;   // premier élément visible de la liste
    int  selected_    = 0;   // piste ouverte dans la vue Pattern
    int  step_cursor_ = -1;  // dernier pas touché dans la vue Pattern
    int  add_cursor_  = 0;   // sample choisi dans la vue Ajout
    int  add_scroll_  = 0;
    bool add_replace_ = false;  // la vue Ajout remplace le sample de la piste sélectionnée
    int  menu_cursor_ = 0;
    View menu_origin_ = View::Tracks;  // vue à laquelle le menu rend la main
    bool confirm_delete_ = false;  // « Supprimer » demande une seconde validation

    int      preview_pending_ = -1;  // sample à écouter quand la roue s'arrête
    uint32_t preview_due_     = 0;
    bool     preview_active_  = false;

    int      bpm_         = 120;
    bool     playing_     = false;
    int      play_step_   = -1;  // pas en cours de lecture (-1 à l'arrêt)
    uint32_t now_ms_      = 0;
    uint32_t error_until_ = 0;   // le message d'erreur s'affiche jusqu'à cet instant
};
