// Balance dans l'UI : mode B, notes supplémentaires, pan et balance de la piste, chargement, sauvegarde, prise.
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../config.h"
#include "../core/engine.h"
#include "../core/engine_state.h"
#include <cmath>
#include "../core/ui/canvas.h"
#include "../core/ui/ui_controller.h"

using namespace profile;
using Mode = UiController::EditMode;
using View = UiController::View;

static int failures = 0;

struct Out_t {
    float l = 0.0f, r = 0.0f;
};
static float out_of(float v) { return std::tanh(cfg::MASTER_GAIN * v); }
static bool  near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

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

// Une loop d'une piste (sample 0), pas 0, 4, 8 et 12 joués. Pas 0 : balance -50. Pas 4 : balance +25 et deux notes
// supplémentaires (balance -45 et +10). Pas 8 : balance -20. Pas 12 : balance +35. Largeur de la piste -40 (40M), balance +30.
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
    t.step_pan[0]          = -50;
    t.step_pan[4]          = 25;
    t.extra_count[4]       = 2;
    t.extra[4][0]          = Hit{-20, 100, 3, -45};
    t.extra[4][1]          = Hit{30, 60, -2, 10};
    t.step_pan[8]          = -20;
    t.step_pan[12]         = 35;
    t.width                = -40;
    t.balance              = 30;
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
    void to_balance() { feed({InputEvent::mode(), InputEvent::mode(), InputEvent::mode(), InputEvent::mode()}); }
};

static bool same_rows(const Canvas& a, const Canvas& b, int y0, int y1, int x0 = 0, int x1 = Canvas::W) {
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
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

static StepEvent event(int track, int step, Hit main, std::initializer_list<Hit> extra = {}) {
    StepEvent ev;
    ev.track = static_cast<uint8_t>(track);
    ev.step  = static_cast<uint8_t>(step);
    ev.main  = main;
    int n    = 0;
    for (const Hit& h : extra) ev.extra[n++] = h;
    ev.extra_count = static_cast<uint8_t>(n);
    return ev;
}

struct FakeEvents : StepEventSource {
    std::vector<StepEvent> queue;
    bool                   pop_step_event(StepEvent& ev) override {
        if (queue.empty()) return false;
        ev = queue.front();
        queue.erase(queue.begin());
        return true;
    }
};

static std::string cols_of(const Canvas& c, int step) {  // colonnes intérieures 1..5 de la boîte : '#' pleine, '.' vide
    std::string out;
    for (int dx = 1; dx <= 5; ++dx) {
        const int n = lit(c, step * 8 + dx, 24 + 1, 1, 5);
        out += n == 5 ? '#' : n == 0 ? '.' : '?';
    }
    return out;
}

int main() {
    // ----- Chargement : le modèle et les commandes -----
    {
        Fixture f;
        CHECK(f.ui.view() == View::Pattern && f.ui.track_count() == 1, "loop chargée");
        CHECK(f.ui.step_pan(0, 0) == -50 && f.ui.step_pan(0, 4) == 25 && f.ui.step_pan(0, 8) == -20 && f.ui.step_pan(0, 12) == 35 && f.ui.step_pan(0, 1) == 0,
              "balances des pas dans le modèle");
        CHECK(f.ui.extra_hit(0, 4, 0).pan == -45 && f.ui.extra_hit(0, 4, 1).pan == 10 && f.ui.extra_hit(0, 4, 0).velocity == 100, "balances des notes supplémentaires");
        CHECK(f.ui.track_width(0) == -40 && f.ui.track_balance(0) == 30, "largeur et balance de la piste dans le modèle");

        CHECK(f.sink.count(Command::SetStepPan) == 4, "une commande par balance de pas non nulle");
        CHECK(f.sink.last(Command::SetStepPan, 0, 0).value == -50.0f && f.sink.last(Command::SetStepPan, 0, 12).value == 35.0f, "valeurs des balances");
        const Command e1 = f.sink.last(Command::SetExtraHit, 0, 4, 1);
        const Command e2 = f.sink.last(Command::SetExtraHit, 0, 4, 2);
        CHECK(f.sink.count(Command::SetExtraHit) == 2 && e1.pan == -45 && e1.offset == -20 && e1.value == 100.0f && e2.pan == 10 && e2.pitch == -2,
              "les notes supplémentaires partent avec leur balance");
        const Command tp = f.sink.last(Command::SetTrackWidth, 0, 0);
        const Command tb = f.sink.last(Command::SetTrackBalance, 0, 0);
        CHECK(f.sink.count(Command::SetTrackWidth) == 1 && tp.value == -40.0f && f.sink.count(Command::SetTrackBalance) == 1 && tb.value == 30.0f,
              "largeur et balance de la piste envoyées, une fois chacune");
        CHECK(!f.ui.dirty(), "charger ne modifie pas la loop");
    }
    {
        // Chargement par petits paquets : chaque commande de pan part exactement une fois.
        Fixture f;
        f.sink.sent.clear();
        f.sink.budget = 3;
        f.feed({InputEvent::back(), InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});
        CHECK(f.ui.loading(), "chargement en cours");
        for (int i = 0; i < 40 && f.ui.loading(); ++i) {
            f.sink.budget += 3;
            f.tick(10);
        }
        CHECK(!f.ui.loading() && f.sink.count(Command::SetStepPan) == 4 && f.sink.count(Command::SetExtraHit) == 2 && f.sink.count(Command::SetTrackWidth) == 1 &&
                  f.sink.count(Command::SetTrackBalance) == 1,
              "toutes les commandes de pan finissent par partir, une fois chacune");
    }

    // ----- Mode B : cycle, lettre, jauge -----
    {
        Fixture f;
        f.to_balance();
        CHECK(f.ui.edit_mode() == Mode::Balance, "Edit -> V -> P -> T -> B");
        Canvas c, ref;
        f.ui.render(c);
        const int bpm_x = Canvas::W - 6 - 4 - Canvas::text_width("120");
        ref.text(bpm_x - 12, 0, "B");
        CHECK(lit(c, bpm_x - 12, 0, 6, 8) == lit(ref, bpm_x - 12, 0, 6, 8) && lit(c, bpm_x - 12, 0, 6, 8) > 0, "lettre B dans l'en-tête");
        CHECK(lit(c, 0, 47, 128, 17) == 0, "pas de sélection : pas de jauge");
        f.feed({InputEvent::step(1)});
        CHECK(f.ui.selected_step() == -1, "pas éteint : non sélectionné");

        f.feed({InputEvent::step(4)});  // +25 : 24 pixels à droite du centre
        f.ui.render(c);
        CHECK(value_is(c, "R25") && lit(c, 51, 49, 24, 5) == 24 * 5 && lit(c, 75, 49, 24, 5) == 0 && lit(c, 2, 49, 48, 5) == 0, "jauge à droite, « R25 »");
        f.feed({InputEvent::step(0)});  // -50 : 48 pixels à gauche
        f.ui.render(c);
        CHECK(value_is(c, "L50") && lit(c, 2, 49, 48, 5) == 48 * 5 && lit(c, 51, 49, 40, 5) == 0, "jauge à gauche, « L50 »");
        f.feed({InputEvent::step(8)});  // -20 : 19 pixels (20 x 48 / 50 = 19,2)
        f.ui.render(c);
        CHECK(value_is(c, "L20") && lit(c, 31, 49, 19, 5) == 19 * 5 && lit(c, 30, 49, 1, 5) == 0, "jauge : L20");

        // Au centre : « C », aucune barre, seulement le trait central de la jauge.
        FakeEvents ev;
        f.ui.set_event_source(&ev);
        ev.queue.push_back(event(0, 3, Hit{0, 127, 0, 0}));
        f.tick(10);
        f.feed({InputEvent::step(3)});
        f.ui.render(c);
        CHECK(value_is(c, "C") && lit(c, 1, 49, 49, 5) == 0 && lit(c, 51, 49, 48, 5) == 0 && lit(c, 50, 47, 1, 9) == 9, "au centre : « C », aucune barre");
    }

    // ----- Boîtes en mode B -----
    {
        Fixture f;
        FakeEvents ev;
        f.ui.set_event_source(&ev);
        // Seuils : +-9 -> rien ; +-10 et +-29 -> une colonne ; +-30 -> deux colonnes.
        const int pans[][2] = {{1, 9}, {2, 10}, {3, 29}, {5, 30}, {6, -9}, {7, -10}, {9, -29}, {10, -30}};
        for (const auto& p : pans) ev.queue.push_back(event(0, p[0], Hit{0, 127, 0, static_cast<int8_t>(p[1])}));
        f.tick(10);
        f.to_balance();
        Canvas c;
        f.ui.render(c);
        CHECK(cols_of(c, 0) == "###..", "pas 0 (-50) : deux colonnes à gauche, plus le trait central");
        CHECK(cols_of(c, 4) == "..##.", "pas 4 (+25) : une colonne à droite");
        CHECK(cols_of(c, 8) == ".##..", "pas 8 (-20) : une colonne à gauche");
        CHECK(cols_of(c, 12) == "..###", "pas 12 (+35) : deux colonnes à droite");
        CHECK(cols_of(c, 1) == "..#..", "+9 : seulement le trait central");
        CHECK(cols_of(c, 2) == "..##." && cols_of(c, 3) == "..##.", "+10 et +29 : une colonne");
        CHECK(cols_of(c, 5) == "..###", "+30 : deux colonnes");
        CHECK(cols_of(c, 6) == "..#..", "-9 : seulement le trait central");
        CHECK(cols_of(c, 7) == ".##.." && cols_of(c, 9) == ".##..", "-10 et -29 : une colonne");
        CHECK(cols_of(c, 10) == "###..", "-30 : deux colonnes");
        CHECK(c.get_pixel(0, 24) && c.get_pixel(6, 30) && c.get_pixel(0, 27) && c.get_pixel(3, 24 + 6), "contour dessiné");
        CHECK(lit(c, 11 * 8, 24, 7, 7) == 24 && lit(c, 13 * 8, 24, 7, 7) == 24, "un pas éteint : contour vide, sans trait ni remplissage");
    }

    // ----- La roue : toutes les notes du pas, avec le même écart -----
    {
        Fixture f;
        f.to_balance();
        f.feed({InputEvent::step(0)});
        f.sink.sent.clear();
        f.feed({InputEvent::wheel(-3)});
        CHECK(f.sink.sent.empty() && !f.ui.dirty() && f.ui.step_pan(0, 0) == -50, "déjà à -50 : rien d'envoyé");
        f.tick(100);
        f.feed({InputEvent::wheel(3)});
        CHECK(f.ui.step_pan(0, 0) == -44 && f.sink.last(Command::SetStepPan, 0, 0).value == -44.0f && f.ui.dirty(), "+3 crans = +6 points");
        CHECK(f.sink.count(Command::AuditionStep) == 1, "la balance change le son : note d'essai");
        CHECK(f.ui.step_velocity(0, 0) == 127 && f.ui.step_pitch(0, 0) == 0 && f.ui.step_offset(0, 0) == 0, "les autres valeurs du pas ne bougent pas");

        // Pas 4 : principale +25, supplémentaires -45 et +10.
        f.tick(100);
        f.feed({InputEvent::step(4)});
        f.sink.sent.clear();
        f.feed({InputEvent::wheel(1)});
        CHECK(f.ui.step_pan(0, 4) == 27 && f.ui.extra_hit(0, 4, 0).pan == -43 && f.ui.extra_hit(0, 4, 1).pan == 12, "le même écart pour toutes les notes");
        const Command e1 = f.sink.last(Command::SetExtraHit, 0, 4, 1);
        const Command e2 = f.sink.last(Command::SetExtraHit, 0, 4, 2);
        CHECK(f.sink.count(Command::SetStepPan) == 1 && f.sink.count(Command::SetExtraHit) == 2 && e1.pan == -43 && e1.offset == -20 && e1.value == 100.0f &&
                  e1.pitch == 3 && e2.pan == 12 && e2.offset == 30 && e2.value == 60.0f && e2.pitch == -2,
              "commandes pour la note principale et les deux supplémentaires, autres champs intacts");
        // Limite : la note la plus haute (27) arrête la roue vers la droite, la plus basse (-43) vers la gauche.
        f.tick(100);
        f.feed({InputEvent::wheel(100)});
        CHECK(f.ui.step_pan(0, 4) == 50 && f.ui.extra_hit(0, 4, 0).pan == -20 && f.ui.extra_hit(0, 4, 1).pan == 35, "bornée par la note la plus à droite : écarts conservés");
        f.tick(100);
        f.feed({InputEvent::wheel(-200)});
        CHECK(f.ui.step_pan(0, 4) == 20 && f.ui.extra_hit(0, 4, 0).pan == -50 && f.ui.extra_hit(0, 4, 1).pan == 5, "bornée par la note la plus à gauche : écarts conservés");

        // Les autres modes gardent la balance des notes supplémentaires dans leurs commandes.
        f.feed({InputEvent::mode()});  // Edit
        f.feed({InputEvent::mode()});  // Velocity
        f.tick(100);
        f.sink.sent.clear();
        f.feed({InputEvent::wheel(-1)});
        const Command v1 = f.sink.last(Command::SetExtraHit, 0, 4, 1);
        const Command v2 = f.sink.last(Command::SetExtraHit, 0, 4, 2);
        CHECK(v1.pan == -50 && v2.pan == 5 && f.ui.extra_hit(0, 4, 0).pan == -50 && f.ui.extra_hit(0, 4, 1).pan == 5 && f.ui.step_pan(0, 4) == 20,
              "vélocité : la balance des notes supplémentaires est renvoyée telle quelle");

        // File pleine : le modèle suit ce que le moteur a reçu.
        Fixture g;
        g.to_balance();
        g.feed({InputEvent::step(0)});
        g.sink.budget = 0;
        g.feed({InputEvent::wheel(3)});
        CHECK(g.ui.step_pan(0, 0) == -50 && !g.ui.dirty(), "file pleine : valeur inchangée");
    }

    // ----- Sauvegarde et rechargement : tout est conservé -----
    {
        Fixture f;
        f.to_balance();
        f.feed({InputEvent::step(0), InputEvent::wheel(3)});  // -50 -> -44
        f.feed({InputEvent::black(3), InputEvent::wheel(1), InputEvent::black_up(3)});  // balance de piste 30 -> 35 (touche noire tenue)
        CHECK(f.ui.track_balance(0) == 35, "balance de piste réglée avant la sauvegarde");
        f.feed({InputEvent::back()});
        f.feed({InputEvent::wheel(-1), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok(), InputEvent::ok()});
        CHECK(!f.ui.dirty(), "sauvegardée");
        const TrackData& t = f.store.file.loops[0].tracks[0];
        CHECK(t.step_pan[0] == -44 && t.step_pan[4] == 25 && t.step_pan[8] == -20 && t.step_pan[12] == 35 && t.step_pan[1] == 0, "balances des pas sauvegardées");
        CHECK(t.extra_count[4] == 2 && t.extra[4][0].pan == -45 && t.extra[4][1].pan == 10 && t.extra[4][0].velocity == 100, "balances des notes supplémentaires sauvegardées");
        CHECK(t.width == -40 && t.balance == 35, "largeur et balance de la piste sauvegardées");

        Fixture g;
        g.store.file = f.store.file;
        g.ui.load_profile();
        g.feed({InputEvent::back(), InputEvent::wheel(-1), InputEvent::ok(), InputEvent::ok()});
        g.tick(10);
        CHECK(g.ui.step_pan(0, 0) == -44 && g.ui.extra_hit(0, 4, 0).pan == -45 && g.ui.track_width(0) == -40 && g.ui.track_balance(0) == 35, "rechargé : mêmes valeurs");
    }

    // ----- Prise : le modèle suit les événements du moteur -----
    {
        Fixture f;
        FakeEvents ev;
        f.ui.set_event_source(&ev);
        ev.queue.push_back(event(0, 7, Hit{-5, 90, 2, -33}, {Hit{4, 80, 0, 18}}));
        f.tick(10);
        CHECK(f.ui.step_pan(0, 7) == -33 && f.ui.extra_count(0, 7) == 1 && f.ui.extra_hit(0, 7, 0).pan == 18 && f.ui.step_offset(0, 7) == -5 && f.ui.dirty(),
              "événement de prise : balances copiées");
    }

    // ----- Annuler la prise : les balances d'origine sont renvoyées -----
    {
        Fixture f;
        FakeEvents ev;
        f.ui.set_event_source(&ev);
        f.feed({InputEvent::black(9)});  // vue Clavier
        f.sink.sent.clear();
        f.feed({InputEvent::play()});
        f.state.record_phase.store(2);
        f.state.playing.store(true);
        f.tick(10);
        ev.queue.push_back(event(0, 0, Hit{0, 127, 0, 12}));
        ev.queue.push_back(event(0, 4, Hit{0, 127, 0, -7}, {Hit{1, 90, 0, 40}}));
        f.tick(10);
        CHECK(f.ui.step_pan(0, 0) == 12 && f.ui.step_pan(0, 4) == -7 && f.ui.extra_hit(0, 4, 0).pan == 40, "la prise a changé les balances");
        f.sink.sent.clear();
        f.feed({InputEvent::menu(), InputEvent::ok()});
        CHECK(f.ui.step_pan(0, 0) == -50 && f.ui.step_pan(0, 4) == 25 && f.ui.extra_hit(0, 4, 0).pan == -45 && f.ui.extra_hit(0, 4, 1).pan == 10,
              "modèle remis dans l'état d'avant la prise");
        CHECK(f.sink.count(Command::SetStepPan) == 16, "une commande de balance par pas");
        CHECK(f.sink.last(Command::SetStepPan, 0, 0).value == -50.0f && f.sink.last(Command::SetStepPan, 0, 4).value == 25.0f &&
                  f.sink.last(Command::SetStepPan, 0, 8).value == -20.0f && f.sink.last(Command::SetStepPan, 0, 1).value == 0.0f,
              "valeurs d'origine des balances");
        CHECK(f.sink.count(Command::SetExtraHit) == 2 && f.sink.last(Command::SetExtraHit, 0, 4, 1).pan == -45 && f.sink.last(Command::SetExtraHit, 0, 4, 2).pan == 10,
              "notes supplémentaires renvoyées avec leur balance");
        CHECK(f.ui.track_width(0) == -40 && f.ui.track_balance(0) == 30, "largeur et balance de la piste : inchangées par la prise");
    }

    // ----- Avec le vrai moteur : de bout en bout -----
    {
        static const int16_t mono[]   = {16384, 0};
        static const int16_t stereo[] = {16384, 8192, 0, 0};
        auto run = [](const int16_t* data, int channels, int line, int wheel_notches, Out_t* out) {
            Engine       e;
            UiController ui(e);
            ui.set_event_source(&e);
            Command c;
            c.type     = Command::SetTrackSample;
            c.track    = 2;
            c.data     = data;
            c.length   = 2;
            c.channels = static_cast<uint8_t>(channels);
            e.push(c);
            ui.add_track("a.wav", 2, 0);
            std::vector<float> l(256), r(256);
            ui.update(e.state(), 0);
            ui.handle(InputEvent::ok());      // vue Pattern
            ui.handle(InputEvent::black(line));  // touche noire tenue : 2 = Wide, 3 = Balance
            ui.handle(InputEvent::wheel(wheel_notches));
            ui.handle(InputEvent::black_up(line));
            ui.handle(InputEvent::black(9));  // ouvre le clavier et joue la note
            e.render(l.data(), r.data(), 256);
            out->l = l[0];
            out->r = r[0];
        };
        Out_t o;
        run(mono, 1, 3, -10, &o);  // balance de la piste à -50 (touche noire 3 tenue)
        CHECK(near(o.l, out_of(0.5f * 0.0f + 0.5f)) && o.r == 0.0f, "balance de piste L50 réglée avec la touche noire tenue : la note sort à gauche seulement");
        run(mono, 1, 3, 10, &o);
        CHECK(o.l == 0.0f && near(o.r, out_of(0.5f)), "balance de piste R50 : à droite seulement");
        run(stereo, 2, 2, -20, &o);  // touche noire 2 (Wide) : 100M, mono complet d'un sample stéréo : (0,5 + 0,25) / 2 des deux côtés
        CHECK(near(o.l, out_of(0.375f)) && near(o.r, out_of(0.375f)), "largeur 100M d'un sample stéréo : les deux canaux deviennent la moyenne");
        run(stereo, 2, 2, 20, &o);  // 100W : côtés doublés
        CHECK(near(o.l, out_of(0.625f)) && near(o.r, out_of(0.125f)), "largeur 100W : côtés doublés");
        run(stereo, 2, 2, 0, &o);
        CHECK(near(o.l, out_of(0.5f)) && near(o.r, out_of(0.25f)), "au centre : image conservée");
    }

    if (failures == 0) std::printf("OK : test_balance_ui\n");
    return failures == 0 ? 0 : 1;
}
