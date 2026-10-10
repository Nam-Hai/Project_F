// Mode Clavier / Rec : disposition des 24 touches, entrée par la 10e touche noire, jeu, écran (timeline du pattern).
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

#include "../config.h"
#include "../core/dsp_utils.h"
#include "../core/engine.h"
#include "../core/engine_state.h"
#include "../core/ui/canvas.h"
#include "../core/ui/keyboard_layout.h"
#include "../core/ui/ui_controller.h"

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
    Command at(std::size_t i) const { return i < sent.size() ? sent[i] : Command{}; }
};

// Deux pistes : « a » sur la piste moteur 2, « b » sur la piste moteur 5.
struct Fixture {
    RecordingSink sink;
    EngineState   state;
    UiController  ui{sink};
    uint32_t      now = 1000;

    Fixture() {
        for (auto& n : state.triggers) n.store(0);
        ui.add_track("a.wav", 2, 0x0011);
        ui.add_track("b.wav", 5, 0x0001);
        ui.update(state, now);
    }
    void tick(uint32_t ms) {
        now += ms;
        ui.update(state, now);
    }
    void feed(std::initializer_list<InputEvent> events) {
        for (const InputEvent& e : events) ui.handle(e);
    }
    // Entre en mode Clavier / Rec sur la piste « a » depuis la vue Pattern.
    void enter_from_pattern() {
        feed({InputEvent::ok(), InputEvent::black(UiController::REC_BLACK_KEY)});
        sink.sent.clear();
    }
};

static std::unique_ptr<Fixture> make() { return std::unique_ptr<Fixture>(new Fixture); }

using View = UiController::View;

static bool region_empty(const Canvas& c, int x0, int y0, int x1, int y1) {
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (c.get_pixel(x, y)) return false;
    return true;
}

static bool same_rows(const Canvas& a, const Canvas& b, int y0, int y1) {
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < Canvas::W; ++x)
            if (a.get_pixel(x, y) != b.get_pixel(x, y)) return false;
    return true;
}

int main() {
    // ----- Disposition : la table des 24 touches -----
    {
        bool whites_ok = true, blacks_ok = true;
        for (int i = 0; i < keyboard::WHITE_KEYS; ++i) {
            whites_ok = whites_ok && keyboard::WHITE_PITCH[i] >= cfg::STEP_PITCH_MIN && keyboard::WHITE_PITCH[i] <= cfg::STEP_PITCH_MAX;
            if (i > 0) whites_ok = whites_ok && keyboard::WHITE_PITCH[i] > keyboard::WHITE_PITCH[i - 1];
        }
        for (int j = 0; j < keyboard::BLACK_KEYS; ++j) {
            blacks_ok = blacks_ok && keyboard::BLACK_PITCH[j] >= cfg::STEP_PITCH_MIN && keyboard::BLACK_PITCH[j] <= cfg::STEP_PITCH_MAX;
            // Une noire est un demi-ton au-dessus de la blanche à sa gauche.
            blacks_ok = blacks_ok && keyboard::BLACK_PITCH[j] == keyboard::WHITE_PITCH[keyboard::BLACK_LEFT_WHITE[j]] + 1;
        }
        CHECK(whites_ok, "blanches : dans la plage ±12, strictement croissantes");
        CHECK(blacks_ok, "noires : dans la plage ±12, un demi-ton au-dessus de la blanche de gauche");
        CHECK(keyboard::WHITE_PITCH[keyboard::ROOT_WHITE] == 0 && keyboard::ROOT_WHITE == 7, "le Do de la 8e blanche est la note de base (pitch 0)");
        CHECK(keyboard::WHITE_PITCH[0] == -12 && keyboard::WHITE_PITCH[13] == 11, "les blanches vont de -12 à +11");

        std::vector<int> all(keyboard::WHITE_PITCH, keyboard::WHITE_PITCH + keyboard::WHITE_KEYS);
        all.insert(all.end(), keyboard::BLACK_PITCH, keyboard::BLACK_PITCH + keyboard::BLACK_KEYS);
        std::sort(all.begin(), all.end());
        bool complete = all.size() == 24;
        for (int i = 0; i < 24 && complete; ++i) complete = all[static_cast<std::size_t>(i)] == -12 + i;
        CHECK(complete, "les 24 touches couvrent exactement les 24 demi-tons de -12 à +11, sans doublon");

        // Pas de noire entre Mi et Fa (blanches 2-3 et 9-10) ni entre Si et Do (6-7 et 13-14), une noire dans chaque autre intervalle.
        bool gaps_ok = true;
        for (int gap : {2, 6, 9, 13})
            for (int j = 0; j < keyboard::BLACK_KEYS; ++j) gaps_ok = gaps_ok && keyboard::BLACK_LEFT_WHITE[j] != gap;
        for (int gap : {0, 1, 3, 4, 5, 7, 8, 10, 11, 12}) {
            int n = 0;
            for (int j = 0; j < keyboard::BLACK_KEYS; ++j) n += keyboard::BLACK_LEFT_WHITE[j] == gap;
            gaps_ok = gaps_ok && n == 1;
        }
        CHECK(gaps_ok, "noires placées comme sur un piano (pas entre Mi-Fa ni Si-Do)");
    }

    // ----- Entrée : la 10e touche noire, depuis la vue Pattern -----
    {
        auto f = make();
        f->feed({InputEvent::ok(), InputEvent::black(9)});  // piste « a » ouverte, puis la 10e noire
        CHECK(f->ui.view() == View::Keyboard && f->ui.keyboard_track() == 0, "la 10e noire entre en mode Clavier / Rec sur la piste ouverte");
        CHECK(f->sink.sent.size() == 1 && f->sink.at(0).type == Command::Note && f->sink.at(0).track == 2 &&
                  f->sink.at(0).value == 127.0f && f->sink.at(0).pitch == 10,
              "et joue sa note (+10) sur la piste moteur 2, vélocité 127");
        f->feed({InputEvent::back()});
        CHECK(f->ui.view() == View::Pattern && f->ui.selected_track() == 0, "Back : retour à la vue Pattern");

        // Le mode d'édition survit à l'aller-retour.
        f->feed({InputEvent::mode(), InputEvent::black(9), InputEvent::back()});
        CHECK(f->ui.view() == View::Pattern && f->ui.edit_mode() == UiController::EditMode::Velocity, "le mode Velocity est conservé");
        f->feed({InputEvent::mode(), InputEvent::mode(), InputEvent::black(9)});
        CHECK(f->ui.view() == View::Keyboard, "l'entrée marche dans n'importe quel mode");
        // La première touche noire règle le gain de la piste (touche tenue) : elle n'entre pas en mode Clavier / Rec.
        auto g = make();
        g->feed({InputEvent::ok(), InputEvent::black(0), InputEvent::black_up(0)});
        CHECK(g->ui.view() == View::Pattern && g->sink.sent.empty(), "la 1re touche noire n'entre plus en mode Clavier / Rec");
    }

    // ----- Entrée depuis la vue Pistes : piste sous le curseur seulement -----
    {
        auto f = make();
        f->feed({InputEvent::wheel(1), InputEvent::black(9)});  // curseur sur « b » (piste moteur 5)
        CHECK(f->ui.view() == View::Keyboard && f->ui.keyboard_track() == 1, "depuis la liste : le mode s'ouvre sur la piste sous le curseur");
        CHECK(f->sink.at(0).type == Command::Note && f->sink.at(0).track == 5 && f->sink.at(0).pitch == 10, "la note part sur la piste moteur de cette piste");
        f->feed({InputEvent::back()});
        CHECK(f->ui.view() == View::Tracks && f->ui.cursor() == 1, "Back : retour à la liste, curseur inchangé");

        auto g = make();
        g->feed({InputEvent::wheel(-5), InputEvent::black(9)});  // ligne de titre
        CHECK(g->ui.view() == View::Tracks && g->sink.sent.empty(), "ligne de titre : rien");
        g->feed({InputEvent::wheel(9), InputEvent::black(9)});  // « + Ajouter piste »
        CHECK(g->ui.view() == View::Tracks && g->sink.sent.empty(), "« Ajouter piste » : rien");
    }

    // ----- Les autres touches noires, et les autres vues, n'ont aucun effet -----
    {
        auto f = make();
        for (int j = 0; j < 9; ++j) f->feed({InputEvent::black(j), InputEvent::black_up(j)});
        f->feed({InputEvent::black(10), InputEvent::black(200)});
        CHECK(f->ui.view() == View::Tracks && f->sink.sent.empty(), "vue Pistes : les noires 1 à 9 ne font rien");
        f->feed({InputEvent::ok()});
        for (int j : {8}) f->feed({InputEvent::black(j), InputEvent::black_up(j)});  // 1 à 8 : réglages de la piste
        CHECK(f->ui.view() == View::Pattern && f->sink.sent.empty(), "vue Pattern : les noires sans rôle ne font rien");

        f->feed({InputEvent::menu(), InputEvent::black(9), InputEvent::black(4)});
        CHECK(f->ui.view() == View::Menu && f->sink.sent.empty(), "menu : la 10e noire est ignorée aussi");
        f->feed({InputEvent::back(), InputEvent::back(), InputEvent::wheel(-5), InputEvent::ok(), InputEvent::black(9)});
        CHECK(f->ui.view() == View::Loops && f->sink.sent.empty(), "liste des loops : ignorée");
    }

    // ----- Jeu en mode Clavier / Rec -----
    {
        auto f = make();
        f->enter_from_pattern();

        // 14 blanches : le pitch de la table, vélocité 127, piste moteur 2.
        for (int i = 0; i < keyboard::WHITE_KEYS; ++i) {
            f->sink.sent.clear();
            f->feed({InputEvent::step(i)});
            CHECK(f->sink.sent.size() == 1 && f->sink.at(0).type == Command::Note && f->sink.at(0).track == 2 &&
                      f->sink.at(0).value == 127.0f && f->sink.at(0).pitch == keyboard::WHITE_PITCH[i],
                  "touche blanche : Note au pitch de la table");
        }
        // Les deux dernières blanches sont inactives.
        f->sink.sent.clear();
        f->feed({InputEvent::step(14), InputEvent::step(15), InputEvent::step(16), InputEvent::step(200)});
        CHECK(f->sink.sent.empty(), "blanches 15 et 16 : inactives");

        // 10 noires.
        for (int j = 0; j < keyboard::BLACK_KEYS; ++j) {
            f->sink.sent.clear();
            f->feed({InputEvent::black(j)});
            CHECK(f->sink.sent.size() == 1 && f->sink.at(0).type == Command::Note && f->sink.at(0).track == 2 &&
                      f->sink.at(0).pitch == keyboard::BLACK_PITCH[j],
                  "touche noire : Note au pitch de la table");
        }
        CHECK(f->ui.view() == View::Keyboard, "on reste dans la vue Clavier");

        // Le pattern et la loop ne sont pas touchés.
        CHECK(f->ui.pattern_bits(0) == 0x0011 && !f->ui.dirty(), "jouer ne modifie ni le pattern ni la loop");

        // Les autres contrôles : la roue, Valider et Mode sont ignorés ; Menu ouvre le menu de la vue ; Play lance la prise.
        f->sink.sent.clear();
        f->feed({InputEvent::wheel(3), InputEvent::ok(), InputEvent::mode()});
        CHECK(f->sink.sent.empty() && f->ui.view() == View::Keyboard, "roue, Valider et Mode sont ignorés");
        f->feed({InputEvent::menu()});
        CHECK(f->ui.view() == View::Menu && f->ui.menu_kind() == UiController::MenuKind::Keyboard, "Menu : menu de la vue Clavier");
        f->feed({InputEvent::back()});
        CHECK(f->ui.view() == View::Keyboard, "Back : retour au clavier");
        f->feed({InputEvent::play()});
        CHECK(f->sink.count(Command::StartRecord) == 1 && f->sink.count(Command::Play) == 0 && f->sink.at(0).track == 2 &&
                  f->ui.view() == View::Keyboard,
              "Play lance la prise sur la piste du clavier");
        CHECK(f->ui.pattern_bits(0) == 0 && f->ui.dirty(), "lancer la prise efface le pattern de la piste (modèle) : la loop est modifiée");
        f->feed({InputEvent::pot_bpm(0.0f)});
        CHECK(f->sink.count(Command::SetBpm) == 1, "le BPM aussi");

        // Back : retour à la vue Pattern, où les boutons de pas retrouvent leur rôle.
        f->feed({InputEvent::back()});
        CHECK(f->ui.view() == View::Pattern, "Back : retour à la vue Pattern");
        CHECK(f->sink.count(Command::StopRecord) == 1, "quitter le clavier arrête la prise");
        f->sink.sent.clear();
        f->feed({InputEvent::step(3)});
        CHECK(f->sink.count(Command::SetStep) == 1 && f->sink.count(Command::Note) == 0 && f->ui.pattern_bits(0) == 0x0008,
              "de retour en vue Pattern, un bouton de pas bascule un pas (le pattern avait été effacé par la prise)");
    }

    // ----- File de commandes pleine : pas de plantage -----
    {
        auto f = make();
        f->feed({InputEvent::ok()});
        f->sink.budget = 0;
        f->feed({InputEvent::black(9)});
        CHECK(f->ui.view() == View::Keyboard && f->sink.sent.empty(), "file pleine : le mode s'ouvre, aucune note n'est envoyée");
        f->feed({InputEvent::step(3), InputEvent::black(4)});
        CHECK(f->sink.sent.empty(), "file pleine : aucune commande");
    }

    // ----- Écran : la timeline du pattern, pas de piano dessiné -----
    {
        auto f = make();
        f->enter_from_pattern();
        Canvas c;
        f->ui.render(c);

        // En-tête : nom de la piste, « KEY » à gauche du BPM, BPM, icône de lecture.
        Canvas ref;
        ref.text(0, 0, "a");
        const int bpm_x = Canvas::W - 6 - 4 - Canvas::text_width("120");
        ref.text(bpm_x - 6 - Canvas::CHAR_W * 3, 0, "KEY");
        ref.text(bpm_x, 0, "120");
        ref.rect(Canvas::W - 6, 0, 2, 7, true);
        ref.rect(Canvas::W - 3, 0, 2, 7, true);
        CHECK(same_rows(c, ref, 0, 8), "en-tête : nom de la piste, KEY, BPM, icône de lecture");

        // Timeline : les boîtes du pattern comme en mode Edit (0x0011 : pas 0 et 4 pleins), repères de temps.
        auto box = [&](int step) {
            int n = 0;
            for (int y = 24; y < 31; ++y)
                for (int x = step * 8; x < step * 8 + 7; ++x) n += c.get_pixel(x, y);
            return n;
        };
        CHECK(box(0) == 49 && box(4) == 49 && box(1) == 24 && box(15) == 24, "boîtes pleines pour les pas joués, contour pour les autres");
        CHECK(c.get_pixel(3, 18) && c.get_pixel(4 * 8 + 3, 18) && !c.get_pixel(8 + 3, 18), "repères de temps tous les 4 pas");
        // Pas de piano : rien entre l'en-tête et les repères, rien sous la timeline, pas de pas sélectionné ni de jauge.
        CHECK(region_empty(c, 0, 8, 128, 18), "pas de touches dessinées");
        CHECK(region_empty(c, 0, 36, 128, 64), "rien sous la timeline : ni jauge ni texte");
        CHECK(!c.get_pixel(3, 22), "pas de repère de sélection");

        // Jouer ne change pas l'écran (pas de touche inversée).
        Canvas before = c;
        f->feed({InputEvent::step(3), InputEvent::black(2), InputEvent::step(7)});
        f->ui.render(c);
        bool same = true;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < Canvas::W; ++x) same = same && c.get_pixel(x, y) == before.get_pixel(x, y);
        CHECK(same, "jouer des notes ne change pas l'écran");

        // Le pas en cours de lecture est marqué sous sa boîte, comme dans la vue Pattern.
        f->state.playing.store(true);
        f->state.step.store(5);
        f->tick(10);
        f->ui.render(c);
        CHECK(c.get_pixel(5 * 8 + 3, 24 + 10) && !c.get_pixel(4 * 8 + 3, 24 + 10), "pas en cours de lecture : repère sous la boîte");

        // Les réglages de piste et les modes d'édition ne s'affichent pas ici, même après un passage en Velocity.
        auto g = make();
        g->feed({InputEvent::ok(), InputEvent::mode(), InputEvent::step(0), InputEvent::wheel(-10), InputEvent::black(9)});
        g->ui.render(c);
        int box0 = 0;
        for (int y = 24; y < 31; ++y)
            for (int x = 0; x < 7; ++x) box0 += c.get_pixel(x, y);
        CHECK(g->ui.step_velocity(0, 0) < 127 && box0 == 49 && region_empty(c, 0, 36, 128, 64) && !c.get_pixel(3, 22),
              "mode Velocity et pas sélectionné d'avant : la timeline reste celle du mode Edit, sans sélection ni jauge");
    }

    // ----- Avec le vrai moteur : les touches font sonner la piste au bon pitch -----
    {
        static const int16_t ramp[] = {0, 8000, 16000, 24000, 32000, 0};

        auto render = [](Engine& e) {
            std::vector<float> l(64), r(64);
            e.render(l.data(), r.data(), 64);
            return l;
        };
        auto setup = [](Engine& e) {
            Command c;
            c.type   = Command::SetTrackSample;
            c.track  = 2;
            c.data   = ramp;
            c.length = 6;
            e.push(c);
        };
        auto direct = [&](int pitch) {
            Engine e;
            setup(e);
            Command n;
            n.type  = Command::Note;
            n.track = 2;
            n.value = 127.0f;
            n.pitch = static_cast<int8_t>(pitch);
            e.push(n);
            return render(e);
        };
        // Entrée : la note jouée par la 10e noire est celle d'une Note de pitch +10.
        {
            Engine       e;
            UiController ui(e);
            setup(e);
            ui.add_track("a.wav", 2);
            ui.update(e.state(), 0);
            ui.handle(InputEvent::ok());
            ui.handle(InputEvent::black(9));
            CHECK(render(e) == direct(10), "la 10e noire sonne à +10 demi-tons, comme une Note directe");
        }
        // Do central (blanche 8, pitch 0) : vitesse normale ; blanche 14 (+11) ; noire 6 (+1).
        auto key = [&](const InputEvent& ev) {
            Engine       e;
            UiController ui(e);
            setup(e);
            ui.add_track("a.wav", 2);
            ui.update(e.state(), 0);
            ui.handle(InputEvent::ok());
            ui.handle(InputEvent::black(9));
            std::vector<float> sink(64), unused(64);
            e.render(sink.data(), unused.data(), 64);  // la note d'entrée se termine
            ui.handle(ev);
            return render(e);
        };
        CHECK(key(InputEvent::step(7)) == direct(0), "Do central : note de base, vitesse normale");
        CHECK(key(InputEvent::step(13)) == direct(11), "14e blanche : +11 demi-tons");
        CHECK(key(InputEvent::step(0)) == direct(-12), "1re blanche : -12 demi-tons");
        CHECK(key(InputEvent::black(5)) == direct(1), "6e noire : +1 demi-ton");
        CHECK(key(InputEvent::black(9)) == direct(10), "10e noire : +10 demi-tons");
        CHECK(key(InputEvent::step(14)) != direct(0) && key(InputEvent::step(15)) != direct(0), "(blanches inactives : aucune nouvelle note)");
        CHECK(direct(0) != direct(12) && direct(0) != direct(-12), "(les pitchs changent bien le son)");
    }

    if (failures == 0) std::printf("OK\n");
    return failures ? 1 : 0;
}
