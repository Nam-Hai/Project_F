// Clavier de piano : disposition, entrée par la première touche noire, jeu dans la vue, dessin.
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
    // Entre dans la vue Clavier sur la piste « a » depuis la vue Pattern, puis laisse passer la lueur de l'entrée.
    void enter_from_pattern() {
        feed({InputEvent::ok(), InputEvent::black(0)});
        sink.sent.clear();
        tick(UiController::LIT_MS + 50);
    }
};

static std::unique_ptr<Fixture> make() { return std::unique_ptr<Fixture>(new Fixture); }

using View = UiController::View;

static bool lit(const Canvas& c, int x, int y) { return c.get_pixel(x, y); }

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

// Dessin : haut du clavier, hauteur des blanches et des noires (voir render_keyboard).
constexpr int TOP = 16;

static int black_x(int j) { return keyboard::BLACK_LEFT_WHITE[j] * 8 + 5; }  // bord gauche de la noire j

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

    // ----- Entrée : la première touche noire, depuis la vue Pattern -----
    {
        auto f = make();
        f->feed({InputEvent::ok(), InputEvent::black(0)});  // piste « a » ouverte, puis la première noire
        CHECK(f->ui.view() == View::Keyboard && f->ui.keyboard_track() == 0, "la première noire ouvre la vue Clavier sur la piste ouverte");
        CHECK(f->sink.sent.size() == 1 && f->sink.at(0).type == Command::Note && f->sink.at(0).track == 2 &&
                  f->sink.at(0).value == 127.0f && f->sink.at(0).pitch == -11,
              "et joue sa note (Do dièse grave, -11) sur la piste moteur 2, vélocité 127");
        f->feed({InputEvent::back()});
        CHECK(f->ui.view() == View::Pattern && f->ui.selected_track() == 0, "Back : retour à la vue Pattern");

        // Le mode d'édition survit à l'aller-retour.
        f->feed({InputEvent::mode(), InputEvent::black(0), InputEvent::back()});
        CHECK(f->ui.view() == View::Pattern && f->ui.edit_mode() == UiController::EditMode::Velocity, "le mode Velocity est conservé");
        f->feed({InputEvent::mode(), InputEvent::mode(), InputEvent::black(0)});
        CHECK(f->ui.view() == View::Keyboard, "l'entrée marche dans n'importe quel mode");
    }

    // ----- Entrée depuis la vue Pistes : piste sous le curseur seulement -----
    {
        auto f = make();
        f->feed({InputEvent::wheel(1), InputEvent::black(0)});  // curseur sur « b » (piste moteur 5)
        CHECK(f->ui.view() == View::Keyboard && f->ui.keyboard_track() == 1, "depuis la liste : le clavier s'ouvre sur la piste sous le curseur");
        CHECK(f->sink.at(0).type == Command::Note && f->sink.at(0).track == 5 && f->sink.at(0).pitch == -11, "la note part sur la piste moteur de cette piste");
        f->feed({InputEvent::back()});
        CHECK(f->ui.view() == View::Tracks && f->ui.cursor() == 1, "Back : retour à la liste, curseur inchangé");

        auto g = make();
        g->feed({InputEvent::wheel(-5), InputEvent::black(0)});  // ligne de titre
        CHECK(g->ui.view() == View::Tracks && g->sink.sent.empty(), "ligne de titre : rien");
        g->feed({InputEvent::wheel(9), InputEvent::black(0)});  // « + Ajouter piste »
        CHECK(g->ui.view() == View::Tracks && g->sink.sent.empty(), "« Ajouter piste » : rien");
    }

    // ----- Les autres touches noires, et les autres vues, n'ont aucun effet -----
    {
        auto f = make();
        for (int j = 1; j < 10; ++j) f->feed({InputEvent::black(j)});
        f->feed({InputEvent::black(10), InputEvent::black(200)});
        CHECK(f->ui.view() == View::Tracks && f->sink.sent.empty(), "vue Pistes : les noires 2 à 10 ne font rien");
        f->feed({InputEvent::ok()});
        for (int j = 1; j < 10; ++j) f->feed({InputEvent::black(j)});
        CHECK(f->ui.view() == View::Pattern && f->sink.sent.empty(), "vue Pattern : les noires 2 à 10 ne font rien");

        f->feed({InputEvent::menu(), InputEvent::black(0), InputEvent::black(4)});
        CHECK(f->ui.view() == View::Menu && f->sink.sent.empty(), "menu : la première noire est ignorée aussi");
        f->feed({InputEvent::back(), InputEvent::back(), InputEvent::wheel(-5), InputEvent::ok(), InputEvent::black(0)});
        CHECK(f->ui.view() == View::Loops && f->sink.sent.empty(), "liste des loops : ignorée");
    }

    // ----- Jeu dans la vue Clavier -----
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
        f->feed({InputEvent::pot_bpm(0.0f)});
        CHECK(f->sink.count(Command::SetBpm) == 1, "le BPM aussi");

        // Back : retour à la vue Pattern, où les boutons de pas retrouvent leur rôle.
        f->feed({InputEvent::back()});
        CHECK(f->ui.view() == View::Pattern, "Back : retour à la vue Pattern");
        CHECK(f->sink.count(Command::StopRecord) == 1, "quitter le clavier arrête la prise");
        f->sink.sent.clear();
        f->feed({InputEvent::step(3)});
        CHECK(f->sink.count(Command::SetStep) == 1 && f->sink.count(Command::Note) == 0 && f->ui.pattern_bits(0) == 0x0019,
              "de retour en vue Pattern, un bouton de pas bascule un pas");
    }

    // ----- File de commandes pleine : pas de plantage, pas de lueur -----
    {
        auto f = make();
        f->feed({InputEvent::ok()});
        f->sink.budget = 0;
        f->feed({InputEvent::black(0)});
        CHECK(f->ui.view() == View::Keyboard && f->sink.sent.empty(), "file pleine : la vue s'ouvre, aucune note n'est envoyée");
        Canvas c;
        f->ui.render(c);
        CHECK(lit(c, black_x(0) + 2, TOP + 10), "la touche n'est pas inversée : la note n'est pas partie");
        f->feed({InputEvent::step(3), InputEvent::black(4)});
        CHECK(f->sink.sent.empty(), "file pleine : aucune commande");
    }

    // ----- Dessin du clavier -----
    {
        auto f = make();
        f->enter_from_pattern();
        Canvas c;
        f->ui.render(c);

        // En-tête : nom de la piste et BPM, comme dans les autres vues, sans lettre de mode.
        Canvas ref;
        ref.text(0, 0, "a");
        const int bpm_x = Canvas::W - 6 - 4 - Canvas::text_width("120");
        ref.text(bpm_x, 0, "120");
        ref.rect(Canvas::W - 6, 0, 2, 7, true);
        ref.rect(Canvas::W - 3, 0, 2, 7, true);
        CHECK(same_rows(c, ref, 0, 8), "en-tête : nom de la piste, BPM, icône de lecture");
        CHECK(region_empty(c, 0, 8, 128, TOP), "rien entre l'en-tête et le clavier");

        // 14 blanches dessinées (contour), les deux dernières cases vides.
        bool whites = true;
        for (int i = 0; i < keyboard::WHITE_KEYS; ++i) {
            whites = whites && lit(c, i * 8, TOP + 30) && lit(c, i * 8 + 6, TOP + 30) && lit(c, i * 8 + 3, TOP + 35) && !lit(c, i * 8 + 3, TOP + 25);
        }
        CHECK(whites, "14 blanches : contour dessiné, intérieur vide");
        CHECK(region_empty(c, 14 * 8, TOP, 128, 64), "les blanches 15 et 16 ne sont pas dessinées");

        // 10 noires : pleines, entre deux blanches ; rien dans les intervalles Mi-Fa et Si-Do.
        bool blacks = true;
        for (int j = 0; j < keyboard::BLACK_KEYS; ++j) {
            const int x = black_x(j);
            blacks = blacks && lit(c, x, TOP) && lit(c, x + 4, TOP + 19) && lit(c, x + 2, TOP + 10) && !lit(c, x + 2, TOP + 20) && !lit(c, x + 2, TOP + 25);
        }
        CHECK(blacks, "10 noires : pleines, de 5 pixels de large sur 20 de haut, décalées entre deux blanches");
        for (int gap : {2, 6, 9, 13}) CHECK(!lit(c, gap * 8 + 7, TOP + 10), "pas de noire entre Mi-Fa et Si-Do");

        // Le Do central est marqué d'un point ; aucune autre blanche.
        CHECK(lit(c, 7 * 8 + 3, TOP + 31) && !lit(c, 6 * 8 + 3, TOP + 31) && !lit(c, 8 * 8 + 3, TOP + 31), "le Do central (8e blanche) est marqué d'un point");

        // La rangée du bas est libre (réservée à l'enregistrement).
        CHECK(region_empty(c, 0, TOP + 36, 128, 64), "rien sous le clavier");
    }

    // ----- Touche jouée : inversée un instant -----
    {
        auto f = make();
        f->enter_from_pattern();
        Canvas c;

        f->ui.render(c);
        CHECK(!lit(c, 3, TOP + 20), "avant : l'intérieur de la blanche 1 est vide");
        f->feed({InputEvent::step(0)});
        f->ui.render(c);
        CHECK(lit(c, 3, TOP + 20) && lit(c, 3, TOP + 34) && !lit(c, 8 + 3, TOP + 20), "blanche jouée : pleine, les autres non");
        f->tick(UiController::LIT_MS - 1);
        f->ui.render(c);
        CHECK(lit(c, 3, TOP + 20), "encore pleine juste avant la fin");
        f->tick(2);
        f->ui.render(c);
        CHECK(!lit(c, 3, TOP + 20), "redevenue vide après 120 ms");

        // Noire jouée : creuse.
        const int x = black_x(2);
        f->ui.render(c);
        CHECK(lit(c, x + 2, TOP + 10), "noire 3 pleine au repos");
        f->feed({InputEvent::black(2)});
        f->ui.render(c);
        CHECK(!lit(c, x + 2, TOP + 10) && lit(c, x, TOP + 10) && lit(c, x + 4, TOP + 10) && lit(c, x + 2, TOP), "noire jouée : creuse (contour conservé)");
        f->tick(UiController::LIT_MS + 1);
        f->ui.render(c);
        CHECK(lit(c, x + 2, TOP + 10), "noire de nouveau pleine");

        // Le Do central : le point s'inverse avec la touche.
        f->feed({InputEvent::step(7)});
        f->ui.render(c);
        CHECK(lit(c, 7 * 8 + 1, TOP + 20) && !lit(c, 7 * 8 + 3, TOP + 31), "Do central joué : touche pleine, point en négatif");

        // Une seule touche est inversée à la fois (la dernière jouée).
        f->feed({InputEvent::step(2)});
        f->ui.render(c);
        CHECK(lit(c, 2 * 8 + 3, TOP + 20) && !lit(c, 7 * 8 + 1, TOP + 20), "seule la dernière touche jouée est inversée");
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
        // Entrée : la note jouée par la première noire est celle d'une Note de pitch -11.
        {
            Engine       e;
            UiController ui(e);
            setup(e);
            ui.add_track("a.wav", 2);
            ui.update(e.state(), 0);
            ui.handle(InputEvent::ok());
            ui.handle(InputEvent::black(0));
            CHECK(render(e) == direct(-11), "la première noire sonne à -11 demi-tons, comme une Note directe");
        }
        // Do central (blanche 8, pitch 0) : vitesse normale ; blanche 14 (+11) ; noire 6 (+1).
        auto key = [&](const InputEvent& ev) {
            Engine       e;
            UiController ui(e);
            setup(e);
            ui.add_track("a.wav", 2);
            ui.update(e.state(), 0);
            ui.handle(InputEvent::ok());
            ui.handle(InputEvent::black(0));
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
