// Collection de loops : sauvegarde, chargement, suppression, confirmations, indicateur de loop.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../config.h"
#include "../core/engine_state.h"
#include "../core/ui/canvas.h"
#include "../core/ui/ui_controller.h"

using namespace profile;

static int failures = 0;

#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            std::printf("FAIL: %s\n", msg);         \
            ++failures;                             \
        }                                           \
    } while (0)

// ----- Faux composants -----------------------------------------------------------------------

// Enregistre les commandes ; `budget` limite le nombre de commandes acceptées avant un « refill ».
struct RecordingSink : CommandSink {
    std::vector<Command> sent;
    int                  budget = 1 << 30;

    bool push(const Command& c) override {
        if (budget <= 0) return false;
        --budget;
        sent.push_back(c);
        return true;
    }
    int count(Command::Type type) const {
        int n = 0;
        for (const Command& c : sent) n += c.type == type;
        return n;
    }
};

struct FakeLibrary : SampleLibrary {
    std::vector<std::string>    names = {"Kick1.wav", "Snare1.wav", "HH.wav"};
    int16_t                     data[8][4] = {};
    std::vector<int>            fail_index;  // index dont le chargement échoue
    std::vector<const int16_t*> discarded;

    // Taille et mémoire annoncées (en-têtes) : 4 images mono par défaut, pas de limite.
    uint32_t            frames_of[8]   = {4, 4, 4, 4, 4, 4, 4, 4};
    int                 channels_of[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    std::vector<int>    info_fails;  // index dont l'en-tête est illisible
    uint32_t            capacity = 0;
    int                 loads    = 0;  // nombre de chargements demandés

    int channels(const int16_t* d) const override {
        for (int i = 0; i < 8; ++i)
            if (d == data[i]) return channels_of[i];
        return 1;
    }

    bool info(int i, uint32_t& frames, int& channels) const override {
        for (int f : info_fails)
            if (f == i) return false;
        frames   = frames_of[i];
        channels = channels_of[i];
        return true;
    }
    uint32_t memory_bytes() const override { return capacity; }

    int         count() const override { return static_cast<int>(names.size()); }
    const char* name(int i) const override { return names[static_cast<std::size_t>(i)].c_str(); }
    int         find(const char* f) const override {
        for (std::size_t i = 0; i < names.size(); ++i)
            if (names[i] == f) return static_cast<int>(i);
        return -1;
    }
    bool load(int i, const int16_t*& d, uint32_t& n) override {
        ++loads;
        for (int f : fail_index)
            if (f == i) return false;
        d = data[i];
        n = 4;
        return true;
    }
    void discard(const int16_t* d) override { discarded.push_back(d); }
};

struct FakeStore : ProfileStore {
    ProfileData file;          // « contenu du fichier »
    bool        exists    = false;
    bool        fail_load = false;
    bool        fail_save = false;
    int         saves     = 0;

    LoadResult load(ProfileData& out) override {
        if (fail_load) return LoadResult::Failed;
        if (!exists) {
            out = ProfileData{};
            return LoadResult::Missing;
        }
        out = file;
        return LoadResult::Loaded;
    }
    bool save(const ProfileData& p) override {
        if (fail_save) return false;
        file   = p;
        exists = true;
        ++saves;
        return true;
    }
    const char* last_error() const override { return "erreur"; }
};

struct Fixture {
    RecordingSink sink;
    FakeLibrary   lib;
    FakeStore     store;
    EngineState   state;
    UiController  ui{sink, &lib, &store};
    uint32_t      now = 1000;

    Fixture() {
        for (auto& n : state.triggers) n.store(0);
        ui.update(state, now);
    }
    void tick(uint32_t ms = 10) {
        now += ms;
        ui.update(state, now);
    }
    void feed(std::initializer_list<InputEvent> events) {
        for (const InputEvent& e : events) ui.handle(e);
    }
};

static std::unique_ptr<Fixture> make() { return std::unique_ptr<Fixture>(new Fixture); }

// Profil de test : 3 samples, 2 loops.
static void fill_profile(ProfileData& p) {
    p = ProfileData{};
    p.add_sample("Kick1.wav");
    p.add_sample("Snare1.wav");
    p.add_sample("HH.wav");
    p.loop_count                 = 2;
    p.loops[0].bpm               = 120.0f;
    p.loops[0].track_count       = 2;
    p.loops[0].tracks[0]         = {0, 1.0f, 1.0f, 0x1111};
    p.loops[0].tracks[1]         = {1, 0.8f, 1.5f, 0x0410};
    p.loops[1].bpm               = 90.0f;
    p.loops[1].track_count       = 1;
    p.loops[1].tracks[0]         = {2, 0.5f, 1.0f, 0x00FF};
}

static bool same_rows(const Canvas& a, const Canvas& b, int y0, int y1) {
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < Canvas::W; ++x)
            if (a.get_pixel(x, y) != b.get_pixel(x, y)) return false;
    return true;
}

// Ligne de liste de référence (barre inversée si sélectionnée).
static void row(Canvas& c, int y, const char* text, bool selected) {
    if (selected) c.rect(0, y, Canvas::W, Canvas::CHAR_H, true);
    c.text(0, y, text, selected);
}

static bool header_has(const Canvas& got, const char* title, const char* loop, const char* bpm, bool playing = false) {
    Canvas ref;
    ref.text(0, 0, title);
    const int bpm_x = Canvas::W - 6 - 4 - Canvas::text_width(bpm);
    ref.text(bpm_x, 0, bpm);
    if (loop) ref.text(bpm_x - 6 - Canvas::text_width(loop), 0, loop);
    if (playing) {
        for (int col = 0; col < 4; ++col) ref.vline(Canvas::W - 6 + col, col, 7 - 2 * col);
    } else {
        ref.rect(Canvas::W - 6, 0, 2, 7, true);
        ref.rect(Canvas::W - 3, 0, 2, 7, true);
    }
    return same_rows(got, ref, 0, 8);
}

int main() {
    // ----- La ligne de titre est sélectionnable ; Valider ouvre les loops, Menu le menu de la loop -----
    {
        auto f = make();
        f->ui.add_track("Kick1.wav", 0);
        f->feed({InputEvent::wheel(-1)});
        CHECK(f->ui.cursor() == -1 && f->ui.view() == UiController::View::Tracks, "monter jusqu'au titre");

        Canvas got, plain, ref;
        f->ui.render(got);
        f->feed({InputEvent::wheel(1)});
        f->ui.render(plain);
        ref = plain;
        ref.invert_rect(0, 0, Canvas::W, Canvas::CHAR_H);
        CHECK(same_rows(got, ref, 0, 8), "titre sélectionné : la ligne du haut est inversée");
        CHECK(same_rows(got, plain, 16, 64), "titre sélectionné, sans confirmation : le bas de l'écran est inchangé (pas de question)");
        f->feed({InputEvent::wheel(-1)});

        f->feed({InputEvent::menu()});
        CHECK(f->ui.view() == UiController::View::Menu && f->ui.menu_kind() == UiController::MenuKind::Loop, "Menu sur le titre : menu de la loop");
        f->ui.render(got);
        ref.clear();
        row(ref, 8, " Charger une loop", true);
        row(ref, 16, " Sauvegarder la loop", false);
        row(ref, 24, " Annuler", false);
        CHECK(same_rows(got, ref, 8, 32), "menu de la loop : trois entrées");
        f->feed({InputEvent::back()});
        CHECK(f->ui.view() == UiController::View::Tracks && f->ui.cursor() == -1, "Back : retour à la liste, titre toujours sélectionné");

        f->feed({InputEvent::ok()});
        CHECK(f->ui.view() == UiController::View::Loops, "Valider sur le titre : liste des loops");
        CHECK(f->sink.sent.size() == 1 && f->sink.sent[0].type == Command::ClearAll && f->ui.track_count() == 0 && f->ui.current_loop() == -1,
              "entrer dans la liste vide le moteur (ClearAll) et le modèle de l'écran");
        f->feed({InputEvent::back()});
        CHECK(f->ui.view() == UiController::View::Loops && f->sink.sent.size() == 1, "Back : pas de retour depuis la liste, rien envoyé");

        f->feed({InputEvent::ok()});  // « + Nouvelle loop » (le profil est vide)
        CHECK(f->ui.view() == UiController::View::Tracks && f->sink.sent.size() == 1 && f->ui.track_count() == 0 && f->ui.cursor() == 0,
              "« + Nouvelle loop » : retour aux pistes, la machine était déjà vide (aucune commande de plus)");

        // Sur une piste, Menu ouvre toujours le menu de la piste.
        f->ui.add_track("Kick1.wav", 0);
        f->feed({InputEvent::menu()});
        CHECK(f->ui.menu_kind() == UiController::MenuKind::Track, "Menu sur une piste : menu de la piste");
    }

    // ----- Sauvegarde d'une loop neuve -----
    {
        auto f = make();
        f->ui.add_track("Kick1.wav", 0, 0x1111, 0.8f, 1.0f);
        f->ui.add_track("Snare1.wav", 1, 0x0410, 1.0f, 1.5f);
        f->ui.update(f->state, f->now);

        Canvas got;
        f->ui.render(got);
        CHECK(header_has(got, "Pistes", "L*", "120"), "avant sauvegarde : L*");

        f->feed({InputEvent::wheel(-1), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok()});
        CHECK(f->store.saves == 1 && f->store.file.loop_count == 1, "une loop sauvegardée");
        CHECK(f->ui.current_loop() == 0 && !f->ui.dirty() && f->ui.view() == UiController::View::Tracks, "loop courante = 0, retour aux pistes");
        const ProfileData& p = f->store.file;
        CHECK(p.sample_count == 2 && std::strcmp(p.samples[0], "Kick1.wav") == 0 && std::strcmp(p.samples[1], "Snare1.wav") == 0,
              "table de samples remplie avec les noms de fichier");
        CHECK(p.loops[0].track_count == 2 && p.loops[0].bpm == 120.0f && p.loops[0].tracks[0].sample == 0 &&
                  p.loops[0].tracks[0].pattern == 0x1111 && std::fabs(p.loops[0].tracks[0].gain - 0.8f) < 1e-6f &&
                  p.loops[0].tracks[1].sample == 1 && std::fabs(p.loops[0].tracks[1].pitch - 1.5f) < 1e-6f,
              "pistes sauvegardées : sample, pattern, gain, pitch");

        // Le message de confirmation s'affiche en bas de l'écran.
        f->ui.render(got);
        CHECK(f->ui.error_shown() && std::strcmp(f->ui.notice(), " Loop sauvegardee") == 0, "message de confirmation");
        Canvas ref;
        ref.text(0, 56, " Loop sauvegardee");
        CHECK(same_rows(got, ref, 56, 64), "le message s'affiche en bas");

        // Le curseur est resté sur la ligne de titre (inversée) : on redescend pour lire l'en-tête.
        f->feed({InputEvent::wheel(1)});
        CHECK(!f->ui.error_shown(), "toute action efface le message");
        f->ui.render(got);
        CHECK(header_has(got, "Pistes", "L1", "120"), "après sauvegarde : L1");
        f->tick(2100);

        // Toute modification rallume l'étoile.
        f->feed({InputEvent::ok(), InputEvent::step(3)});
        CHECK(f->ui.dirty(), "pas modifié : loop modifiée");
        f->feed({InputEvent::back()});
        f->ui.render(got);
        CHECK(header_has(got, "Pistes", "L1*", "120"), "L1*");
    }

    // ----- Écraser : confirmation, même emplacement -----
    {
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->ui.load_profile();
        f->ui.add_track("Kick1.wav", 0, 0x0001);
        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});  // liste des loops, charge la loop 1
        f->tick();
        CHECK(f->ui.current_loop() == 0, "loop 1 chargée");
        f->feed({InputEvent::wheel(-1), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok()});
        CHECK(f->ui.confirming() && f->store.saves == 0, "écraser : confirmation demandée, rien écrit");
        Canvas got, ref;
        f->ui.render(got);
        row(ref, 8, " Charger une loop", false);
        row(ref, 16, " Ecraser ?", true);
        CHECK(same_rows(got, ref, 8, 24), "libellé « Ecraser ? », et la ligne « Charger une loop » reste inchangée");

        f->feed({InputEvent::wheel(1), InputEvent::wheel(-1)});
        CHECK(!f->ui.confirming(), "la roue annule la confirmation");
        f->feed({InputEvent::ok(), InputEvent::ok()});
        CHECK(f->store.saves == 1 && f->store.file.loop_count == 2 && f->ui.current_loop() == 0, "seconde validation : écrase la loop 1, pas de nouvel emplacement");
    }

    // ----- Une seconde loop prend l'emplacement suivant ; la table de samples est nettoyée -----
    {
        auto f = make();
        f->ui.add_track("Kick1.wav", 0, 0x1111);
        f->ui.add_track("Snare1.wav", 1, 0x0410);
        f->feed({InputEvent::wheel(-1), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok()});  // loop 1 : Kick, Snare
        CHECK(f->ui.current_loop() == 0, "première loop");

        f->feed({InputEvent::ok()});  // Valider sur le titre : liste des loops
        f->feed({InputEvent::wheel(1), InputEvent::ok()});  // « + Nouvelle loop » (la loop 1 est sauvegardée : pas de confirmation)
        CHECK(f->ui.current_loop() == -1 && f->ui.track_count() == 0 && f->sink.count(Command::ClearAll) == 1, "nouvelle loop : machine vidée");

        f->ui.add_track("Kick1.wav", 0, 0x000F);
        f->ui.add_track("HH.wav", 1, 0x0003);
        f->feed({InputEvent::wheel(-1), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok()});
        CHECK(f->store.file.loop_count == 2 && f->ui.current_loop() == 1, "la seconde loop prend l'emplacement 2");
        CHECK(f->store.file.sample_count == 3, "table : Kick1, Snare1, HH (Kick1 partagé)");

        // Supprimer la loop 1 (Menu sur la loop dans la liste) : « Snare1 » n'est plus utilisé.
        f->feed({InputEvent::ok()});
        f->feed({InputEvent::wheel(-1), InputEvent::menu()});
        CHECK(f->ui.view() == UiController::View::Menu && f->ui.menu_kind() == UiController::MenuKind::LoopItem, "Menu sur une loop : menu de la loop");
        Canvas got, ref;
        f->ui.render(got);
        row(ref, 8, " Supprimer la loop", true);
        row(ref, 16, " Annuler", false);
        CHECK(same_rows(got, ref, 8, 24), "menu d'une loop : Supprimer / Annuler");
        f->feed({InputEvent::ok()});
        CHECK(f->ui.confirming() && f->store.file.loop_count == 2, "suppression : confirmation demandée");
        f->feed({InputEvent::ok()});
        CHECK(f->store.file.loop_count == 1 && f->store.file.sample_count == 2 && f->ui.view() == UiController::View::Loops,
              "loop supprimée, retour à la liste");
        CHECK(std::strcmp(f->store.file.samples[0], "Kick1.wav") == 0 && std::strcmp(f->store.file.samples[1], "HH.wav") == 0 &&
                  f->store.file.loops[0].tracks[1].sample == 1,
              "table de samples nettoyée, pistes renumérotées");
        CHECK(f->ui.current_loop() == -1 && !f->ui.dirty() && f->ui.track_count() == 0, "aucune loop courante dans la liste : rien à renuméroter");
        CHECK(std::strcmp(f->ui.notice(), " Loop supprimee") == 0, "message");
    }

    // ----- Supprimer la loop qu'on vient de quitter : la machine est déjà vide -----
    {
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->ui.load_profile();
        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});  // charge la loop 1
        f->tick();
        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::menu(), InputEvent::ok(), InputEvent::ok()});
        CHECK(f->store.file.loop_count == 1 && f->ui.current_loop() == -1 && !f->ui.dirty() && f->ui.track_count() == 0,
              "loop supprimée depuis la liste : la machine reste vide, aucune loop courante, rien de modifié");

        // « + Nouvelle loop » n'a pas de menu.
        f->feed({InputEvent::wheel(5), InputEvent::menu()});
        CHECK(f->ui.view() == UiController::View::Loops, "pas de menu sur « + Nouvelle loop »");
    }

    // ----- Affichage de la liste des loops -----
    {
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->ui.load_profile();
        f->feed({InputEvent::wheel(-1), InputEvent::ok()});
        Canvas got, ref;
        f->ui.render(got);
        ref.text(0, 0, "Loops");
        row(ref, 8, " L1 120bpm 2 pistes", true);
        row(ref, 16, " L2 90bpm 1 piste", false);
        row(ref, 24, " + Nouvelle loop", false);
        bool title_ok = true;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 5 * 6; ++x) title_ok = title_ok && got.get_pixel(x, y) == ref.get_pixel(x, y);
        CHECK(title_ok, "titre « Loops »");
        CHECK(same_rows(got, ref, 8, 32), "lignes : résumé de chaque loop, puis « + Nouvelle loop »");

        f->feed({InputEvent::wheel(1), InputEvent::ok()});  // charge la loop 2
        f->tick();
        f->feed({InputEvent::wheel(-1), InputEvent::ok()});
        f->ui.render(got);
        ref.clear();
        row(ref, 8, " L1 120bpm 2 pistes", false);
        row(ref, 16, " L2 90bpm 1 piste", true);  // plus de loop courante : pas de « > » ; le curseur démarre sur la loop quittée
        CHECK(same_rows(got, ref, 8, 24), "aucune loop courante dans la liste, et le curseur démarre sur la loop qu'on vient de quitter");
    }

    // ----- Chargement d'une loop -----
    {
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->ui.load_profile();
        f->feed({InputEvent::wheel(-1), InputEvent::ok()});  // la liste des loops : le moteur est vidé
        f->sink.sent.clear();
        f->feed({InputEvent::ok()});  // loop 1

        const auto& s = f->sink.sent;
        CHECK(!f->ui.loading(), "chargement terminé d'un coup quand la file accepte tout");
        CHECK(s.size() == 2 + 4 * 2, "ClearAll, SetBpm, puis 4 commandes par piste");
        if (s.size() == 10) {
            CHECK(s[0].type == Command::ClearAll && s[1].type == Command::SetBpm && s[1].value == 120.0f, "ClearAll puis SetBpm 120");
            CHECK(s[2].type == Command::SetTrackSample && s[2].track == 0 && s[2].data == f->lib.data[0] && s[2].length == 4, "piste 0 : sample Kick1");
            CHECK(s[3].type == Command::SetTrackGain && s[3].value == 1.0f && s[4].type == Command::SetTrackPitch && s[4].value == 1.0f, "piste 0 : gain, pitch");
            CHECK(s[5].type == Command::SetTrackPattern && s[5].track == 0 && s[5].length == 0x1111, "piste 0 : pattern");
            CHECK(s[6].type == Command::SetTrackSample && s[6].track == 1 && s[6].data == f->lib.data[1], "piste 1 : sample Snare1");
            CHECK(std::fabs(s[7].value - 0.8f) < 1e-6f && std::fabs(s[8].value - 1.5f) < 1e-6f && s[9].length == 0x0410, "piste 1 : gain 0.8, pitch 1.5, pattern");
        }
        CHECK(f->ui.track_count() == 2 && std::strcmp(f->ui.track_name(0), "Kick1") == 0 && f->ui.engine_track(1) == 1 &&
                  f->ui.pattern_bits(1) == 0x0410 && f->ui.current_loop() == 0 && !f->ui.dirty() && f->ui.bpm() == 120 &&
                  f->ui.view() == UiController::View::Tracks && f->ui.cursor() == 0,
              "modèle de l'UI : pistes, patterns, loop courante, aucune modification");

        // Charger la loop 2, qui n'a qu'une piste : les anciennes pistes disparaissent.
        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::wheel(1)});
        CHECK(f->ui.track_count() == 0 && f->sink.count(Command::ClearAll) == 2, "entrer dans la liste : anciennes pistes retirées tout de suite");
        f->sink.sent.clear();
        f->feed({InputEvent::ok()});
        CHECK(f->sink.sent.size() == 6 && f->sink.sent[0].type == Command::ClearAll && f->sink.sent[1].value == 90.0f &&
                  f->ui.track_count() == 1 && std::strcmp(f->ui.track_name(0), "HH") == 0 && f->ui.bpm() == 90,
              "la loop 2 remplace tout");
    }

    // ----- Chargement par paquets : file de commandes limitée, entrées ignorées pendant ce temps -----
    {
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->ui.load_profile();
        f->feed({InputEvent::wheel(-1), InputEvent::ok()});
        f->sink.sent.clear();  // le ClearAll de l'entrée dans la liste
        f->sink.budget = 3;
        f->feed({InputEvent::ok()});
        CHECK(f->ui.loading() && f->sink.sent.size() == 3, "file saturée : 3 commandes parties, chargement en cours");

        f->feed({InputEvent::play(), InputEvent::wheel(1), InputEvent::back()});
        CHECK(f->sink.sent.size() == 3 && f->ui.view() == UiController::View::Tracks, "entrées ignorées pendant le chargement");

        f->sink.budget = 4;
        f->tick();
        CHECK(f->ui.loading() && f->sink.sent.size() == 7, "suite du chargement au tick suivant");
        f->sink.budget = 100;
        f->tick();
        CHECK(!f->ui.loading() && f->sink.sent.size() == 10, "chargement terminé : toutes les commandes, une seule fois");
        CHECK(f->sink.count(Command::ClearAll) == 1 && f->sink.count(Command::SetTrackSample) == 2, "aucune commande en double");

        f->feed({InputEvent::play()});
        CHECK(f->sink.count(Command::Play) == 1, "les entrées fonctionnent de nouveau");
    }

    // ----- Samples introuvables : les pistes sont gardées, muettes, avec leur nom d'origine -----
    {
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->lib.names    = {"Kick1.wav", "HH.wav"};  // Snare1.wav a disparu
        f->ui.load_profile();
        auto open_first = [&] {
            f->feed({InputEvent::wheel(-1), InputEvent::ok()});
            f->sink.sent.clear();  // le ClearAll de l'entrée dans la liste
            f->feed({InputEvent::ok()});
        };
        open_first();
        CHECK(f->ui.track_count() == 2 && std::strcmp(f->ui.track_name(0), "Kick1") == 0 && std::strcmp(f->ui.track_name(1), "Snare1") == 0 &&
                  std::strcmp(f->ui.track_file(1), "Snare1.wav") == 0 && !f->ui.track_missing(0) && f->ui.track_missing(1),
              "la piste au sample manquant est gardée, avec son nom, signalée comme manquante");
        CHECK(f->sink.count(Command::SetTrackSample) == 2 && f->sink.sent.size() == 2 + 4 * 2 && f->sink.sent[6].type == Command::SetTrackSample &&
                  f->sink.sent[6].track == 1 && f->sink.sent[6].data == nullptr,
              "ses commandes partent comme celles des autres (le sample est vide : le moteur l'ignore)");
        CHECK(f->sink.sent[5].type == Command::SetTrackPattern && f->sink.sent[9].type == Command::SetTrackPattern && f->sink.sent[9].track == 1 && f->sink.sent[9].length == 0x0410,
              "son pattern est envoyé");
        CHECK(std::strcmp(f->ui.notice(), " 1 sample manquant") == 0 && f->ui.error_shown(), "message : 1 sample manquant");

        f->lib.names = {"Kick1.wav", "Snare1.wav", "HH.wav"};
        f->lib.fail_index = {1};  // Snare1 existe mais ne se charge pas
        open_first();
        CHECK(f->ui.track_count() == 2 && f->ui.track_missing(1) && std::strcmp(f->ui.notice(), " 1 sample manquant") == 0, "un échec de chargement compte comme manquant, la piste est gardée");

        f->lib.names = {"HH.wav"};
        f->lib.fail_index.clear();
        open_first();
        CHECK(f->ui.track_count() == 2 && f->ui.track_missing(0) && f->ui.track_missing(1) && std::strcmp(f->ui.notice(), " 2 samples manquants") == 0,
              "message : 2 samples manquants, les deux pistes sont gardées");

        // Le fichier est revenu : à la réouverture, plus rien ne manque.
        f->lib.names = {"Kick1.wav", "Snare1.wav", "HH.wav"};
        f->feed({InputEvent::wheel(-5), InputEvent::ok(), InputEvent::ok()});
        CHECK(f->ui.track_count() == 2 && !f->ui.track_missing(0) && !f->ui.track_missing(1) && !f->ui.error_shown(), "le sample est revenu : la loop se recharge normalement");
    }

    // ----- Le nombre de canaux d'un sample chargé est transmis au moteur ; un index hors table est écarté -----
    {
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->lib.channels_of[1] = 2;  // Snare1 est stéréo
        f->ui.load_profile();
        f->feed({InputEvent::wheel(-1), InputEvent::ok()});
        f->sink.sent.clear();
        f->feed({InputEvent::ok()});
        CHECK(f->sink.sent.size() == 10 && f->sink.sent[2].channels == 1 && f->sink.sent[6].channels == 2, "SetTrackSample : 1 canal pour Kick1, 2 pour Snare1");

        // Une piste dont l'index de sample est hors de la table (que les codecs ne produisent pas) est écartée, sans plantage.
        auto g = make();
        fill_profile(g->store.file);
        g->store.file.loops[0].tracks[1].sample = static_cast<uint8_t>(g->store.file.sample_count);  // juste après la table
        g->store.exists = true;
        g->ui.load_profile();
        g->feed({InputEvent::wheel(-1), InputEvent::ok()});
        g->sink.sent.clear();
        g->feed({InputEvent::ok()});
        CHECK(g->ui.track_count() == 1 && std::strcmp(g->ui.track_name(0), "Kick1") == 0 && !g->ui.track_missing(0) && g->sink.sent.size() == 6 &&
                  std::strcmp(g->ui.notice(), " 1 sample manquant") == 0,
              "index de sample hors de la table : la piste est écartée, le reste se charge");
    }

    // ----- Une piste sans sample : affichage « ? », modifications, remplacement, sauvegarde -----
    {
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->lib.names    = {"Kick1.wav", "HH.wav"};  // Snare1.wav manque
        f->ui.load_profile();
        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});  // loop 1 : Kick1, Snare1 (manquant)
        f->tick();

        // L'indicateur « ? » : à droite de la ligne de la piste manquante, et d'elle seule.
        Canvas got, ref;
        f->ui.render(got);
        auto has_question = [&](int row_y, bool inverted) {
            Canvas q;
            if (inverted) q.rect(0, row_y, Canvas::W, Canvas::CHAR_H, true);
            q.text(Canvas::W - 6, row_y, "?", inverted);
            for (int y = row_y; y < row_y + 8; ++y)
                for (int x = Canvas::W - 6; x < Canvas::W; ++x)
                    if (got.get_pixel(x, y) != q.get_pixel(x, y)) return false;
            return true;
        };
        CHECK(has_question(16, false), "la ligne de la piste manquante porte un « ? » à droite");
        f->feed({InputEvent::wheel(1)});
        f->ui.render(got);
        CHECK(has_question(16, true), "piste sélectionnée (ligne inversée) : le « ? » est inversé aussi");
        bool none_on_first = true;
        for (int y = 8; y < 16; ++y)
            for (int x = Canvas::W - 6; x < Canvas::W; ++x) none_on_first = none_on_first && !got.get_pixel(x, y);
        CHECK(none_on_first, "pas de « ? » sur la piste dont le sample existe");
        f->feed({InputEvent::wheel(-1)});

        // On modifie la piste comme n'importe quelle autre : pattern, vélocité, pitch, gain, réglages.
        f->sink.sent.clear();
        f->feed({InputEvent::wheel(1), InputEvent::ok()});  // ouvre le pattern de la piste 2
        CHECK(f->ui.view() == UiController::View::Pattern && f->ui.engine_track(f->ui.cursor()) == 1, "le pattern de la piste manquante s'ouvre");
        f->feed({InputEvent::step(5)});
        CHECK(f->ui.dirty() && f->sink.count(Command::SetStep) == 1 && f->sink.sent[0].track == 1 && f->sink.sent[0].step == 5 && f->sink.sent[0].value == 1.0f,
              "un pas s'allume : commande envoyée à la piste moteur 1");
        f->sink.sent.clear();
        f->feed({InputEvent::mode(), InputEvent::step(4), InputEvent::wheel(-10)});  // vélocité du pas 5 (joué)
        CHECK(f->sink.count(Command::SetStepVelocity) >= 1 && f->sink.sent[0].track == 1, "la vélocité d'un pas se règle, envoyée à la piste 1 du moteur");
        f->sink.sent.clear();
        f->feed({InputEvent::black(0), InputEvent::wheel(-6), InputEvent::black_up(0)});  // gain de la piste
        CHECK(f->sink.count(Command::SetTrackGain) >= 1 && f->sink.sent.back().type == Command::SetTrackGain && f->sink.sent.back().track == 1, "le gain de la piste se règle");
        f->feed({InputEvent::back()});

        // La piste manquante est sauvegardée telle quelle : son nom est conservé.
        f->feed({InputEvent::wheel(-5), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok(), InputEvent::ok()});
        const ProfileData& p = f->store.file;
        CHECK(f->store.saves == 1 && p.loops[0].track_count == 2 && std::strcmp(p.samples[p.loops[0].tracks[1].sample], "Snare1.wav") == 0,
              "sauvegarde : la piste manquante est gardée avec le nom Snare1.wav");

        // Changer le sample : la piste a enfin un son, son nom change, plus de « ? ».
        f->feed({InputEvent::wheel(2)});  // le curseur était sur le titre : la piste 2 est deux crans plus bas
        f->feed({InputEvent::menu(), InputEvent::ok()});  // menu de la piste 2, Changer le sample
        CHECK(f->ui.view() == UiController::View::Add, "Changer le sample : la liste des samples s'ouvre");
        f->sink.sent.clear();
        f->feed({InputEvent::wheel(1), InputEvent::ok()});  // HH.wav
        CHECK(f->sink.count(Command::SetTrackSample) == 1 && f->sink.sent[0].track == 1 && f->sink.sent[0].data != nullptr,
              "le nouveau sample part vers la piste moteur 1");
        CHECK(!f->ui.track_missing(1) && std::strcmp(f->ui.track_file(1), "HH.wav") == 0 && f->ui.dirty(), "plus manquante, nom du fichier mis à jour, loop modifiée");
        f->feed({InputEvent::wheel(-1)});  // la piste 2 n'est plus sélectionnée : sa ligne est claire
        f->ui.render(got);
        bool gone = true;
        for (int y = 16; y < 24; ++y)
            for (int x = Canvas::W - 6; x < Canvas::W; ++x) gone = gone && !got.get_pixel(x, y);
        CHECK(gone, "le « ? » a disparu");
    }
    {
        // Les réglages d'une piste manquante (pitch, vélocité, enveloppe) suivent les autres au chargement.
        auto f = make();
        ProfileData& p = f->store.file;
        p              = ProfileData{};
        p.add_sample("Kick1.wav");
        p.add_sample("Snare1.wav");
        p.add_sample("HH.wav");
        p.loop_count           = 1;
        p.loops[0].track_count = 3;
        p.loops[0].tracks[0].sample = 0;
        p.loops[0].tracks[1].sample = 1;  // sera introuvable
        p.loops[0].tracks[1].step_pitch[0] = 5;
        p.loops[0].tracks[1].attack        = 20;
        p.loops[0].tracks[2].sample = 2;
        p.loops[0].tracks[2].step_velocity[2] = 64;
        f->store.exists = true;
        f->lib.names    = {"Kick1.wav", "HH.wav"};
        f->ui.load_profile();
        f->feed({InputEvent::wheel(-5), InputEvent::ok()});
        f->sink.sent.clear();  // le ClearAll de l'entrée dans la liste
        f->feed({InputEvent::ok()});
        const auto& s = f->sink.sent;
        CHECK(s.size() == 2 + 3 * 4 + 1 + 1 + 1 && f->ui.track_count() == 3, "3 pistes chargées, leurs commandes toutes envoyées");
        CHECK(s.back().type == Command::SetStepVelocity && s.back().track == 2 && s.back().step == 2 && s.back().value == 64.0f,
              "la vélocité de la 3e piste vise la piste moteur 2");
        CHECK(f->sink.count(Command::SetStepPitch) == 1 && f->ui.step_pitch(1, 0) == 5, "le pitch d'un pas de la piste manquante est envoyé et gardé dans le modèle");
        CHECK(f->ui.step_velocity(2, 2) == 64 && f->ui.track_missing(1) && !f->ui.track_missing(2), "modèle : la piste HH garde sa vélocité, seule la 2e est manquante");
    }

    // ----- Perdre les modifications : « Quitter sans sauv. ? » est demandé en entrant dans la liste des loops -----
    {
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->ui.load_profile();
        f->ui.add_track("Kick1.wav", 0, 0x0001);
        f->feed({InputEvent::ok(), InputEvent::step(2), InputEvent::back()});  // modification : loop jamais sauvegardée
        CHECK(f->ui.dirty(), "loop modifiée");

        // Depuis la ligne de titre.
        f->feed({InputEvent::wheel(-1), InputEvent::ok()});
        CHECK(f->ui.confirming() && f->ui.view() == UiController::View::Tracks && f->sink.count(Command::ClearAll) == 0 && f->ui.track_count() == 1,
              "première validation : confirmation demandée, rien vidé, on reste sur les pistes");
        Canvas got, ref;
        f->ui.render(got);
        ref.text(0, 56, " Quitter sans sauv. ?");
        CHECK(same_rows(got, ref, 56, 64), "la question s'affiche en bas de l'écran");

        f->feed({InputEvent::wheel(1)});
        CHECK(!f->ui.confirming(), "la roue annule");
        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::back()});
        CHECK(!f->ui.confirming() && f->ui.view() == UiController::View::Tracks && f->sink.count(Command::ClearAll) == 0, "Back annule");
        f->feed({InputEvent::ok(), InputEvent::play()});
        CHECK(!f->ui.confirming() && f->ui.view() == UiController::View::Tracks && f->sink.count(Command::ClearAll) == 0, "n'importe quel autre geste annule aussi");

        f->feed({InputEvent::ok(), InputEvent::ok()});
        CHECK(f->ui.view() == UiController::View::Loops && f->sink.count(Command::ClearAll) == 1 && f->ui.track_count() == 0 && !f->ui.dirty() &&
                  f->ui.current_loop() == -1 && !f->ui.confirming(),
              "seconde validation : moteur et écran vidés, la liste s'ouvre, plus rien de modifié");

        // Le choix d'une loop ne redemande rien.
        f->feed({InputEvent::ok()});
        CHECK(f->ui.current_loop() == 0 && f->ui.view() == UiController::View::Tracks && f->ui.track_count() == 2, "choisir une loop : chargée sans confirmation");
    }
    {
        // Depuis le menu de la loop : « Charger une loop » demande la même chose.
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->ui.load_profile();
        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});  // loop 1 chargée
        f->feed({InputEvent::wheel(1), InputEvent::ok(), InputEvent::step(5), InputEvent::back()});  // modifie la piste 2
        CHECK(f->ui.dirty(), "modifiée");
        f->sink.sent.clear();

        f->feed({InputEvent::wheel(-5), InputEvent::menu(), InputEvent::ok()});  // titre, menu de la loop, « Charger une loop »
        CHECK(f->ui.confirming() && f->ui.view() == UiController::View::Menu && f->sink.sent.empty(), "menu, « Charger une loop » : confirmation, rien envoyé");
        Canvas got, ref;
        f->ui.render(got);
        row(ref, 8, " Quitter sans sauv. ?", true);
        row(ref, 16, " Sauvegarder la loop", false);
        CHECK(same_rows(got, ref, 8, 24), "libellé « Quitter sans sauv. ? » sur la ligne du menu (l'autre ligne inchangée)");

        f->feed({InputEvent::wheel(1)});
        CHECK(!f->ui.confirming(), "la roue annule");
        f->ui.render(got);
        ref.clear();
        row(ref, 8, " Charger une loop", false);
        row(ref, 16, " Sauvegarder la loop", true);
        CHECK(same_rows(got, ref, 8, 24), "libellés normaux après l'annulation");

        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});
        CHECK(f->ui.view() == UiController::View::Loops && f->sink.count(Command::ClearAll) == 1 && f->ui.track_count() == 0 && !f->ui.dirty(),
              "seconde validation : vidé, la liste s'ouvre");

        // « + Nouvelle loop » : la machine est déjà vide, rien de plus à envoyer.
        f->sink.sent.clear();
        f->feed({InputEvent::wheel(5), InputEvent::ok()});
        CHECK(f->sink.sent.empty() && f->ui.track_count() == 0 && f->ui.current_loop() == -1 && f->ui.view() == UiController::View::Tracks,
              "« + Nouvelle loop » : retour aux pistes, aucune commande");
        Canvas c2;
        f->ui.render(c2);
        CHECK(header_has(c2, "Pistes", "L*", "120"), "L* après une nouvelle loop");

        // Une loop propre n'a pas de confirmation à l'entrée.
        f->feed({InputEvent::wheel(-5), InputEvent::ok()});
        CHECK(!f->ui.confirming() && f->ui.view() == UiController::View::Loops, "loop non modifiée : la liste s'ouvre tout de suite");
    }

    // ----- Entrée dans la liste : file de commandes pleine -----
    {
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->ui.load_profile();
        f->ui.add_track("Kick1.wav", 0, 0x0001);
        f->sink.budget = 0;  // la file n'accepte plus rien
        f->feed({InputEvent::wheel(-1), InputEvent::ok()});
        CHECK(f->ui.view() == UiController::View::Tracks && f->ui.track_count() == 1 && std::strcmp(f->ui.notice(), " Echec de l'action") == 0,
              "file pleine : message, on reste sur les pistes avec leur contenu");
        f->sink.budget = 100;
        f->feed({InputEvent::ok()});
        CHECK(f->ui.view() == UiController::View::Loops && f->ui.track_count() == 0, "la file accepte de nouveau : la liste s'ouvre");

        // Loop modifiée : après un échec à la seconde validation, la question sera reposée.
        auto g = make();
        fill_profile(g->store.file);
        g->store.exists = true;
        g->ui.load_profile();
        g->ui.add_track("Kick1.wav", 0, 0x0001);
        g->feed({InputEvent::ok(), InputEvent::step(2), InputEvent::back(), InputEvent::wheel(-1), InputEvent::ok()});
        CHECK(g->ui.confirming(), "loop modifiée : question posée");
        g->sink.budget = 0;
        g->feed({InputEvent::ok()});
        CHECK(!g->ui.confirming() && g->ui.view() == UiController::View::Tracks && g->ui.dirty() && g->ui.track_count() == 1,
              "échec de l'envoi : rien perdu, la question n'est plus en attente");
        g->sink.budget = 100;
        g->feed({InputEvent::ok()});
        CHECK(g->ui.confirming() && g->ui.view() == UiController::View::Tracks, "elle est reposée à la validation suivante");
    }

    // ----- Mémoire des samples : une loop qui ne tient pas est refusée avant tout chargement -----
    {
        auto make_mem = [] {
            auto f = make();
            ProfileData& p = f->store.file;
            p              = ProfileData{};
            p.add_sample("Kick1.wav");
            p.add_sample("Snare1.wav");
            p.add_sample("HH.wav");
            p.loop_count                 = 2;
            p.loops[0].bpm               = 120.0f;
            p.loops[0].track_count       = 2;
            p.loops[0].tracks[0]         = {0, 1.0f, 1.0f, 0x1111};  // Kick : 1000 images mono = 2000 octets
            p.loops[0].tracks[1]         = {1, 1.0f, 1.0f, 0x0410};  // Snare : 500 images stéréo = 2000 octets
            p.loops[1].bpm               = 90.0f;
            p.loops[1].track_count       = 1;
            p.loops[1].tracks[0]         = {2, 1.0f, 1.0f, 0x00FF};  // HH : 100 images mono = 200 octets
            f->store.exists    = true;
            f->lib.frames_of[0]   = 1000;
            f->lib.frames_of[1]   = 500;
            f->lib.channels_of[1] = 2;
            f->lib.frames_of[2]   = 100;
            f->ui.load_profile();
            return f;
        };
        auto open_list = [](Fixture& f) { f.feed({InputEvent::wheel(-1), InputEvent::ok()}); f.sink.sent.clear(); };

        // Exactement la place : chargée.
        auto f = make_mem();
        f->lib.capacity = 4000;
        open_list(*f);
        f->feed({InputEvent::ok()});
        CHECK(f->ui.current_loop() == 0 && f->ui.track_count() == 2 && f->ui.view() == UiController::View::Tracks && !f->ui.error_shown(),
              "la loop de 4000 octets tient dans 4000 octets : chargée");

        // Un octet de moins : refusée, rien n'est chargé ni envoyé, la liste reste ouverte.
        auto g = make_mem();
        g->lib.capacity = 3999;
        open_list(*g);
        g->feed({InputEvent::ok()});
        CHECK(g->ui.view() == UiController::View::Loops && g->ui.track_count() == 0 && g->ui.current_loop() == -1 && g->sink.sent.empty() && g->lib.loads == 0 &&
                  !g->ui.loading() && std::strcmp(g->ui.notice(), " Memoire pleine") == 0 && g->ui.error_shown(),
              "4000 octets dans 3999 : refusée, rien chargé, rien envoyé, la liste reste ouverte, message");
        Canvas got, ref;
        g->ui.render(got);
        row(ref, 8, " L1 120bpm 2 pistes", true);
        row(ref, 16, " L2 90bpm 1 piste", false);
        row(ref, 24, " + Nouvelle loop", false);
        ref.text(0, 56, " Memoire pleine");
        CHECK(same_rows(got, ref, 8, 64), "la loop refusée reste listée, le message est en bas");

        // Une plus petite loop de la même liste se charge, et on peut ressayer la grosse quand la place revient.
        g->feed({InputEvent::wheel(1), InputEvent::ok()});
        CHECK(g->ui.current_loop() == 1 && g->ui.track_count() == 1 && g->lib.loads == 1, "la loop de 200 octets passe");
        g->feed({InputEvent::wheel(-5), InputEvent::ok()});  // titre, liste
        g->lib.capacity = 4000;
        g->feed({InputEvent::wheel(-1), InputEvent::ok()});  // le curseur démarre sur la loop 2 : on remonte sur la loop 1
        CHECK(g->ui.current_loop() == 0 && g->ui.track_count() == 2, "avec assez de place, la grosse loop s'ouvre");

        // Pas de limite connue (0) : rien n'est refusé, même énorme.
        auto h = make_mem();
        h->lib.capacity    = 0;
        h->lib.frames_of[0] = 0xFFFFFFF0u;
        open_list(*h);
        h->feed({InputEvent::ok()});
        CHECK(h->ui.current_loop() == 0 && h->ui.track_count() == 2, "sans limite connue : aucune loop refusée");

        // Les calculs sont en 64 bits : 2^31 images stéréo = 8 Gio, qui reviendraient à 0 en 32 bits.
        auto i = make_mem();
        i->lib.capacity       = 4000;
        i->lib.frames_of[0]   = 0x80000000u;
        i->lib.channels_of[0] = 2;
        open_list(*i);
        i->feed({InputEvent::ok()});
        CHECK(i->ui.view() == UiController::View::Loops && std::strcmp(i->ui.notice(), " Memoire pleine") == 0, "8 Gio : refusée (pas de dépassement de 32 bits)");

        // Un en-tête illisible ne compte pas ; un sample absent non plus ; un même sample sur deux pistes compte deux fois.
        auto j = make_mem();
        j->lib.capacity = 2000;  // Kick seul tient
        j->lib.info_fails = {1};  // Snare : en-tête illisible, on ne sait pas la mesurer
        open_list(*j);
        j->feed({InputEvent::ok()});
        CHECK(j->ui.current_loop() == 0, "un en-tête illisible n'est pas compté (2000 octets pour la seule Kick)");

        auto k = make_mem();
        k->lib.capacity = 2000;
        k->lib.names    = {"Kick1.wav", "HH.wav"};  // Snare1.wav a disparu : sa piste n'est pas comptée
        open_list(*k);
        k->feed({InputEvent::ok()});
        CHECK(k->ui.current_loop() == 0, "un sample absent n'est pas compté");

        auto m = make_mem();
        m->store.file.loops[0].track_count = 2;
        m->store.file.loops[0].tracks[1].sample = 0;  // deux pistes sur Kick : 2 × 2000 octets
        m->ui.load_profile();
        m->lib.capacity = 3999;
        open_list(*m);
        m->feed({InputEvent::ok()});
        CHECK(m->ui.view() == UiController::View::Loops && std::strcmp(m->ui.notice(), " Memoire pleine") == 0, "le même sample sur deux pistes est compté deux fois");
        m->lib.capacity = 4000;
        m->feed({InputEvent::ok()});
        CHECK(m->ui.current_loop() == 0, "...et tient dans 4000");

        // Stéréo = 2 canaux ; 16 bits = 2 octets.
        auto n = make_mem();
        n->lib.capacity = 2399;  // HH seul : 200 octets, deux fois la loop 2 : on vérifie surtout l'unité
        open_list(*n);
        n->feed({InputEvent::wheel(1), InputEvent::ok()});
        CHECK(n->ui.current_loop() == 1, "100 images mono = 200 octets tiennent");
        n->feed({InputEvent::wheel(-5), InputEvent::ok()});
        n->lib.capacity = 199;
        n->feed({InputEvent::ok()});  // le curseur est sur la loop 2
        CHECK(n->ui.view() == UiController::View::Loops && std::strcmp(n->ui.notice(), " Memoire pleine") == 0, "100 images mono ne tiennent pas dans 199 octets");
    }

    // ----- Vélocité et pitch par pas : sauvegarde, chargement, ordre des commandes -----
    {
        // Profil d'essai : la piste 0 a des réglages de pas, la 1 aucun, la 2 une vélocité.
        auto fill_custom = [](ProfileData& p) {
            p = ProfileData{};
            p.add_sample("Kick1.wav");
            p.add_sample("Snare1.wav");
            p.add_sample("HH.wav");
            p.loop_count           = 1;
            p.loops[0].bpm         = 100.0f;
            p.loops[0].track_count = 3;
            p.loops[0].tracks[0].sample = 0;
            p.loops[0].tracks[0].pattern = 0x0011;
            p.loops[0].tracks[0].step_velocity[4]  = 90;
            p.loops[0].tracks[0].step_velocity[15] = 1;
            p.loops[0].tracks[0].step_pitch[0]     = -5;
            p.loops[0].tracks[0].step_pitch[3]     = 7;
            p.loops[0].tracks[1].sample = 1;
            p.loops[0].tracks[2].sample = 2;
            p.loops[0].tracks[2].pattern = 0x0001;
            p.loops[0].tracks[2].step_velocity[2] = 64;
        };
        auto same_commands = [](const std::vector<Command>& a, const std::vector<Command>& b) {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                // Les pointeurs de samples viennent de deux bibliothèques différentes : on compare leur présence.
                if (a[i].type != b[i].type || a[i].track != b[i].track || a[i].step != b[i].step || a[i].value != b[i].value ||
                    (a[i].data != nullptr) != (b[i].data != nullptr) || a[i].length != b[i].length)
                    return false;
            return true;
        };

        // Chargement : ClearAll, SetBpm, puis par piste 4 commandes de base et un réglage par valeur non défaut.
        auto f = make();
        fill_custom(f->store.file);
        f->store.exists = true;
        f->ui.load_profile();
        f->feed({InputEvent::wheel(-5), InputEvent::ok()});
        f->sink.sent.clear();  // le ClearAll de l'entrée dans la liste
        f->feed({InputEvent::ok()});
        const auto& s = f->sink.sent;
        CHECK(!f->ui.loading() && s.size() == 19, "19 commandes : 2 + (4+4) + 4 + (4+1)");
        if (s.size() == 19) {
            CHECK(s[0].type == Command::ClearAll && s[1].type == Command::SetBpm && s[1].value == 100.0f, "ClearAll, SetBpm");
            CHECK(s[2].type == Command::SetTrackSample && s[3].type == Command::SetTrackGain && s[4].type == Command::SetTrackPitch &&
                      s[5].type == Command::SetTrackPattern && s[5].length == 0x0011,
                  "piste 0 : 4 commandes de base");
            CHECK(s[6].type == Command::SetStepVelocity && s[6].track == 0 && s[6].step == 4 && s[6].value == 90.0f &&
                      s[7].type == Command::SetStepVelocity && s[7].track == 0 && s[7].step == 15 && s[7].value == 1.0f,
                  "piste 0 : vélocités non défaut, dans l'ordre des pas");
            CHECK(s[8].type == Command::SetStepPitch && s[8].track == 0 && s[8].step == 0 && s[8].value == -5.0f &&
                      s[9].type == Command::SetStepPitch && s[9].track == 0 && s[9].step == 3 && s[9].value == 7.0f,
                  "piste 0 : pitchs non défaut, après les vélocités");
            CHECK(s[10].type == Command::SetTrackSample && s[10].track == 1 && s[13].type == Command::SetTrackPattern && s[13].track == 1 &&
                      s[14].type == Command::SetTrackSample && s[14].track == 2,
                  "piste 1 sans réglage : exactement 4 commandes");
            CHECK(s[18].type == Command::SetStepVelocity && s[18].track == 2 && s[18].step == 2 && s[18].value == 64.0f, "piste 2 : sa vélocité");
        }
        CHECK(f->ui.step_velocity(0, 4) == 90 && f->ui.step_velocity(0, 15) == 1 && f->ui.step_velocity(0, 0) == 127 &&
                  f->ui.step_pitch(0, 0) == -5 && f->ui.step_pitch(0, 3) == 7 && f->ui.step_pitch(0, 1) == 0 &&
                  f->ui.step_velocity(1, 4) == 127 && f->ui.step_velocity(2, 2) == 64,
              "modèle de l'UI : valeurs par pas restituées");

        // Même chargement avec une file qui n'accepte que 5 commandes à la fois : mêmes commandes, dans le même ordre.
        const std::vector<Command> expected_commands = f->sink.sent;
        auto g = make();
        fill_custom(g->store.file);
        g->store.exists = true;
        g->ui.load_profile();
        g->feed({InputEvent::wheel(-5), InputEvent::ok()});
        g->sink.sent.clear();  // le ClearAll de l'entrée dans la liste
        g->sink.budget = 5;
        g->feed({InputEvent::ok()});
        int ticks = 0;
        while (g->ui.loading() && ticks < 20) {
            g->sink.budget = 5;
            g->tick();
            ++ticks;
        }
        CHECK(!g->ui.loading() && ticks >= 3 && same_commands(g->sink.sent, expected_commands),
              "chargement par paquets de 5 : les mêmes 19 commandes, dans le même ordre, une seule fois");

        // Aller-retour : sauvegarder la loop rechargée redonne les mêmes valeurs.
        f->feed({InputEvent::wheel(-5), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok(), InputEvent::ok()});
        const ProfileData& saved = f->store.file;
        CHECK(f->store.saves == 1 && saved.loops[0].tracks[0].step_velocity[4] == 90 && saved.loops[0].tracks[0].step_velocity[15] == 1 &&
                  saved.loops[0].tracks[0].step_pitch[0] == -5 && saved.loops[0].tracks[0].step_pitch[3] == 7 &&
                  saved.loops[0].tracks[2].step_velocity[2] == 64 && saved.loops[0].tracks[1].step_velocity[0] == 127,
              "recharger puis sauvegarder : les valeurs par pas sont conservées");
    }
    {
        // add_track : réglages par pas fournis (bornés), ou valeurs par défaut.
        RecordingSink sink;
        UiController  ui(sink);
        uint8_t       vel[16];
        int8_t        pit[16];
        for (int i = 0; i < 16; ++i) {
            vel[i] = static_cast<uint8_t>(i == 0 ? 0 : i == 1 ? 200 : 127);
            pit[i] = static_cast<int8_t>(i == 0 ? -50 : i == 1 ? 50 : 3);
        }
        ui.add_track("a.wav", 0, 0, 1.0f, 1.0f, vel, pit);
        ui.add_track("b.wav", 1);
        CHECK(ui.step_velocity(0, 0) == 1 && ui.step_velocity(0, 1) == 127 && ui.step_velocity(0, 2) == 127 &&
                  ui.step_pitch(0, 0) == -12 && ui.step_pitch(0, 1) == 12 && ui.step_pitch(0, 2) == 3,
              "add_track : valeurs hors limites bornées");
        CHECK(ui.step_velocity(1, 5) == 127 && ui.step_pitch(1, 5) == 0, "add_track sans réglages : valeurs par défaut");
    }
    {
        // Sauvegarde d'une piste créée avec des réglages : ils arrivent dans le profil.
        auto f = make();
        uint8_t vel[16];
        int8_t  pit[16] = {};
        for (int i = 0; i < 16; ++i) vel[i] = 127;
        vel[6] = 33;
        pit[9] = -4;
        f->ui.add_track("Kick1.wav", 0, 0x0040, 1.0f, 1.0f, vel, pit);
        f->feed({InputEvent::wheel(-1), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok()});
        CHECK(f->store.file.loop_count == 1 && f->store.file.loops[0].tracks[0].step_velocity[6] == 33 &&
                  f->store.file.loops[0].tracks[0].step_pitch[9] == -4 && f->store.file.loops[0].tracks[0].step_velocity[0] == 127,
              "sauvegarde : vélocités et pitchs par pas exportés");
    }

    // ----- Échecs et limites -----
    {
        auto f = make();
        f->ui.add_track("Kick1.wav", 0, 0x0001);

        // Sauvegarde qui échoue : la loop reste en mémoire, non sauvegardée (`*`), prête pour un nouvel essai.
        f->store.fail_save = true;
        f->feed({InputEvent::wheel(-1), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok()});
        CHECK(f->ui.current_loop() == 0 && f->ui.dirty() && f->ui.profile().loop_count == 1 && f->store.file.loop_count == 0 &&
                  std::strcmp(f->ui.notice(), " Echec sauvegarde") == 0,
              "échec d'écriture : loop gardée en mémoire, non sauvegardée, message");
        f->store.fail_save = false;
        f->feed({InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok(), InputEvent::ok()});
        CHECK(f->ui.current_loop() == 0 && !f->ui.dirty() && f->ui.profile().loop_count == 1 && f->store.file.loop_count == 1,
              "le nouvel essai réécrit le profil sans doublon");

        // Suppression qui échoue.
        f->store.fail_save = true;
        f->feed({InputEvent::ok(), InputEvent::menu(), InputEvent::ok(), InputEvent::ok()});
        CHECK(f->ui.profile().loop_count == 0 && f->store.file.loop_count == 1 && std::strcmp(f->ui.notice(), " Echec sauvegarde") == 0,
              "suppression échouée : retirée de la mémoire, encore sur le disque, message");
        f->store.fail_save = false;
    }
    {
        // Profil plein : 32 loops.
        auto f = make();
        f->store.file   = ProfileData{};
        f->store.file.add_sample("Kick1.wav");
        f->store.file.loop_count = MAX_LOOPS;
        f->store.exists = true;
        f->ui.load_profile();
        f->ui.add_track("Kick1.wav", 0, 0x0001);
        f->feed({InputEvent::wheel(-1), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok()});
        CHECK(f->store.saves == 0 && std::strcmp(f->ui.notice(), " Profil plein") == 0 && f->ui.current_loop() == -1, "profil plein : sauvegarde refusée");
    }
    {
        // Table de samples pleine : 4 loops de 16 samples différents = 64 entrées.
        auto f = make();
        ProfileData& p = f->store.file;
        p              = ProfileData{};
        for (int i = 0; i < MAX_SAMPLES; ++i) p.add_sample(("s" + std::to_string(i) + ".wav").c_str());
        p.loop_count = 4;
        for (int l = 0; l < 4; ++l) {
            p.loops[l].track_count = cfg::MAX_TRACKS;
            for (int t = 0; t < cfg::MAX_TRACKS; ++t) p.loops[l].tracks[t].sample = static_cast<uint8_t>(l * 16 + t);
        }
        f->store.exists = true;
        f->ui.load_profile();
        f->ui.add_track("nouveau.wav", 0, 0x0001);
        f->feed({InputEvent::wheel(-1), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok()});
        CHECK(f->store.saves == 0 && std::strcmp(f->ui.notice(), " Trop de samples") == 0 && f->ui.profile().loop_count == 4 &&
                  f->ui.profile().sample_count == MAX_SAMPLES,
              "table de samples pleine : refusé, profil intact");
    }
    {
        // Profil illisible : message, profil vide, la sauvegarde reste possible.
        auto f = make();
        f->store.fail_load = true;
        CHECK(!f->ui.load_profile() && std::strcmp(f->ui.notice(), " Profil illisible") == 0 && f->ui.profile().loop_count == 0,
              "profil illisible : message et profil vide");
        f->feed({InputEvent::wheel(-1), InputEvent::ok()});
        Canvas got, ref;
        f->ui.render(got);
        row(ref, 8, " + Nouvelle loop", true);
        CHECK(same_rows(got, ref, 8, 16), "la liste ne contient que « + Nouvelle loop »");
    }
    {
        // Sans stockage ni bibliothèque : les menus existent mais ne font rien de dangereux.
        RecordingSink sink;
        UiController  ui(sink);
        ui.load_profile();
        ui.add_track("Kick1.wav", 0, 0x0001);
        ui.handle(InputEvent::wheel(-1));
        ui.handle(InputEvent::menu());
        ui.handle(InputEvent::wheel(1));
        ui.handle(InputEvent::ok());
        CHECK(std::strcmp(ui.notice(), " Pas de stockage") == 0 && sink.sent.empty(), "sans stockage : message, aucune commande");
        ui.handle(InputEvent::back());
        ui.handle(InputEvent::ok());
        CHECK(ui.view() == UiController::View::Loops, "la liste des loops s'ouvre (vide)");
        ui.handle(InputEvent::ok());  // « + Nouvelle loop » : la machine est modifiée ? non (seulement add_track) : vide directement
        CHECK(ui.track_count() == 0, "« Nouvelle loop » sans stockage : la machine est vidée");
    }

    // ----- Le remplacement d'un sample et la suppression d'une piste comptent comme des modifications -----
    {
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->ui.load_profile();
        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});
        CHECK(!f->ui.dirty(), "loop chargée : propre");
        f->feed({InputEvent::menu(), InputEvent::ok(), InputEvent::wheel(1), InputEvent::ok()});  // remplace le sample de la piste 1
        CHECK(f->ui.dirty() && std::strcmp(f->ui.track_file(0), "Snare1.wav") == 0, "remplacement : modifiée, nom de fichier mis à jour");

        f->feed({InputEvent::wheel(-1), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok()});  // sauvegarde (écrasement : confirmation)
        f->feed({InputEvent::ok()});
        CHECK(!f->ui.dirty(), "sauvegardée : plus de modification");
        CHECK(f->store.file.loops[0].track_count == 2 && std::strcmp(f->store.file.samples[f->store.file.loops[0].tracks[0].sample], "Snare1.wav") == 0,
              "le nouveau sample est dans la loop sauvegardée");
    }

    if (failures == 0) std::printf("OK\n");
    return failures ? 1 : 0;
}
