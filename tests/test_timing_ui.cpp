// Micro-timing dans l'UI : mode T, notes supplémentaires, quantification, chargement et sauvegarde.
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
using Mode = UiController::EditMode;
using View = UiController::View;

static int failures = 0;

#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            std::printf("FAIL: %s\n", msg);         \
            ++failures;                             \
        }                                           \
    } while (0)

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
    // Dernière commande de ce type pour (piste, pas, slot), ou une commande vide.
    Command last(Command::Type type, int track, int step, int slot = 0) const {
        for (std::size_t i = sent.size(); i-- > 0;)
            if (sent[i].type == type && sent[i].track == track && sent[i].step == step && sent[i].slot == slot) return sent[i];
        return Command{};
    }
};

struct FakeLibrary : SampleLibrary {
    int16_t data[4] = {};
    int         count() const override { return 1; }
    const char* name(int) const override { return "Kick1.wav"; }
    int         find(const char* f) const override { return std::strcmp(f, "Kick1.wav") == 0 ? 0 : -1; }
    bool        load(int, const int16_t*& d, uint32_t& n) override {
        d = data;
        n = 4;
        return true;
    }
    void discard(const int16_t*) override {}
};

struct FakeStore : ProfileStore {
    ProfileData file;
    bool        exists = false;
    LoadResult  load(ProfileData& out) override {
        out = exists ? file : ProfileData{};
        return exists ? LoadResult::Loaded : LoadResult::Missing;
    }
    bool        save(const ProfileData& p) override {
        file   = p;
        exists = true;
        return true;
    }
    const char* last_error() const override { return ""; }
};

// Une loop d'une piste (sample 0), pas 0, 4, 8 et 12 joués. Pas 4 : décalage 10 et deux notes supplémentaires
// (décalage -20 / vélocité 100 / pitch 3 et décalage 30 / vélocité 60 / pitch -2). Pas 8 : décalage 32.
// Pas 12 : décalage -64. Quantification 40 %.
static void fill_profile(ProfileData& p) {
    p = ProfileData{};
    p.add_sample("Kick1.wav");
    p.loop_count           = 1;
    p.loops[0].bpm         = 120.0f;
    p.loops[0].track_count = 1;
    TrackData& t           = p.loops[0].tracks[0];
    t                      = TrackData{};
    t.sample               = 0;
    t.gain                 = 1.0f;
    t.pitch                = 1.0f;
    t.pattern              = 0x1111;
    t.step_offset[4]       = 10;
    t.extra_count[4]       = 2;
    t.extra[4][0]          = Hit{-20, 100, 3};
    t.extra[4][1]          = Hit{30, 60, -2};
    t.step_offset[8]       = 32;
    t.step_offset[12]      = -64;
    t.quantize             = 40;
}

struct Fixture {
    RecordingSink sink;
    FakeLibrary   lib;
    FakeStore     store;
    EngineState   state;
    UiController  ui{sink, &lib, &store};
    uint32_t      now = 1000;

    Fixture() {
        for (auto& n : state.triggers) n.store(0);
        fill_profile(store.file);
        store.exists = true;
        ui.load_profile();
        ui.update(state, now);
        feed({InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});  // liste des loops, charge la loop 1
        tick(10);
        feed({InputEvent::ok()});  // vue Pattern de la piste
    }
    void tick(uint32_t ms) {
        now += ms;
        ui.update(state, now);
    }
    void feed(std::initializer_list<InputEvent> events) {
        for (const InputEvent& e : events) ui.handle(e);
    }
};

static bool same_rows(const Canvas& a, const Canvas& b, int y0, int y1) {
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < Canvas::W; ++x)
            if (a.get_pixel(x, y) != b.get_pixel(x, y)) return false;
    return true;
}

static int lit(const Canvas& c, int x, int y, int w, int h) {
    int n = 0;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) n += c.get_pixel(x + i, y + j);
    return n;
}

static bool value_is(const Canvas& c, const char* text) {
    Canvas ref;
    ref.text(104, 48, text);
    for (int y = 40; y < 64; ++y)
        for (int x = 100; x < Canvas::W; ++x)
            if (c.get_pixel(x, y) != ref.get_pixel(x, y)) return false;
    return true;
}

int main() {
    // ----- Chargement : le modèle et les commandes -----
    {
        Fixture f;
        CHECK(f.ui.view() == View::Pattern && f.ui.track_count() == 1, "loop chargée, pattern ouvert");
        CHECK(f.ui.step_offset(0, 4) == 10 && f.ui.step_offset(0, 8) == 32 && f.ui.step_offset(0, 12) == -64 && f.ui.step_offset(0, 0) == 0,
              "décalages dans le modèle");
        CHECK(f.ui.extra_count(0, 4) == 2 && f.ui.extra_hit(0, 4, 0).offset == -20 && f.ui.extra_hit(0, 4, 0).velocity == 100 &&
                  f.ui.extra_hit(0, 4, 0).pitch == 3 && f.ui.extra_hit(0, 4, 1).offset == 30 && f.ui.extra_hit(0, 4, 1).velocity == 60 &&
                  f.ui.extra_hit(0, 4, 1).pitch == -2 && f.ui.extra_count(0, 0) == 0,
              "notes supplémentaires dans le modèle");
        CHECK(f.ui.quantize(0) == 40, "quantification dans le modèle");

        CHECK(f.sink.count(Command::SetStepOffset) == 3, "une commande par décalage non nul");
        CHECK(f.sink.last(Command::SetStepOffset, 0, 12).value == -64.0f && f.sink.last(Command::SetStepOffset, 0, 4).value == 10.0f,
              "valeurs des décalages");
        const Command e1 = f.sink.last(Command::SetExtraHit, 0, 4, 1);
        const Command e2 = f.sink.last(Command::SetExtraHit, 0, 4, 2);
        CHECK(f.sink.count(Command::SetExtraHit) == 2 && e1.offset == -20 && e1.value == 100.0f && e1.pitch == 3 && e2.offset == 30 &&
                  e2.value == 60.0f && e2.pitch == -2,
              "une commande par note supplémentaire, avec son slot");
        CHECK(f.sink.count(Command::SetTrackQuantize) == 1, "quantification envoyée");
        CHECK(!f.ui.dirty(), "charger ne modifie pas la loop");
    }

    // ----- Mode T : cycle, lettre, jauge, roue -----
    {
        Fixture f;
        f.feed({InputEvent::mode(), InputEvent::mode(), InputEvent::mode()});
        CHECK(f.ui.edit_mode() == Mode::Timing, "Edit -> V -> P -> T");
        Canvas c;
        f.ui.render(c);
        Canvas ref;
        const int bpm_x = Canvas::W - 6 - 4 - Canvas::text_width("120");
        ref.text(bpm_x - 12, 0, "T");
        CHECK(lit(c, bpm_x - 12, 0, 6, 8) == lit(ref, bpm_x - 12, 0, 6, 8) && lit(c, bpm_x - 12, 0, 6, 8) > 0, "lettre T dans l'en-tête");

        // Sans sélection : pas de jauge. Pas éteint : pas sélectionnable.
        f.feed({InputEvent::step(1)});
        CHECK(f.ui.selected_step() == -1, "pas éteint : non sélectionné");

        // Pas 8 (décalage 32 = +25 %) : jauge à droite du centre, valeur « +25% ».
        f.feed({InputEvent::step(8)});
        f.ui.render(c);
        CHECK(value_is(c, "+25%"), "valeur en pourcentage de pas");
        CHECK(lit(c, 51, 49, 24, 5) == 24 * 5 && lit(c, 26, 49, 24, 5) == 0, "jauge : 24 pixels à droite du centre");

        // Pas 12 (-64 = -50 %) : 48 pixels à gauche.
        f.feed({InputEvent::step(12)});
        f.ui.render(c);
        CHECK(value_is(c, "-50%") && lit(c, 2, 49, 48, 5) == 48 * 5 && lit(c, 51, 49, 40, 5) == 0, "jauge à gauche, -50 %");

        // Roue : 2 graduations par cran, borné à 63 ; une commande par cran, note d'essai envoyée.
        f.sink.sent.clear();
        f.tick(100);
        f.feed({InputEvent::step(0), InputEvent::wheel(3)});
        CHECK(f.ui.step_offset(0, 0) == 6 && f.sink.last(Command::SetStepOffset, 0, 0).value == 6.0f && f.ui.dirty(),
              "+3 crans = +6 graduations, loop modifiée");
        CHECK(f.sink.count(Command::AuditionStep) == 0, "mode T : pas de note d'essai");
        f.tick(100);
        f.feed({InputEvent::wheel(100)});
        CHECK(f.ui.step_offset(0, 0) == 63, "borné à 63");
        f.tick(100);
        f.feed({InputEvent::wheel(-100)});
        CHECK(f.ui.step_offset(0, 0) == -64, "borné à -64");
        f.tick(100);
        f.feed({InputEvent::wheel(100)});
        f.tick(100);
        f.sink.sent.clear();
        f.feed({InputEvent::wheel(100), InputEvent::step(0), InputEvent::wheel(1)});
        CHECK(f.sink.count(Command::SetStepOffset) <= 1, "à la limite : rien d'inutile n'est envoyé");
    }

    // ----- Modes V, P et T agissent sur toutes les notes du pas, en gardant leurs écarts -----
    {
        Fixture f;
        f.feed({InputEvent::mode(), InputEvent::mode(), InputEvent::mode(), InputEvent::step(4)});  // Timing
        f.sink.sent.clear();
        f.feed({InputEvent::wheel(5)});  // +10
        CHECK(f.ui.step_offset(0, 4) == 20 && f.ui.extra_hit(0, 4, 0).offset == -10 && f.ui.extra_hit(0, 4, 1).offset == 40,
              "T : le même décalage pour toutes les notes");
        const Command e1 = f.sink.last(Command::SetExtraHit, 0, 4, 1);
        const Command e2 = f.sink.last(Command::SetExtraHit, 0, 4, 2);
        CHECK(f.sink.count(Command::SetStepOffset) == 1 && f.sink.count(Command::SetExtraHit) == 2 && e1.offset == -10 &&
                  e1.value == 100.0f && e1.pitch == 3 && e2.offset == 40 && e2.value == 60.0f && e2.pitch == -2,
              "T : commandes pour la note principale et les deux supplémentaires, autres champs intacts");

        // Borné par la note qui arrive en premier à sa limite : la note à +40 plafonne à 63 -> +23 au plus.
        f.tick(100);
        f.feed({InputEvent::wheel(100)});
        CHECK(f.ui.extra_hit(0, 4, 1).offset == 63 && f.ui.step_offset(0, 4) == 43 && f.ui.extra_hit(0, 4, 0).offset == 13,
              "T : écarts conservés à la limite");

        // Vélocité : 127 / 100 / 60 ; vers le bas, tout descend de 4 par cran.
        f.feed({InputEvent::mode(), InputEvent::mode(), InputEvent::mode()});  // Balance, Edit, Velocity
        f.tick(100);
        f.feed({InputEvent::wheel(-2)});
        CHECK(f.ui.step_velocity(0, 4) == 119 && f.ui.extra_hit(0, 4, 0).velocity == 92 && f.ui.extra_hit(0, 4, 1).velocity == 52,
              "V : toutes les notes");
        // Vers le haut : la note principale est à 119 -> au plus +8, donc le pas est borné par elle.
        f.tick(100);
        f.feed({InputEvent::wheel(100)});
        CHECK(f.ui.step_velocity(0, 4) == 127 && f.ui.extra_hit(0, 4, 0).velocity == 100 && f.ui.extra_hit(0, 4, 1).velocity == 60,
              "V : borné par la note la plus haute");

        // Pitch : 0 / +3 / -2.
        f.feed({InputEvent::mode()});  // Pitch
        f.tick(100);
        f.feed({InputEvent::wheel(2)});
        CHECK(f.ui.step_pitch(0, 4) == 2 && f.ui.extra_hit(0, 4, 0).pitch == 5 && f.ui.extra_hit(0, 4, 1).pitch == 0, "P : toutes les notes");

        // Les autres pas ne bougent pas.
        CHECK(f.ui.extra_count(0, 8) == 0 && f.ui.step_offset(0, 8) == 32 && f.ui.step_pitch(0, 8) == 0, "autres pas intacts");
    }

    // ----- Affichage : points des roulements -----
    {
        Fixture f;
        Canvas c;
        f.ui.render(c);  // mode Edit
        // Les boîtes restent sur la grille quel que soit le décalage (pas 4 : +10, pas 8 : +32, pas 12 : -64).
        CHECK(lit(c, 0, 24, 7, 7) == 49 && lit(c, 4 * 8, 24, 7, 7) == 49 && lit(c, 8 * 8, 24, 7, 7) == 49 && lit(c, 12 * 8, 24, 7, 7) == 49,
              "boîtes pleines sur la grille, sans décalage");
        CHECK(!c.get_pixel(8 * 8 - 1, 24 + 3) && !c.get_pixel(12 * 8 - 1, 24 + 3) && !c.get_pixel(12 * 8 + 7, 24 + 3), "rien en dehors des cases");
        // Points : 3 notes au pas 4 (x = 32, 34, 36, sous la boîte) ; aucun ailleurs.
        CHECK(c.get_pixel(32, 32) && c.get_pixel(34, 32) && c.get_pixel(36, 32) && !c.get_pixel(38, 32) && !c.get_pixel(0, 32),
              "pas 4 : trois points (trois notes)");

        // Un pas éteint reste sur la grille et n'affiche pas de points.
        f.feed({InputEvent::step(8)});  // éteint le pas 8
        f.ui.render(c);
        CHECK(c.get_pixel(8 * 8, 24) && c.get_pixel(8 * 8 + 6, 24 + 6) && !c.get_pixel(8 * 8 + 3, 24 + 3), "pas éteint : contour vide sur la grille");
    }

    // ----- Sauvegarde : les données de timing sont conservées -----
    {
        Fixture f;
        f.feed({InputEvent::mode(), InputEvent::mode(), InputEvent::mode(), InputEvent::step(4), InputEvent::wheel(5)});  // T : +10
        f.feed({InputEvent::mode()});                                                                                      // Edit
        f.feed({InputEvent::back(), InputEvent::wheel(-1), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok(), InputEvent::ok()});
        CHECK(!f.ui.dirty(), "sauvegardée");
        const TrackData& t = f.store.file.loops[0].tracks[0];
        CHECK(t.step_offset[4] == 20 && t.step_offset[8] == 32 && t.step_offset[12] == -64 && t.step_offset[0] == 0, "décalages sauvegardés");
        CHECK(t.extra_count[4] == 2 && t.extra[4][0].offset == -10 && t.extra[4][0].velocity == 100 && t.extra[4][0].pitch == 3 &&
                  t.extra[4][1].offset == 40 && t.extra[4][1].velocity == 60 && t.extra[4][1].pitch == -2 && t.extra_count[0] == 0,
              "notes supplémentaires sauvegardées");
        CHECK(t.quantize == 40, "quantification sauvegardée");

        // Recharger : mêmes données (aller-retour complet par le profil).
        Fixture g;
        g.store.file = f.store.file;
        g.ui.load_profile();
        g.feed({InputEvent::back(), InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});
        g.tick(10);
        CHECK(g.ui.step_offset(0, 4) == 20 && g.ui.extra_hit(0, 4, 1).offset == 40 && g.ui.quantize(0) == 40, "rechargé : mêmes données");
    }

    // ----- Chargement par petits paquets : file de commandes limitée -----
    {
        Fixture f;
        f.sink.sent.clear();
        f.sink.budget = 3;
        f.feed({InputEvent::back(), InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});
        CHECK(f.ui.loading(), "chargement en cours");
        for (int i = 0; i < 40 && f.ui.loading(); ++i) {
            f.sink.budget += 3;
            f.tick(10);
        }
        CHECK(!f.ui.loading() && f.sink.count(Command::SetExtraHit) == 2 && f.sink.count(Command::SetStepOffset) == 3 &&
                  f.sink.count(Command::SetTrackQuantize) == 1,
              "toutes les commandes de timing finissent par partir, une fois chacune");
    }

    if (failures == 0) std::printf("OK : test_timing_ui\n");
    return failures == 0 ? 0 : 1;
}
