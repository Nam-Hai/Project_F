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

    int         count() const override { return static_cast<int>(names.size()); }
    const char* name(int i) const override { return names[static_cast<std::size_t>(i)].c_str(); }
    int         find(const char* f) const override {
        for (std::size_t i = 0; i < names.size(); ++i)
            if (names[i] == f) return static_cast<int>(i);
        return -1;
    }
    bool load(int i, const int16_t*& d, uint32_t& n) override {
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
        f->feed({InputEvent::back()});
        CHECK(f->ui.view() == UiController::View::Tracks && f->sink.sent.empty(), "Back : retour aux pistes, rien envoyé");

        // Sur une piste, Menu ouvre toujours le menu de la piste.
        f->feed({InputEvent::wheel(1), InputEvent::menu()});
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
        row(ref, 16, " Ecraser ?", true);
        CHECK(same_rows(got, ref, 16, 24), "libellé « Ecraser ? »");

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
        CHECK(f->ui.current_loop() == 0, "la loop courante a glissé de l'emplacement 2 à 1");
        CHECK(std::strcmp(f->ui.notice(), " Loop supprimee") == 0, "message");
    }

    // ----- Supprimer la loop courante : elle reste dans la machine, sans emplacement -----
    {
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->ui.load_profile();
        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});  // charge la loop 1
        f->tick();
        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::menu(), InputEvent::ok(), InputEvent::ok()});
        CHECK(f->store.file.loop_count == 1 && f->ui.current_loop() == -1 && f->ui.dirty() && f->ui.track_count() == 2,
              "loop courante supprimée : toujours dans la machine, sans emplacement, marquée modifiée");

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
        row(ref, 16, ">L2 90bpm 1 piste", true);  // « > » devant la loop courante, curseur dessus
        CHECK(same_rows(got, ref, 8, 24), "la loop courante est marquée par « > » et le curseur démarre dessus");
    }

    // ----- Chargement d'une loop -----
    {
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->ui.load_profile();
        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});  // loop 1

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
        f->sink.sent.clear();
        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::wheel(1), InputEvent::ok()});
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

    // ----- Samples introuvables : les pistes concernées sont sautées -----
    {
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->lib.names    = {"Kick1.wav", "HH.wav"};  // Snare1.wav a disparu
        f->ui.load_profile();
        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});
        CHECK(f->ui.track_count() == 1 && std::strcmp(f->ui.track_name(0), "Kick1") == 0 && f->sink.count(Command::SetTrackSample) == 1 &&
                  f->sink.sent.size() == 6,
              "la piste au sample manquant est sautée");
        CHECK(std::strcmp(f->ui.notice(), " 1 sample manquant") == 0 && f->ui.error_shown(), "message : 1 sample manquant");

        f->lib.names = {"Kick1.wav", "Snare1.wav", "HH.wav"};
        f->lib.fail_index = {1};  // Snare1 existe mais ne se charge pas
        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});
        CHECK(f->ui.track_count() == 1 && std::strcmp(f->ui.notice(), " 1 sample manquant") == 0, "un échec de chargement compte comme manquant");

        f->lib.names = {"HH.wav"};
        f->lib.fail_index.clear();
        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});
        CHECK(f->ui.track_count() == 0 && std::strcmp(f->ui.notice(), " 2 samples manquants") == 0, "message : 2 samples manquants");
    }

    // ----- Perdre les modifications : confirmation -----
    {
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->ui.load_profile();
        f->ui.add_track("Kick1.wav", 0, 0x0001);
        f->feed({InputEvent::ok(), InputEvent::step(2), InputEvent::back()});  // modification : loop jamais sauvegardée
        CHECK(f->ui.dirty(), "loop modifiée");

        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});
        CHECK(f->ui.confirming() && f->ui.view() == UiController::View::Loops && f->sink.count(Command::ClearAll) == 0,
              "première validation : confirmation, rien chargé");
        Canvas got, ref;
        f->ui.render(got);
        row(ref, 8, " Quitter sans sauv. ?", true);
        CHECK(same_rows(got, ref, 8, 16), "libellé « Quitter sans sauv. ? »");

        f->feed({InputEvent::wheel(1)});
        CHECK(!f->ui.confirming(), "la roue annule");
        f->feed({InputEvent::ok(), InputEvent::back()});
        CHECK(!f->ui.confirming() && f->ui.view() == UiController::View::Tracks && f->sink.count(Command::ClearAll) == 0, "Back annule");

        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok(), InputEvent::ok()});
        CHECK(f->sink.count(Command::ClearAll) == 1 && f->ui.current_loop() == 0 && !f->ui.dirty(), "seconde validation : la loop est chargée");
    }
    {
        auto f = make();
        fill_profile(f->store.file);
        f->store.exists = true;
        f->ui.load_profile();
        f->feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});  // loop 1 chargée
        f->feed({InputEvent::wheel(1), InputEvent::ok(), InputEvent::step(5), InputEvent::back()});  // modifie la piste 2
        CHECK(f->ui.dirty(), "modifiée");
        f->sink.sent.clear();
        f->feed({InputEvent::wheel(-5), InputEvent::ok(), InputEvent::wheel(5), InputEvent::ok()});  // titre, liste, « + Nouvelle loop »
        CHECK(f->ui.confirming() && f->sink.sent.empty(), "« + Nouvelle loop » modifiée : confirmation");
        f->feed({InputEvent::ok()});
        CHECK(f->sink.count(Command::ClearAll) == 1 && f->ui.track_count() == 0 && f->ui.current_loop() == -1 && !f->ui.dirty() &&
                  f->ui.view() == UiController::View::Tracks,
              "seconde validation : machine vidée, loop sans emplacement");
        Canvas got;
        f->ui.render(got);
        CHECK(header_has(got, "Pistes", "L*", "120"), "L* après une nouvelle loop");

        // Une loop vide jamais touchée ne demande pas de confirmation.
        f->sink.sent.clear();
        f->feed({InputEvent::wheel(-5), InputEvent::ok(), InputEvent::ok()});
        CHECK(!f->ui.confirming() && f->ui.current_loop() == 0 && f->sink.count(Command::ClearAll) == 1, "loop vide : pas de confirmation, la loop est chargée");
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
        f->feed({InputEvent::wheel(-5), InputEvent::ok(), InputEvent::ok()});
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
        // Une piste dont le sample manque est sautée : les réglages des suivantes visent la bonne piste du moteur.
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
        p.loops[0].tracks[2].sample = 2;
        p.loops[0].tracks[2].step_velocity[2] = 64;
        f->store.exists = true;
        f->lib.names    = {"Kick1.wav", "HH.wav"};
        f->ui.load_profile();
        f->feed({InputEvent::wheel(-5), InputEvent::ok(), InputEvent::ok()});
        const auto& s = f->sink.sent;
        CHECK(s.size() == 2 + 4 + 5 && f->ui.track_count() == 2, "2 pistes chargées, leurs commandes seulement");
        CHECK(s.back().type == Command::SetStepVelocity && s.back().track == 1 && s.back().step == 2 && s.back().value == 64.0f,
              "la vélocité de la 3e piste vise la piste moteur 1 (la 2e est sautée)");
        CHECK(f->sink.count(Command::SetStepPitch) == 0, "les réglages de la piste sautée ne sont pas envoyés");
        CHECK(f->ui.step_velocity(1, 2) == 64, "modèle : la piste HH garde sa vélocité");
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
