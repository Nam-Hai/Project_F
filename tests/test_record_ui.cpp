// Enregistrement dans la vue Clavier : Play, pré-compte, prise, événements du moteur, annulation de la prise.
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <utility>
#include <vector>

#include "../config.h"
#include "../core/engine.h"
#include "../core/engine_state.h"
#include "../core/ui/canvas.h"
#include "../core/ui/ui_controller.h"

static int failures = 0;

#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            std::printf("FAIL: %s\n", msg);         \
            ++failures;                             \
        }                                           \
    } while (0)

using View = UiController::View;

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
    Command at(std::size_t i) const { return i < sent.size() ? sent[i] : Command{}; }
};

struct FakeEvents : StepEventSource {
    std::deque<StepEvent> queue;
    bool pop_step_event(StepEvent& ev) override {
        if (queue.empty()) return false;
        ev = queue.front();
        queue.pop_front();
        return true;
    }
};

struct Fixture {
    RecordingSink sink;
    FakeEvents    events;
    EngineState   state;
    UiController  ui{sink};
    uint32_t      now = 1000;

    Fixture() {
        for (auto& n : state.triggers) n.store(0);
        ui.set_event_source(&events);
        // Une piste sur la piste moteur 2 : pas 0 et 4, vélocité 100 au pas 0, décalage 7 au pas 4.
        uint8_t vel[16];
        for (auto& v : vel) v = 127;
        vel[0] = 100;
        ui.add_track("a.wav", 2, 0x0011, 1.0f, 1.0f, vel);
        ui.update(state, now);
        feed({InputEvent::ok(), InputEvent::black(9)});  // vue Pattern, puis vue Clavier
        sink.sent.clear();
    }
    void tick(uint32_t ms = 10) {
        now += ms;
        ui.update(state, now);
    }
    void feed(std::initializer_list<InputEvent> e) {
        for (const InputEvent& x : e) ui.handle(x);
    }
    void phase(int p, int precount = -1, int step = -1) {
        state.record_phase.store(p);
        state.precount_step.store(precount);
        state.step.store(step);
        state.playing.store(p != 0);
        tick();
    }
};

static bool same_rows(const Canvas& a, const Canvas& b, int y0, int y1, int x0 = 0, int x1 = Canvas::W) {
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (a.get_pixel(x, y) != b.get_pixel(x, y)) return false;
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

int main() {
    // ----- Play : lancer et arrêter la prise -----
    {
        Fixture f;
        CHECK(f.ui.view() == View::Keyboard && f.ui.record_phase() == 0, "vue Clavier, au repos");
        f.feed({InputEvent::play()});
        CHECK(f.sink.sent.size() == 1 && f.sink.at(0).type == Command::StartRecord && f.sink.at(0).track == 2 && f.sink.count(Command::Play) == 0,
              "Play : StartRecord sur la piste moteur du clavier");
        CHECK(f.ui.can_undo_take(), "la piste est copiée au lancement");
        f.phase(1);
        f.feed({InputEvent::play()});
        CHECK(f.sink.count(Command::StopRecord) == 1, "Play pendant le pré-compte : StopRecord");
        f.phase(2, -1, 3);
        f.feed({InputEvent::play()});
        CHECK(f.sink.count(Command::StopRecord) == 2 && f.sink.count(Command::StartRecord) == 1, "Play pendant la prise : StopRecord");
        f.phase(0);
        f.feed({InputEvent::play()});
        CHECK(f.sink.count(Command::StartRecord) == 2, "de nouveau au repos : une nouvelle prise");

        // File pleine : rien ne démarre.
        Fixture g;
        g.sink.budget = 0;
        g.feed({InputEvent::play()});
        CHECK(g.ui.record_phase() == 0 && !g.ui.can_undo_take(), "file pleine : pas de prise");

        // Play hors de la vue Clavier : comme avant.
        Fixture h;
        h.feed({InputEvent::back(), InputEvent::play()});
        CHECK(h.sink.count(Command::Play) == 1 && h.sink.count(Command::StartRecord) == 0, "Play dans les autres vues : lecture simple");
    }

    // ----- Quitter la vue arrête la prise -----
    {
        Fixture f;
        f.feed({InputEvent::play()});
        f.phase(2, -1, 0);
        f.feed({InputEvent::back()});
        CHECK(f.ui.view() == View::Pattern && f.sink.count(Command::StopRecord) == 1, "Back pendant la prise : StopRecord");
        Fixture g;
        g.feed({InputEvent::back()});
        CHECK(g.sink.count(Command::StopRecord) == 0, "Back au repos : rien");
    }

    // ----- Affichage : KEY, décompte, REC dans l'en-tête ; la timeline du pattern -----
    {
        Fixture f;
        Canvas  c, ref;
        const int bpm_x = Canvas::W - 6 - 4 - Canvas::text_width("120");
        auto header_is = [&](const char* text) {
            ref.clear();
            ref.text(bpm_x - 6 - Canvas::CHAR_W * static_cast<int>(std::strlen(text)), 0, text);
            for (int y = 0; y < 8; ++y)
                for (int x = 50; x < bpm_x - 2; ++x)
                    if (c.get_pixel(x, y) != ref.get_pixel(x, y)) return false;
            return true;
        };
        f.ui.render(c);
        CHECK(header_is("KEY"), "au repos : KEY");

        f.feed({InputEvent::play()});
        const struct { int precount; const char* text; } counts[] = {{-1, "PRE 4"}, {0, "PRE 4"}, {3, "PRE 4"}, {4, "PRE 3"},
                                                                  {8, "PRE 2"}, {12, "PRE 1"}, {15, "PRE 1"}};
        for (const auto& k : counts) {
            f.phase(1, k.precount, k.precount < 0 ? 0 : k.precount);
            f.ui.render(c);
            char msg[64];
            std::snprintf(msg, sizeof msg, "décompte au pas %d du pré-compte : %s", k.precount, k.text);
            CHECK(header_is(k.text), msg);
        }

        // Pendant la prise : REC, et la timeline montre les pas qui se remplissent (0x0011 au départ).
        f.phase(2, -1, 4);
        f.ui.render(c);
        CHECK(header_is("REC"), "REC pendant la prise");
        auto box = [&](int step) {
            int n = 0;
            for (int y = 24; y < 31; ++y)
                for (int x = step * 8; x < step * 8 + 7; ++x) n += c.get_pixel(x, y);
            return n;
        };
        CHECK(box(0) == 24 && box(4) == 24 && box(7) == 24, "timeline : le pattern de la piste a été effacé au lancement de la prise");
        CHECK(c.get_pixel(4 * 8 + 3, 24 + 10) && !c.get_pixel(3 * 8 + 3, 24 + 10), "trait sous le pas en cours de lecture");
        f.events.queue.push_back(event(2, 7, Hit{0, 127, 0}));
        f.tick();
        f.ui.render(c);
        CHECK(box(7) == 49, "un pas enregistré s'allume dans la timeline");

        f.phase(0);
        f.ui.render(c);
        CHECK(header_is("KEY"), "fin de la prise : KEY");
    }

    // ----- Événements du moteur : le modèle suit -----
    {
        Fixture f;
        f.feed({InputEvent::play()});
        f.phase(2, -1, 0);
        CHECK(f.ui.dirty() && f.ui.pattern_bits(0) == 0, "lancer la prise a effacé le pattern (loop modifiée), avant tout événement");
        f.events.queue.push_back(event(2, 7, Hit{-20, 90, 3}, {Hit{30, 60, -2}, Hit{5, 50, 0}}));
        f.tick();
        CHECK(f.ui.pattern_bits(0) == 0x0080 && f.ui.step_offset(0, 7) == -20 && f.ui.step_velocity(0, 7) == 90 && f.ui.step_pitch(0, 7) == 3 &&
                  f.ui.extra_count(0, 7) == 2 && f.ui.extra_hit(0, 7, 0).offset == 30 && f.ui.extra_hit(0, 7, 0).velocity == 60 &&
                  f.ui.extra_hit(0, 7, 0).pitch == -2 && f.ui.extra_hit(0, 7, 1).offset == 5 && f.ui.dirty(),
              "événement : pas allumé, note principale et notes supplémentaires copiées, loop modifiée");
        // Le même pas, remplacé par un événement plus récent.
        f.events.queue.push_back(event(2, 7, Hit{1, 127, 0}));
        f.tick();
        CHECK(f.ui.extra_count(0, 7) == 0 && f.ui.step_offset(0, 7) == 1 && f.ui.step_velocity(0, 7) == 127, "un nouvel événement écrase l'état du pas");
        // Piste inconnue, pas hors limites, notes en trop : ignorés.
        f.events.queue.push_back(event(9, 1, Hit{}));
        f.events.queue.push_back(event(2, 16, Hit{}));
        StepEvent bad = event(2, 2, Hit{});
        bad.extra_count = 9;
        f.events.queue.push_back(bad);
        f.tick();
        CHECK(f.ui.pattern_bits(0) == 0x0080, "événements invalides ignorés");

        // Pertes : message.
        f.state.record_lost.store(3);
        f.tick();
        CHECK(std::strcmp(f.ui.notice(), " Frappes perdues") == 0 && f.ui.error_shown(), "frappes perdues : message");
        f.tick(5000);
        f.state.record_lost.store(3);
        f.tick();
        CHECK(!f.ui.error_shown(), "message une seule fois par perte");

        // Sans source d'événements : rien ne plante.
        RecordingSink sink;
        UiController  ui(sink);
        ui.add_track("a.wav", 2, 0x0011);
        EngineState st;
        ui.update(st, 100);
        CHECK(ui.pattern_bits(0) == 0x0011, "sans source d'événements");
    }

    // ----- Lancer la prise efface les pas de la piste, pas ses réglages -----
    {
        RecordingSink sink;
        FakeEvents    events;
        EngineState   state;
        UiController  ui(sink);
        ui.set_event_source(&events);
        uint8_t vel[16];
        int8_t  pit[16] = {};
        for (auto& v : vel) v = 127;
        vel[3] = 60;
        pit[3] = 5;
        ui.add_track("a.wav", 2, 0x0049, 0.5f, 1.0f, vel, pit);
        ui.update(state, 0);
        ui.handle(InputEvent::ok());
        // Réglages de la piste : quantification 25, largeur -40, balance +15 (touches noires tenues).
        for (auto k : {std::pair<int, int>{1, 5}, {2, -8}, {3, 3}}) {
            ui.handle(InputEvent::black(k.first));
            ui.handle(InputEvent::wheel(k.second));
            ui.handle(InputEvent::black_up(k.first));
        }
        events.queue.push_back(event(2, 7, Hit{-20, 90, 3, -33}, {Hit{30, 60, -2, 18}}));
        ui.update(state, 10);
        CHECK(ui.step_pan(0, 7) == -33 && ui.extra_count(0, 7) == 1 && ui.step_pitch(0, 3) == 5, "avant : des valeurs partout");

        ui.handle(InputEvent::black(9));
        ui.handle(InputEvent::play());
        bool defaults = ui.pattern_bits(0) == 0;
        for (int st = 0; st < 16; ++st)
            defaults = defaults && ui.step_velocity(0, st) == 127 && ui.step_pitch(0, st) == 0 && ui.step_pan(0, st) == 0 && ui.step_offset(0, st) == 0 &&
                       ui.extra_count(0, st) == 0;
        CHECK(defaults, "lancer la prise : pattern, vélocités, pitchs, balances, décalages et roulements remis à zéro");
        CHECK(ui.quantize(0) == 25 && ui.track_width(0) == -40 && ui.track_balance(0) == 15, "la quantification, la largeur et la balance de la piste restent");
        CHECK(ui.can_undo_take() && ui.dirty(), "annulable, loop modifiée");

        // Annuler la prise rend tout.
        sink.sent.clear();
        ui.handle(InputEvent::menu());
        ui.handle(InputEvent::ok());
        bool restored = ui.pattern_bits(0) == 0x00C9 && ui.step_velocity(0, 3) == 60 && ui.step_pitch(0, 3) == 5 && ui.step_pan(0, 7) == -33 &&
                        ui.step_offset(0, 7) == -20 && ui.extra_count(0, 7) == 1 && ui.extra_hit(0, 7, 0).pan == 18;
        CHECK(restored, "Annuler la prise : tout le pattern d'avant est rendu");
        CHECK(ui.quantize(0) == 25 && ui.track_width(0) == -40, "…et les réglages de la piste n'ont pas bougé");
    }

    // ----- Annuler la prise -----
    {
        Fixture f;
        // Un pas avec roulement avant la prise, pour vérifier la restauration complète.
        f.feed({InputEvent::back()});  // vue Pattern
        f.feed({InputEvent::back()});
        f.feed({InputEvent::ok(), InputEvent::black(9)});
        f.sink.sent.clear();

        f.feed({InputEvent::play()});
        f.phase(2, -1, 0);
        f.events.queue.push_back(event(2, 7, Hit{-20, 90, 3}, {Hit{30, 60, -2}}));
        f.events.queue.push_back(event(2, 4, Hit{9, 80, 0}));
        f.tick();
        CHECK(f.ui.pattern_bits(0) == 0x0090 && f.ui.step_offset(0, 4) == 9, "la prise a modifié le modèle");

        f.sink.sent.clear();
        f.feed({InputEvent::menu()});
        Canvas c, ref;
        f.ui.render(c);
        ref.rect(0, 8, 128, 8, true);
        ref.text(0, 8, " Annuler la prise", true);
        ref.text(0, 16, " Annuler");
        CHECK(same_rows(c, ref, 8, 24), "menu du clavier : deux lignes");
        f.feed({InputEvent::ok()});
        CHECK(f.sink.count(Command::StopRecord) == 1, "annuler la prise arrête d'abord la prise en cours");
        CHECK(f.ui.view() == View::Keyboard && f.ui.pattern_bits(0) == 0x0011 && f.ui.step_offset(0, 4) == 0 && f.ui.step_offset(0, 7) == 0 &&
                  f.ui.extra_count(0, 7) == 0 && f.ui.step_velocity(0, 0) == 100 && f.ui.step_velocity(0, 7) == 127,
              "modèle remis dans l'état d'avant la prise");
        CHECK(f.sink.count(Command::ClearExtraHits) == 16 && f.sink.count(Command::SetStepVelocity) == 16 && f.sink.count(Command::SetStepPitch) == 16 &&
                  f.sink.count(Command::SetStepOffset) == 16 && f.sink.count(Command::SetStepPan) == 16 && f.sink.count(Command::SetTrackPattern) == 1 && f.sink.count(Command::SetTrackQuantize) == 1 &&
                  f.sink.count(Command::SetExtraHit) == 0,
              "le moteur reçoit tout l'état de la copie");
        bool all_track2 = true;
        for (const Command& k : f.sink.sent) all_track2 = all_track2 && (k.type == Command::StopRecord || k.track == 2);
        CHECK(all_track2, "toutes les commandes visent la piste moteur du clavier");
        Command pat;
        for (const Command& k : f.sink.sent)
            if (k.type == Command::SetTrackPattern) pat = k;
        CHECK(pat.length == 0x0011, "pattern d'origine");
        bool vel0 = false;
        for (const Command& k : f.sink.sent) vel0 = vel0 || (k.type == Command::SetStepVelocity && k.step == 0 && k.value == 100.0f);
        CHECK(vel0, "vélocité d'origine du pas 0");
        f.phase(0);
    }
    {
        // Une copie avec des notes supplémentaires : elles sont renvoyées au moteur, après les effacements.
        Fixture f;
        f.feed({InputEvent::back(), InputEvent::back()});
        f.ui.add_track("b.wav", 3, 0x0001);
        f.feed({InputEvent::wheel(1), InputEvent::ok(), InputEvent::black(9)});
        CHECK(f.ui.keyboard_track() == 1, "deuxième piste au clavier");
        f.feed({InputEvent::play()});
        f.phase(2, -1, 0);
        f.events.queue.push_back(event(3, 2, Hit{4, 70, 0}, {Hit{-3, 60, 1}, Hit{8, 55, 2}, Hit{9, 54, 3}}));
        f.tick();
        f.feed({InputEvent::play()});  // fin de prise
        f.phase(0);
        f.feed({InputEvent::play()});  // nouvelle prise : copie avec les 4 notes
        f.phase(2, -1, 0);
        f.events.queue.push_back(event(3, 2, Hit{0, 127, 0}));
        f.tick();
        CHECK(f.ui.extra_count(1, 2) == 0, "la nouvelle prise a écrasé le pas");
        f.sink.sent.clear();
        f.feed({InputEvent::menu(), InputEvent::ok()});
        int first_extra = -1, last_clear = -1;
        for (std::size_t i = 0; i < f.sink.sent.size(); ++i) {
            if (f.sink.sent[i].type == Command::SetExtraHit && first_extra < 0) first_extra = static_cast<int>(i);
            if (f.sink.sent[i].type == Command::ClearExtraHits) last_clear = static_cast<int>(i);
        }
        CHECK(f.sink.count(Command::SetExtraHit) == 3 && first_extra > last_clear && f.ui.extra_count(1, 2) == 3 && f.ui.extra_hit(1, 2, 2).velocity == 54,
              "notes supplémentaires renvoyées après les effacements");
        Command e2;
        for (const Command& k : f.sink.sent)
            if (k.type == Command::SetExtraHit && k.slot == 2) e2 = k;
        CHECK(e2.track == 3 && e2.step == 2 && e2.offset == 8 && e2.value == 55.0f && e2.pitch == 2, "contenu d'une note supplémentaire");
    }
    {
        // Envoi par paquets quand la file est petite ; les entrées sont ignorées pendant ce temps.
        Fixture f;
        f.feed({InputEvent::play()});
        f.phase(0);
        f.sink.sent.clear();
        f.sink.budget = 10;
        f.feed({InputEvent::menu(), InputEvent::ok()});
        CHECK(f.sink.sent.size() == 10, "premier paquet");
        f.feed({InputEvent::black(3), InputEvent::back()});
        CHECK(f.sink.count(Command::Note) == 0 && f.ui.view() == View::Keyboard, "entrées ignorées pendant la remise en état");
        for (int i = 0; i < 20 && f.sink.sent.size() < 82; ++i) {
            f.sink.budget += 10;
            f.tick();
        }
        CHECK(f.sink.sent.size() == 82, "toutes les commandes finissent par partir, une fois chacune");
        f.feed({InputEvent::black(3)});
        CHECK(f.sink.count(Command::Note) == 1, "les entrées reviennent");
    }
    {
        // Sans copie : l'entrée du menu ne fait rien. Annuler : rien non plus.
        Fixture f;
        CHECK(!f.ui.can_undo_take(), "pas de copie avant la première prise");
        f.feed({InputEvent::menu(), InputEvent::ok()});
        CHECK(f.sink.sent.empty() && f.ui.view() == View::Keyboard && !f.ui.dirty(), "annuler la prise sans prise : rien");
        f.feed({InputEvent::play()});
        f.phase(0);
        f.sink.sent.clear();
        f.feed({InputEvent::menu(), InputEvent::wheel(5)});
        CHECK(f.ui.menu_cursor() == 1, "le menu du clavier n'a que deux lignes");
        f.feed({InputEvent::ok()});
        CHECK(f.sink.sent.empty() && f.ui.view() == View::Keyboard, "Annuler : referme le menu, rien d'envoyé");
        f.feed({InputEvent::menu(), InputEvent::menu()});
        CHECK(f.ui.view() == View::Keyboard, "Menu rappuyé : referme");
    }

    // ----- Avec le vrai moteur : une prise jouée au clavier met le modèle à jour -----
    {
        static const int16_t impulse[] = {16384, 0};
        Engine               e;
        UiController         ui(e);
        ui.set_event_source(&e);
        Command c;
        c.type   = Command::SetTrackSample;
        c.track  = 2;
        c.data   = impulse;
        c.length = 2;
        e.push(c);
        ui.add_track("a.wav", 2, 0x0000);

        std::vector<float> l(512), r(512);
        uint32_t           t    = 0;
        int                done = 0;
        auto advance = [&](int frames) {
            for (int n = 0; n < frames; n += 512) {
                e.render(l.data(), r.data(), 512);
                done += 512;
                t += 12;
                ui.update(e.state(), t);
            }
        };
        ui.update(e.state(), t);
        ui.handle(InputEvent::ok());
        ui.handle(InputEvent::black(9));
        ui.handle(InputEvent::play());
        advance(1024);
        CHECK(ui.record_phase() == 1, "moteur : pré-compte");
        advance(88200);
        CHECK(ui.record_phase() == 2, "moteur : prise");
        // Une frappe sur les blanches (pitch -12) dans le pas 2, vers la fin de ce pas.
        advance(2 * 5512);
        ui.handle(InputEvent::step(0));
        advance(1024);
        CHECK(ui.pattern_bits(0) != 0 && ui.dirty(), "moteur : le modèle a reçu la frappe");
        int s = -1;
        for (int i = 0; i < 16; ++i)
            if ((ui.pattern_bits(0) >> i) & 1) s = i;
        CHECK(s >= 2 && s <= 3 && ui.step_pitch(0, s) == -12 && ui.step_velocity(0, s) == 127, "moteur : pas, pitch et vélocité de la frappe");
        ui.handle(InputEvent::play());
        advance(1024);
        CHECK(ui.record_phase() == 0 && e.state().playing.load(), "moteur : fin de la prise, la lecture continue");
        ui.handle(InputEvent::menu());
        ui.handle(InputEvent::ok());
        advance(1024);
        CHECK(ui.pattern_bits(0) == 0, "moteur : annuler la prise remet le pattern d'origine");
    }

    if (failures == 0) std::printf("OK : test_record_ui\n");
    return failures == 0 ? 0 : 1;
}
