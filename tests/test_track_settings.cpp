// Réglages de la piste sur les touches noires 1 à 3 (Quantifier, Wide, Balance), tenues dans la vue Pattern :
// la roue règle la valeur, la jauge l'affiche, le relâchement rend la main à l'état d'avant.
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../config.h"
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

using Mode = UiController::EditMode;
using View = UiController::View;
using Set  = UiController::TrackSetting;

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

// Une piste « Snare_long_name.wav » sur la piste moteur 2, pas 0, 4, 8, 12 ; quantification 40, largeur -40 (40M), balance +30
// (réglés par les touches mêmes, pour partir d'un état connu) ; vue Pattern ouverte.
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
        set(1, 8);    // 0 -> 40
        set(2, -8);   // 0 -> -40
        set(3, 6);    // 0 -> +30
        sink.sent.clear();
    }
    void feed(std::initializer_list<InputEvent> e) {
        for (const InputEvent& x : e) ui.handle(x);
    }
    void set(int key, int notches) { feed({InputEvent::black(key), InputEvent::wheel(notches), InputEvent::black_up(key)}); }
    void tick(uint32_t ms) {
        now += ms;
        ui.update(state, now);
    }
};

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

// Le texte du mode ou du réglage, à gauche du BPM, aligné à droite sur la même colonne que la lettre d'un mode.
static bool header_text_is(const Canvas& got, const char* text) {
    Canvas      ref;
    const int   bpm_x = Canvas::W - 6 - 4 - Canvas::text_width("120");
    ref.text(bpm_x - 6 - Canvas::CHAR_W * static_cast<int>(std::strlen(text)), 0, text);
    for (int y = 0; y < 8; ++y)
        for (int x = 50; x < bpm_x - 2; ++x)
            if (got.get_pixel(x, y) != ref.get_pixel(x, y)) return false;
    return true;
}

int main() {
    // ----- État de départ -----
    {
        Fixture f;
        CHECK(f.ui.view() == View::Pattern && f.ui.quantize(0) == 40 && f.ui.track_width(0) == -40 && f.ui.track_balance(0) == 30, "état de départ");
        CHECK(f.ui.track_setting() == Set::None, "aucun réglage tenu");
    }

    // ----- Quantifier (touche noire 2) -----
    {
        Fixture f;
        Canvas  c;
        f.feed({InputEvent::black(1)});
        CHECK(f.ui.track_setting() == Set::Quantize, "touche tenue : réglage Quantifier");
        f.ui.render(c);
        CHECK(header_text_is(c, "Quant"), "en-tête : « Quant » à la place de la lettre du mode");
        CHECK(value_is(c, "40%") && lit(c, 1, 49, 38, 5) == 38 * 5 && lit(c, 40, 49, 58, 5) == 0, "jauge : 38 pixels pour 40 %, valeur « 40% »");

        f.feed({InputEvent::wheel(3)});
        CHECK(f.ui.quantize(0) == 55 && f.sink.last(Command::SetTrackQuantize).value == 55.0f && f.ui.dirty(), "la roue règle la force, 5 % par cran, effet immédiat");
        f.ui.render(c);
        CHECK(value_is(c, "55%") && lit(c, 1, 49, 53, 5) == 53 * 5, "la jauge suit");
        f.feed({InputEvent::wheel(100)});
        CHECK(f.ui.quantize(0) == 100, "borné à 100");
        f.sink.sent.clear();
        f.feed({InputEvent::wheel(1)});
        CHECK(f.sink.sent.empty(), "à 100 : rien d'envoyé");
        f.feed({InputEvent::wheel(-100)});
        CHECK(f.ui.quantize(0) == 0, "borné à 0");
        f.ui.render(c);
        CHECK(value_is(c, "0%") && lit(c, 1, 49, 98, 5) == 0, "0 % : aucune barre");
        f.feed({InputEvent::wheel(2), InputEvent::black_up(1)});
        CHECK(f.ui.quantize(0) == 10 && f.ui.track_setting() == Set::None, "relâché : la valeur est gardée, réglage terminé");
    }

    // ----- Wide (touche noire 3) et Balance (touche noire 4) -----
    {
        Fixture f;
        Canvas  c;
        f.feed({InputEvent::black(2)});
        CHECK(f.ui.track_setting() == Set::Width, "touche 3 : Wide");
        f.ui.render(c);
        CHECK(header_text_is(c, "Wide") && value_is(c, "40M") && lit(c, 31, 49, 19, 5) == 19 * 5 && lit(c, 51, 49, 40, 5) == 0 && lit(c, 50, 47, 1, 9) == 9,
              "jauge de la largeur : 19 pixels à gauche du centre (40 x 48 / 100), « 40M »");
        f.feed({InputEvent::wheel(1)});
        CHECK(f.ui.track_width(0) == -35 && f.sink.last(Command::SetTrackWidth).value == -35.0f && f.ui.track_balance(0) == 30, "la roue règle la largeur, pas la balance");
        f.feed({InputEvent::wheel(100)});
        CHECK(f.ui.track_width(0) == 100, "borné à +100");
        f.ui.render(c);
        CHECK(value_is(c, "100W") && lit(c, 51, 49, 48, 5) == 48 * 5, "100W : 48 pixels à droite");
        f.feed({InputEvent::wheel(-100)});
        CHECK(f.ui.track_width(0) == -100, "borné à -100");
        f.ui.render(c);
        CHECK(value_is(c, "100M") && lit(c, 2, 49, 48, 5) == 48 * 5, "100M : mono complet, 48 pixels à gauche");
        f.feed({InputEvent::wheel(20), InputEvent::black_up(2)});
        CHECK(f.ui.track_width(0) == 0, "retour exactement à 0");
        f.feed({InputEvent::black(2)});
        f.ui.render(c);
        CHECK(value_is(c, "0") && lit(c, 1, 49, 49, 5) == 0 && lit(c, 51, 49, 48, 5) == 0, "à 0 : « 0 », aucune barre");
        f.feed({InputEvent::black_up(2)});

        f.feed({InputEvent::black(3)});
        CHECK(f.ui.track_setting() == Set::Balance, "touche 4 : Balance");
        f.ui.render(c);
        CHECK(header_text_is(c, "Bal") && value_is(c, "R30") && lit(c, 51, 49, 29, 5) == 29 * 5, "jauge de la balance : « R30 », 29 pixels");
        f.feed({InputEvent::wheel(-2)});
        CHECK(f.ui.track_balance(0) == 20 && f.sink.last(Command::SetTrackBalance).value == 20.0f && f.ui.track_width(0) == 0, "la roue règle la balance, pas la largeur");
        f.feed({InputEvent::black(3), InputEvent::black_up(9), InputEvent::black_up(0), InputEvent::black_up(7)});
        CHECK(f.ui.track_setting() == Set::Balance, "relâcher une touche sans rôle ne termine pas le réglage tenu");
        f.feed({InputEvent::black_up(3)});
        CHECK(f.ui.track_setting() == Set::None, "(relâchée)");
    }

    // ----- Relâcher rend la main : le mode, la sélection et la roue d'avant -----
    {
        Fixture f;
        f.feed({InputEvent::mode(), InputEvent::step(4)});  // Velocity, pas 4 sélectionné
        CHECK(f.ui.edit_mode() == Mode::Velocity && f.ui.selected_step() == 4, "mode Velocity, pas 4");
        f.tick(100);
        f.feed({InputEvent::wheel(-2)});
        CHECK(f.ui.step_velocity(0, 4) == 119, "avant : la roue règle la vélocité du pas");
        f.tick(100);
        f.sink.sent.clear();

        f.feed({InputEvent::black(2), InputEvent::wheel(1)});
        CHECK(f.ui.step_velocity(0, 4) == 119 && f.ui.track_width(0) == -35 && f.sink.count(Command::SetStepVelocity) == 0 && f.sink.count(Command::AuditionStep) == 0,
              "tenue : la roue règle la largeur, pas la vélocité, et ne joue pas de note d'essai");
        Canvas c;
        f.ui.render(c);
        CHECK(value_is(c, "35M"), "tenue : la jauge de la largeur remplace celle de la vélocité");
        f.feed({InputEvent::black_up(2)});
        CHECK(f.ui.edit_mode() == Mode::Velocity && f.ui.selected_step() == 4, "relâché : même mode, même pas");
        f.ui.render(c);
        CHECK(value_is(c, "119"), "la jauge de la vélocité est revenue");
        f.tick(100);
        f.feed({InputEvent::wheel(-1)});
        CHECK(f.ui.step_velocity(0, 4) == 115 && f.ui.track_width(0) == -35, "la roue règle de nouveau la vélocité");

        // Une note d'essai retenue par la cadence est abandonnée quand on saisit un réglage de la piste.
        f.feed({InputEvent::wheel(-1)});  // t = même instant : la note d'essai est retenue
        const int auditions = f.sink.count(Command::AuditionStep);
        f.feed({InputEvent::black(3), InputEvent::black_up(3)});
        f.tick(200);
        CHECK(f.sink.count(Command::AuditionStep) == auditions, "note d'essai retenue : abandonnée quand la touche est saisie");

        // Les boutons de pas gardent leur rôle pendant que la touche est tenue.
        f.feed({InputEvent::black(1), InputEvent::step(8)});
        CHECK(f.ui.selected_step() == 8 && f.ui.track_setting() == Set::Quantize, "un bouton de pas pendant la touche tenue : sélection du pas, réglage conservé");
        f.feed({InputEvent::black_up(1)});

        // Mode Edit : la roue ne fait rien d'habitude, mais règle le réglage tenu.
        Fixture g;
        g.sink.sent.clear();
        g.feed({InputEvent::wheel(3)});
        CHECK(g.sink.sent.empty(), "mode Edit : la roue ne fait rien sans touche tenue");
        g.feed({InputEvent::black(3), InputEvent::wheel(1)});
        CHECK(g.ui.track_balance(0) == 35, "mode Edit, touche tenue : la roue règle");
    }

    // ----- Plusieurs touches tenues -----
    {
        Fixture f;
        f.feed({InputEvent::black(1), InputEvent::black(2)});
        CHECK(f.ui.track_setting() == Set::Width, "la dernière touche enfoncée règle");
        f.feed({InputEvent::black(3)});
        CHECK(f.ui.track_setting() == Set::Balance, "…puis la suivante");
        f.feed({InputEvent::black_up(2)});
        CHECK(f.ui.track_setting() == Set::Balance, "relâcher une touche qui ne règle pas : sans effet sur le réglage actif");
        f.feed({InputEvent::black_up(3)});
        CHECK(f.ui.track_setting() == Set::Quantize, "relâcher la dernière : la touche encore tenue reprend la main");
        f.feed({InputEvent::black_up(1)});
        CHECK(f.ui.track_setting() == Set::None, "tout relâché : plus de réglage");
        // Relâchements sans appui, touches sans rôle : ignorés.
        f.feed({InputEvent::black_up(1), InputEvent::black_up(0), InputEvent::black_up(7), InputEvent::black_up(9)});
        CHECK(f.ui.track_setting() == Set::None && f.ui.view() == View::Pattern, "relâchements inutiles : ignorés");
        f.feed({InputEvent::black(4), InputEvent::black(9)});
        CHECK(f.ui.track_setting() == Set::None, "les autres touches noires ne règlent rien dans la vue Pattern");
        // Une touche tenue deux fois (rebond) : un seul relâchement suffit.
        f.feed({InputEvent::black(1), InputEvent::black(1), InputEvent::black_up(1)});
        CHECK(f.ui.track_setting() == Set::None, "appui répété : un relâchement termine");
    }

    // ----- Quitter la vue pendant que la touche est tenue -----
    {
        Fixture f;
        f.feed({InputEvent::black(2), InputEvent::back()});
        CHECK(f.ui.view() == View::Tracks && f.ui.track_setting() == Set::None, "Back : on quitte la vue, le réglage tenu est abandonné");
        f.sink.sent.clear();
        f.feed({InputEvent::wheel(1)});
        CHECK(f.sink.sent.empty() && f.ui.track_width(0) == -40, "la roue ne règle plus rien dans la liste");
        f.feed({InputEvent::black_up(2), InputEvent::wheel(-1), InputEvent::ok()});
        CHECK(f.ui.view() == View::Pattern && f.ui.track_setting() == Set::None, "réouverture du pattern : aucun réglage actif, le relâchement tardif est ignoré");
        f.feed({InputEvent::wheel(1)});
        CHECK(f.sink.sent.empty(), "la roue ne règle rien");

        // Menu pendant le maintien.
        Fixture g;
        g.feed({InputEvent::black(1), InputEvent::menu()});
        CHECK(g.ui.view() == View::Menu && g.ui.track_setting() == Set::None, "Menu : le réglage tenu est abandonné");
    }

    // ----- Hors de la vue Pattern -----
    {
        Fixture f;
        f.feed({InputEvent::back()});
        f.sink.sent.clear();
        f.feed({InputEvent::black(1), InputEvent::black_up(1)});
        CHECK(f.ui.view() == View::Tracks && f.ui.track_setting() == Set::None && f.sink.sent.empty() && f.ui.quantize(0) == 40,
              "vue Pistes : les touches de réglage ne font rien");
        // Vue Clavier : elles jouent des notes.
        f.feed({InputEvent::black(9)});
        CHECK(f.ui.view() == View::Keyboard, "vue Clavier");
        f.sink.sent.clear();
        f.feed({InputEvent::black(1), InputEvent::black_up(1)});
        CHECK(f.sink.count(Command::Note) == 1 && f.ui.track_setting() == Set::None, "vue Clavier : la touche joue une note, aucun réglage");
    }

    // ----- Titre coupé, file pleine, menu de la piste -----
    {
        Fixture f;
        Canvas  c, ref;
        f.ui.render(c);
        ref.text(0, 0, "Snare_long_n");  // 13 caractères au plus hors réglage : « Snare_long_na »
        bool same_long = true;
        {
            Canvas full;
            full.text(0, 0, "Snare_long_na");
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 13 * 6 - 1 && x < 80; ++x) same_long = same_long && c.get_pixel(x, y) == full.get_pixel(x, y);
        }
        CHECK(same_long, "hors réglage : titre de 13 caractères");
        f.feed({InputEvent::black(1)});
        f.ui.render(c);
        Canvas eight;
        eight.text(0, 0, "Snare_lo");
        bool same8 = true;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8 * 6; ++x) same8 = same8 && c.get_pixel(x, y) == eight.get_pixel(x, y);
        CHECK(same8 && lit(c, 8 * 6, 0, 16, 8) == 0, "réglage tenu : titre coupé à 8 caractères, pas de chevauchement avec « Quant »");
        f.feed({InputEvent::black_up(1)});

        f.sink.budget = 0;
        f.feed({InputEvent::black(2), InputEvent::wheel(1), InputEvent::black_up(2)});
        CHECK(f.ui.track_width(0) == -40, "file pleine : valeur inchangée");

        // Le menu de la piste n'a plus les trois réglages.
        Fixture g;
        g.feed({InputEvent::menu()});
        Canvas m, mref;
        g.ui.render(m);
        mref.rect(0, 8, 128, 8, true);
        mref.text(0, 8, " Changer le sample", true);
        mref.text(0, 16, " Supprimer la piste");
        mref.text(0, 24, " Annuler");
        bool menu_same = true;
        for (int y = 8; y < 32; ++y)
            for (int x = 0; x < Canvas::W; ++x) menu_same = menu_same && m.get_pixel(x, y) == mref.get_pixel(x, y);
        CHECK(menu_same && lit(m, 0, 32, 128, 8) == 0, "menu de la piste : trois lignes seulement");
    }

    if (failures == 0) std::printf("OK : test_track_settings\n");
    return failures == 0 ? 0 : 1;
}
