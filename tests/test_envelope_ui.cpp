// Enveloppe ADSR dans l'UI : touches noires 5 à 8 tenues (Attack, Decay, Sustain, Release), la roue, la forme de
// l'enveloppe à l'écran, chargement et sauvegarde.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../config.h"
#include "../core/engine.h"
#include "../core/engine_state.h"
#include "../core/ui/canvas.h"
#include "../core/ui/ui_controller.h"

using namespace profile;
using View = UiController::View;
using Set  = UiController::TrackSetting;

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
    Command last(Command::Type type) const {
        for (std::size_t i = sent.size(); i-- > 0;)
            if (sent[i].type == type) return sent[i];
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

// Une piste « Snare_long_name.wav » sur la piste moteur 2 ; enveloppe par défaut.
struct Fixture {
    RecordingSink sink;
    EngineState   state;
    UiController  ui{sink};
    uint32_t      now = 1000;

    Fixture() {
        for (auto& n : state.triggers) n.store(0);
        ui.add_track("Snare_long_name.wav", 2, 0x1111);
        ui.update(state, now);
        feed({InputEvent::ok()});
    }
    void feed(std::initializer_list<InputEvent> e) {
        for (const InputEvent& x : e) ui.handle(x);
    }
    void hold(int key, int notches) { feed({InputEvent::black(key), InputEvent::wheel(notches), InputEvent::black_up(key)}); }
};

static int lit(const Canvas& c, int x, int y, int w, int h) {
    int n = 0;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) n += c.get_pixel(x + i, y + j);
    return n;
}

static bool header_text_is(const Canvas& got, const char* text) {
    Canvas    ref;
    const int bpm_x = Canvas::W - 6 - 4 - Canvas::text_width("120");
    ref.text(bpm_x - 6 - Canvas::CHAR_W * static_cast<int>(std::strlen(text)), 0, text);
    for (int y = 0; y < 8; ++y)
        for (int x = 50; x < bpm_x - 2; ++x)
            if (got.get_pixel(x, y) != ref.get_pixel(x, y)) return false;
    return true;
}

int main() {
    // ----- Départ : enveloppe par défaut -----
    {
        Fixture f;
        CHECK(f.ui.track_attack(0) == 0 && f.ui.track_decay(0) == 0 && f.ui.track_sustain(0) == 100 && f.ui.track_release(0) == cfg::ENV_DEFAULT_RELEASE_MS,
              "enveloppe par défaut : A 0, D 0, S 100, R 5");
        CHECK(f.ui.track_setting() == Set::None, "rien de tenu");
    }

    // ----- Les touches noires 5 à 8 -----
    {
        Fixture f;
        const struct { int key; Set setting; const char* header; } keys[] = {{4, Set::Attack, "Atk"}, {5, Set::Decay, "Dec"}, {6, Set::Sustain, "Sus"}, {7, Set::Release, "Rel"}};
        for (const auto& k : keys) {
            f.feed({InputEvent::black(k.key)});
            Canvas c;
            f.ui.render(c);
            CHECK(f.ui.track_setting() == k.setting && header_text_is(c, k.header), "touche tenue : le bon réglage et son nom dans l'en-tête");
            f.feed({InputEvent::black_up(k.key)});
            CHECK(f.ui.track_setting() == Set::None, "relâchée : plus de réglage");
        }
        // La dernière enfoncée règle ; relâchée, la précédente reprend la main (y compris parmi les nouvelles).
        f.feed({InputEvent::black(4), InputEvent::black(7), InputEvent::black(5)});
        CHECK(f.ui.track_setting() == Set::Decay, "la dernière enfoncée règle");
        f.feed({InputEvent::black_up(5)});
        CHECK(f.ui.track_setting() == Set::Release, "relâchée : la plus récente encore tenue reprend (Release)");
        f.feed({InputEvent::black_up(7), InputEvent::black_up(4)});
        CHECK(f.ui.track_setting() == Set::None, "tout relâché");
        // Mélange avec les réglages 2 à 4.
        f.feed({InputEvent::black(2), InputEvent::black(6)});
        CHECK(f.ui.track_setting() == Set::Sustain, "Sustain après Wide");
        f.feed({InputEvent::black_up(6)});
        CHECK(f.ui.track_setting() == Set::Width, "Wide reprend la main");
        f.feed({InputEvent::black_up(2)});
        // La touche 9 (index 8) n'a pas de rôle dans la vue Pattern ; la 1re règle le gain.
        f.feed({InputEvent::black(8), InputEvent::black_up(8)});
        CHECK(f.ui.track_setting() == Set::None && f.sink.sent.empty(), "touche 9 : sans rôle");
        f.feed({InputEvent::black(0), InputEvent::black(4)});
        CHECK(f.ui.track_setting() == Set::Attack, "Attack après Gain");
        f.feed({InputEvent::black_up(4)});
        CHECK(f.ui.track_setting() == Set::Gain, "Gain reprend la main");
        f.feed({InputEvent::black_up(0)});
        CHECK(f.ui.track_setting() == Set::None, "tout relâché");
        // Quitter la vue abandonne le réglage.
        f.feed({InputEvent::black(5), InputEvent::back()});
        CHECK(f.ui.view() == View::Tracks && f.ui.track_setting() == Set::None, "Back : réglage abandonné");
        // Mode Clavier / Rec : les touches jouent des notes.
        f.feed({InputEvent::ok(), InputEvent::black(9)});
        f.sink.sent.clear();
        f.feed({InputEvent::black(4), InputEvent::black_up(4)});
        CHECK(f.ui.track_setting() == Set::None && f.sink.count(Command::Note) == 1, "mode Clavier : la touche 5 joue une note, aucun réglage");
    }

    // ----- La roue : Attack, Decay, Release suivent une liste ; Sustain 5 % par cran -----
    {
        Fixture f;
        // Attack : 0, 1, 2, 3, 5, 7, 10, 15, 20, 30, 40, 50, 70, 100, 150, 200, 300, 500, 700, 1000, 1500, 2000.
        f.feed({InputEvent::black(4)});
        f.feed({InputEvent::wheel(1)});
        CHECK(f.ui.track_attack(0) == 1 && f.sink.last(Command::SetTrackAttack).value == 1.0f && f.ui.dirty(), "Attack : 0 -> 1 ms, commande envoyée, loop modifiée");
        f.feed({InputEvent::wheel(5)});
        CHECK(f.ui.track_attack(0) == 10, "cinq crans de plus : 10 ms");
        f.feed({InputEvent::wheel(-1)});
        CHECK(f.ui.track_attack(0) == 7, "un cran en arrière : 7 ms");
        f.feed({InputEvent::wheel(100)});
        CHECK(f.ui.track_attack(0) == 2000, "borné à 2000 ms");
        f.sink.sent.clear();
        f.feed({InputEvent::wheel(1)});
        CHECK(f.sink.sent.empty(), "à 2000 : rien d'envoyé");
        f.feed({InputEvent::wheel(-100)});
        CHECK(f.ui.track_attack(0) == 0, "borné à 0");
        f.sink.sent.clear();
        f.feed({InputEvent::wheel(-1)});
        CHECK(f.sink.sent.empty(), "à 0 : rien d'envoyé");
        f.feed({InputEvent::black_up(4)});

        // Decay : même liste, sa propre valeur.
        f.hold(5, 13);
        CHECK(f.ui.track_decay(0) == 100 && f.ui.track_attack(0) == 0 && f.sink.last(Command::SetTrackDecay).value == 100.0f, "Decay : 13 crans = 100 ms, sans toucher à l'attack");
        // Release : part de 5 ms (le défaut est dans la liste).
        f.hold(7, -1);
        CHECK(f.ui.track_release(0) == 3 && f.sink.last(Command::SetTrackRelease).value == 3.0f, "Release : 5 -> 3 ms");
        f.hold(7, -10);
        CHECK(f.ui.track_release(0) == 0, "Release jusqu'à 0 (coupure nette)");
        // Sustain : 5 % par cran, de 100 vers le bas.
        f.hold(6, -3);
        CHECK(f.ui.track_sustain(0) == 85 && f.sink.last(Command::SetTrackSustain).value == 85.0f, "Sustain : 5 % par cran");
        f.hold(6, -100);
        CHECK(f.ui.track_sustain(0) == 0, "Sustain borné à 0");
        f.hold(6, 100);
        CHECK(f.ui.track_sustain(0) == 100, "Sustain borné à 100");
        f.sink.sent.clear();
        f.hold(6, 1);
        CHECK(f.sink.sent.empty(), "Sustain à 100 : rien d'envoyé");

        // File pleine : la valeur ne change pas.
        f.sink.budget = 0;
        f.hold(4, 3);
        CHECK(f.ui.track_attack(0) == 0, "file pleine : valeur inchangée");
    }
    {
        // Une valeur qui n'est pas dans la liste (venue d'un fichier) : le cran va à la plus proche dans le sens du mouvement.
        RecordingSink sink;
        FakeLibrary   lib;
        FakeStore     store;
        EngineState   state;
        store.file = ProfileData{};
        store.file.add_sample("Kick1.wav");
        store.file.loop_count           = 1;
        store.file.loops[0].track_count = 1;
        store.file.loops[0].tracks[0].attack  = 12;
        store.file.loops[0].tracks[0].decay   = 12;
        store.file.loops[0].tracks[0].release = 1800;
        store.exists = true;
        UiController ui(sink, &lib, &store);
        ui.load_profile();
        ui.update(state, 1000);
        ui.handle(InputEvent::wheel(-1));
        ui.handle(InputEvent::ok());
        ui.handle(InputEvent::ok());
        ui.update(state, 1010);
        ui.handle(InputEvent::ok());
        ui.handle(InputEvent::black(4));
        ui.handle(InputEvent::wheel(1));
        CHECK(ui.track_attack(0) == 15, "12 ms : le cran en avant va à 15");
        ui.handle(InputEvent::black_up(4));
        ui.handle(InputEvent::black(5));
        ui.handle(InputEvent::wheel(-1));
        CHECK(ui.track_decay(0) == 10, "12 ms : le cran en arrière va à 10");
        ui.handle(InputEvent::black_up(5));
        ui.handle(InputEvent::black(7));
        ui.handle(InputEvent::wheel(1));
        CHECK(ui.track_release(0) == 2000, "1800 ms : le cran en avant va à 2000");
        ui.handle(InputEvent::wheel(-2));
        CHECK(ui.track_release(0) == 1000, "puis 1500, puis 1000");
        ui.handle(InputEvent::black_up(7));
    }

    // ----- La forme de l'enveloppe à l'écran -----
    {
        Fixture f;
        Canvas  c;
        f.feed({InputEvent::black(7)});  // Release tenu : A 0, D 0, S 100, R 5
        f.ui.render(c);
        // Largeurs : A et D = 1 pixel, palier = 20, R(5 ms) = 8 : total 30, départ à x = 49 ; sommet à y = 16, base à y = 46.
        CHECK(c.get_pixel(49, 46) && c.get_pixel(50, 16) && c.get_pixel(71, 16) && c.get_pixel(79, 46), "forme : départ, sommet, fin du palier, retour à zéro");
        CHECK(c.get_pixel(60, 16) && !c.get_pixel(60, 17) && !c.get_pixel(60, 15), "sustain 100 % : palier tout en haut");
        CHECK(c.get_pixel(49, 47) && c.get_pixel(53, 47) && !c.get_pixel(50, 47), "axe du temps en pointillés sous la base");
        f.feed({InputEvent::black_up(7)});

        // Sustain 50 % : le palier est à mi-hauteur (y = 31).
        f.hold(6, -10);
        f.feed({InputEvent::black(6)});
        f.ui.render(c);
        CHECK(f.ui.track_sustain(0) == 50 && c.get_pixel(60, 31) && !c.get_pixel(60, 16), "sustain 50 % : palier à mi-hauteur");
        f.feed({InputEvent::black_up(6)});

        // Attack 100 ms : la montée s'étale (17 pixels), le sommet recule d'autant.
        f.hold(4, 13);
        f.feed({InputEvent::black(4)});
        f.ui.render(c);
        // A 100 ms : 17 ; D 1 ; R 8 ; total 17 + 1 + 20 + 8 = 46 ; départ x = 41 ; sommet en x = 58.
        CHECK(f.ui.track_attack(0) == 100 && c.get_pixel(41, 46) && c.get_pixel(58, 16), "attack 100 ms : montée sur 17 pixels");
        f.feed({InputEvent::black_up(4)});
    }
    {
        // Le segment réglé est en gras, les autres non ; la valeur réglée est en négatif en bas.
        Fixture f;
        f.hold(5, 13);  // Decay 100 ms
        f.hold(6, -10);  // Sustain 50 %
        Canvas atk, dec, sus, rel;
        f.feed({InputEvent::black(4)});
        f.ui.render(atk);
        f.feed({InputEvent::black(5)});
        f.ui.render(dec);
        f.feed({InputEvent::black(6)});
        f.ui.render(sus);
        f.feed({InputEvent::black(7)});
        f.ui.render(rel);
        f.feed({InputEvent::black_up(7), InputEvent::black_up(6), InputEvent::black_up(5), InputEvent::black_up(4)});
        auto count = [](const Canvas& c) { return lit(c, 0, 12, 128, 36); };
        // Chaque rendu met un segment différent en gras : mêmes segments, épaisseurs différentes.
        CHECK(count(atk) != count(dec) || count(dec) != count(sus) || count(sus) != count(rel), "le segment réglé change avec la touche");
        CHECK(count(atk) > 0 && count(dec) > 0 && count(sus) > 0 && count(rel) > 0, "la forme est dessinée pour chaque réglage");

        // Texte du bas, avec les unités : « A0ms D.1s S50% R5ms », la valeur réglée en négatif.
        Canvas ref;
        const char* items[4] = {"A0ms", "D.1s", "S50%", "R5ms"};
        int         x        = 0;
        for (int k = 0; k < 4; ++k) {
            ref.text(x, 52, items[k], k == 2);
            x += Canvas::text_width(items[k]) + 2;
        }
        bool same = true;
        for (int y = 52; y < 60; ++y)
            for (int xx = 0; xx < Canvas::W; ++xx) same = same && sus.get_pixel(xx, y) == ref.get_pixel(xx, y);
        CHECK(same, "bas de l'écran : les quatre valeurs, celle du Sustain (réglée) en négatif");
    }

    // ----- Les durées en texte : unités, au plus 4 caractères, tout tient sur l'écran -----
    {
        // Valeurs de départ choisies par un profil : chaque durée s'affiche avec sa bonne unité.
        struct Case { int ms; const char* text; };
        const Case cases[] = {{0, "0ms"}, {1, "1ms"}, {5, "5ms"}, {99, "99ms"}, {100, ".1s"}, {150, ".15s"}, {300, ".3s"}, {123, ".12s"}, {304, ".3s"},
                              {305, ".31s"}, {994, ".99s"}, {995, "1s"}, {999, "1s"}, {1000, "1s"}, {1500, "1.5s"}, {1540, "1.5s"}, {1550, "1.6s"}, {1990, "2s"}, {2000, "2s"}};
        bool all = true;
        for (const Case& k : cases) {
            RecordingSink sink;
            FakeLibrary   lib;
            FakeStore     store;
            EngineState   state;
            store.file = ProfileData{};
            store.file.add_sample("Kick1.wav");
            store.file.loop_count                 = 1;
            store.file.loops[0].track_count       = 1;
            store.file.loops[0].tracks[0].attack  = static_cast<uint16_t>(k.ms);
            store.exists = true;
            UiController ui(sink, &lib, &store);
            ui.load_profile();
            ui.update(state, 0);
            ui.handle(InputEvent::wheel(-1));
            ui.handle(InputEvent::ok());
            ui.handle(InputEvent::ok());
            ui.update(state, 10);
            ui.handle(InputEvent::ok());
            ui.handle(InputEvent::black(4));
            Canvas c, ref;
            ui.render(c);
            char item[16];
            std::snprintf(item, sizeof item, "A%s", k.text);
            ref.text(0, 52, item, true);
            bool same = true;
            for (int y = 52; y < 60; ++y)
                for (int x = 0; x < Canvas::text_width(item); ++x) same = same && c.get_pixel(x, y) == ref.get_pixel(x, y);
            if (!same) std::printf("  (durée %d ms : « %s » attendu)\n", k.ms, k.text);
            all = all && same;
        }
        CHECK(all, "durées en texte court : ms sous 100, .1s à .99s, 1s à 2s");
    }
    {
        // Le pire cas tient dans l'écran : trois durées de 4 caractères et un sustain de 100 %.
        RecordingSink sink;
        FakeLibrary   lib;
        FakeStore     store;
        EngineState   state;
        store.file = ProfileData{};
        store.file.add_sample("Kick1.wav");
        store.file.loop_count                 = 1;
        store.file.loops[0].track_count       = 1;
        store.file.loops[0].tracks[0].attack  = 150;
        store.file.loops[0].tracks[0].decay   = 70;
        store.file.loops[0].tracks[0].release = 1500;
        store.exists = true;
        UiController ui(sink, &lib, &store);
        ui.load_profile();
        ui.update(state, 0);
        ui.handle(InputEvent::wheel(-1));
        ui.handle(InputEvent::ok());
        ui.handle(InputEvent::ok());
        ui.update(state, 10);
        ui.handle(InputEvent::ok());
        ui.handle(InputEvent::black(7));
        Canvas c;
        ui.render(c);
        const char* items[4] = {"A.15s", "D70ms", "S100%", "R1.5s"};
        int         total    = 0;
        for (int k = 0; k < 4; ++k) total += Canvas::text_width(items[k]) + (k < 3 ? 2 : 0);
        CHECK(total <= Canvas::W, "pire cas : la rangée des quatre valeurs tient en 128 pixels");
        Canvas ref;
        int    x = 0;
        for (int k = 0; k < 4; ++k) {
            ref.text(x, 52, items[k], k == 3);
            x += Canvas::text_width(items[k]) + 2;
        }
        bool same = true;
        for (int y = 52; y < 60; ++y)
            for (int xx = 0; xx < Canvas::W; ++xx) same = same && c.get_pixel(xx, y) == ref.get_pixel(xx, y);
        CHECK(same, "rangée complète : A.15s D70ms S100% R1.5s, le Release en négatif");
    }

    // ----- Chargement et sauvegarde -----
    {
        RecordingSink sink;
        FakeLibrary   lib;
        FakeStore     store;
        EngineState   state;
        store.file = ProfileData{};
        store.file.add_sample("Kick1.wav");
        store.file.loop_count           = 1;
        store.file.loops[0].bpm         = 120.0f;
        store.file.loops[0].track_count = 2;
        store.file.loops[0].tracks[0].pattern = 0x0001;
        store.file.loops[0].tracks[0].attack  = 20;
        store.file.loops[0].tracks[0].decay   = 300;
        store.file.loops[0].tracks[0].sustain = 60;
        store.file.loops[0].tracks[0].release = 0;
        store.file.loops[0].tracks[1].pattern = 0x0002;  // enveloppe par défaut : aucune commande
        store.exists = true;
        UiController ui(sink, &lib, &store);
        ui.load_profile();
        ui.update(state, 1000);
        ui.handle(InputEvent::wheel(-1));
        ui.handle(InputEvent::ok());
        ui.handle(InputEvent::ok());
        ui.update(state, 1010);
        CHECK(ui.track_attack(0) == 20 && ui.track_decay(0) == 300 && ui.track_sustain(0) == 60 && ui.track_release(0) == 0 && ui.track_release(1) == 5 &&
                  ui.track_sustain(1) == 100,
              "chargement : enveloppe de chaque piste dans le modèle");
        CHECK(sink.count(Command::SetTrackAttack) == 1 && sink.last(Command::SetTrackAttack).value == 20.0f && sink.count(Command::SetTrackDecay) == 1 &&
                  sink.last(Command::SetTrackDecay).value == 300.0f && sink.count(Command::SetTrackSustain) == 1 && sink.last(Command::SetTrackSustain).value == 60.0f &&
                  sink.count(Command::SetTrackRelease) == 1 && sink.last(Command::SetTrackRelease).value == 0.0f,
              "une commande par réglage non défaut, la piste par défaut n'en reçoit pas");
        CHECK(!ui.dirty(), "charger ne modifie pas la loop");

        // Modifier, sauvegarder, recharger.
        ui.handle(InputEvent::ok());  // vue Pattern de la piste 1
        ui.handle(InputEvent::black(4));
        ui.handle(InputEvent::wheel(-1));  // 20 -> 15
        ui.handle(InputEvent::black_up(4));
        CHECK(ui.track_attack(0) == 15, "attack 20 -> 15");
        ui.handle(InputEvent::back());
        ui.handle(InputEvent::wheel(-5));
        ui.handle(InputEvent::menu());
        ui.handle(InputEvent::wheel(1));
        ui.handle(InputEvent::ok());
        ui.handle(InputEvent::ok());
        CHECK(!ui.dirty(), "sauvegardée");
        const TrackData& t = store.file.loops[0].tracks[0];
        CHECK(t.attack == 15 && t.decay == 300 && t.sustain == 60 && t.release == 0 && store.file.loops[0].tracks[1].release == 5 && store.file.loops[0].tracks[1].sustain == 100,
              "enveloppes sauvegardées, défauts compris");

        // Chargement par petits paquets : chaque commande part une fois.
        sink.sent.clear();
        sink.budget = 2;
        ui.handle(InputEvent::wheel(-5));
        ui.handle(InputEvent::ok());
        ui.handle(InputEvent::ok());
        for (int i = 0; i < 40 && ui.loading(); ++i) {
            sink.budget += 2;
            ui.update(state, 1100 + static_cast<uint32_t>(i));
        }
        CHECK(!ui.loading() && sink.count(Command::SetTrackAttack) == 1 && sink.count(Command::SetTrackDecay) == 1 && sink.count(Command::SetTrackSustain) == 1 &&
                  sink.count(Command::SetTrackRelease) == 1,
              "par petits paquets : chaque commande d'enveloppe une fois");
    }

    // ----- Avec le vrai moteur : l'attack réglée à la touche est entendue -----
    {
        static int16_t held[20000];
        for (int16_t& v : held) v = 16384;
        Engine       e;
        UiController ui(e);
        Command      c;
        c.type   = Command::SetTrackSample;
        c.track  = 2;
        c.data   = held;
        c.length = 20000;
        e.push(c);
        ui.add_track("a.wav", 2, 0);
        ui.update(e.state(), 0);
        ui.handle(InputEvent::ok());
        ui.handle(InputEvent::black(4));
        ui.handle(InputEvent::wheel(6));  // Attack 10 ms
        ui.handle(InputEvent::black_up(4));
        ui.handle(InputEvent::black(9));  // mode Clavier : la touche joue une note
        std::vector<float> l(600), r(600);
        e.render(l.data(), r.data(), 600);
        auto out_of = [](float v) { return std::tanh(cfg::MASTER_GAIN * v); };
        CHECK(l[0] == 0.0f && std::fabs(l[220] - out_of(0.5f * 220.0f / 441.0f)) < 1e-4f && std::fabs(l[500] - out_of(0.5f)) < 1e-4f,
              "attack de 10 ms réglée avec la touche 5 : la note monte sur 441 échantillons");
    }

    if (failures == 0) std::printf("OK : test_envelope_ui\n");
    return failures == 0 ? 0 : 1;
}
