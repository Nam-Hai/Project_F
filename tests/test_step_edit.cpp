// Vue Pattern : modes Edit / Velocity / Pitch, sélection d'un pas, réglage à la roue, rendu.
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
    // Commande numéro i, ou une commande vide si elle n'existe pas : un test qui échoue ne plante pas.
    Command at(std::size_t i) const { return i < sent.size() ? sent[i] : Command{}; }
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

// Une piste « a.wav » sur la piste moteur 2, pas 0, 4, 8 et 12 joués ; l'UI est dans la vue Pattern.
struct Fixture {
    RecordingSink sink;
    FakeStore     store;
    EngineState   state;
    UiController  ui{sink, nullptr, &store};
    uint32_t      now = 1000;

    explicit Fixture(const uint8_t* velocity = nullptr, const int8_t* pitch = nullptr) {
        for (auto& n : state.triggers) n.store(0);
        ui.add_track("a.wav", 2, 0x1111, 1.0f, 1.0f, velocity, pitch);
        ui.update(state, now);
        ui.handle(InputEvent::ok());  // ouvre la vue Pattern de la piste
    }
    void tick(uint32_t ms) {
        now += ms;
        ui.update(state, now);
    }
    void feed(std::initializer_list<InputEvent> events) {
        for (const InputEvent& e : events) ui.handle(e);
    }
    // Commandes envoyées depuis `from`.
    std::vector<Command> since(std::size_t from) const { return {sink.sent.begin() + static_cast<std::ptrdiff_t>(from), sink.sent.end()}; }
};

static std::unique_ptr<Fixture> make() { return std::unique_ptr<Fixture>(new Fixture); }

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

// En-tête de la vue Pattern attendu : titre, lettre du mode, BPM et icône de lecture.
static bool header_is(const Canvas& got, const char* title, char letter, const char* bpm = "120") {
    Canvas ref;
    ref.text(0, 0, title);
    const int bpm_x = Canvas::W - 6 - 4 - Canvas::text_width(bpm);
    ref.text(bpm_x, 0, bpm);
    const char l[2] = {letter, '\0'};
    ref.text(bpm_x - 12, 0, l);
    ref.rect(Canvas::W - 6, 0, 2, 7, true);
    ref.rect(Canvas::W - 3, 0, 2, 7, true);
    return same_rows(got, ref, 0, 8);
}

// Jauge du pas sélectionné : contour de 100 x 7 pixels à partir de la rangée 48 (le trait central du
// pitch dépasse d'un pixel en haut et en bas) ; la valeur est écrite à sa droite, à partir de x = 104.
static int gauge(const Canvas& c) { return lit(c, 0, 47, 100, 9); }

static bool value_is(const Canvas& c, const char* text) {
    Canvas ref;
    ref.text(104, 48, text);
    for (int y = 40; y < 64; ++y)
        for (int x = 100; x < Canvas::W; ++x)
            if (c.get_pixel(x, y) != ref.get_pixel(x, y)) return false;
    return true;
}

static int box(const Canvas& c, int step) { return lit(c, step * 8, 24, 7, 7); }

using Mode = UiController::EditMode;
using View = UiController::View;

int main() {
    // ----- Le bouton Mode -----
    {
        auto f = make();
        CHECK(f->ui.view() == View::Pattern && f->ui.edit_mode() == Mode::Edit, "on arrive en mode Edit");
        f->feed({InputEvent::mode()});
        CHECK(f->ui.edit_mode() == Mode::Velocity, "Edit -> Velocity");
        f->feed({InputEvent::mode()});
        CHECK(f->ui.edit_mode() == Mode::Pitch, "Velocity -> Pitch");
        f->feed({InputEvent::mode()});
        CHECK(f->ui.edit_mode() == Mode::Timing, "Pitch -> Timing");
        f->feed({InputEvent::mode()});
        CHECK(f->ui.edit_mode() == Mode::Balance, "Timing -> Balance");
        f->feed({InputEvent::mode()});
        CHECK(f->ui.edit_mode() == Mode::Edit, "Balance -> Edit");
        CHECK(f->sink.sent.empty() && !f->ui.dirty(), "changer de mode n'envoie rien et ne modifie pas la loop");

        // Le mode est conservé quand on passe par le menu, remis à Edit à chaque ouverture du pattern.
        f->feed({InputEvent::mode(), InputEvent::mode(), InputEvent::menu(), InputEvent::back()});
        CHECK(f->ui.view() == View::Pattern && f->ui.edit_mode() == Mode::Pitch, "le mode survit à un aller-retour par le menu");
        f->feed({InputEvent::back(), InputEvent::ok()});
        CHECK(f->ui.view() == View::Pattern && f->ui.edit_mode() == Mode::Edit, "réouverture du pattern : mode Edit");

        // Ignoré hors de la vue Pattern.
        f->feed({InputEvent::back(), InputEvent::mode()});
        CHECK(f->ui.view() == View::Tracks && f->ui.edit_mode() == Mode::Edit, "Mode ignoré dans la vue Pistes");
        f->feed({InputEvent::wheel(-5), InputEvent::ok(), InputEvent::mode()});
        CHECK(f->ui.view() == View::Loops && f->ui.edit_mode() == Mode::Edit, "Mode ignoré dans la vue Loops");
        CHECK(f->sink.sent.empty(), "aucune commande");
    }

    // ----- Mode Edit : comme avant, la roue ne fait rien -----
    {
        auto f = make();
        f->feed({InputEvent::step(1)});
        CHECK(f->ui.pattern_bits(0) == 0x1113 && f->sink.count(Command::SetStep) == 1, "Edit : un pas bascule");
        f->sink.sent.clear();
        f->feed({InputEvent::wheel(3), InputEvent::wheel(-2)});
        CHECK(f->sink.sent.empty(), "Edit : la roue ne fait rien");
    }

    // ----- Mode Velocity : sélection et réglage à la roue -----
    {
        auto f = make();
        f->feed({InputEvent::mode()});
        f->feed({InputEvent::step(4)});
        CHECK(f->ui.selected_step() == 4 && f->sink.sent.empty() && f->ui.pattern_bits(0) == 0x1111 && !f->ui.dirty(),
              "Velocity : un pas joué se sélectionne, sans changer le pattern ni envoyer de commande");
        f->feed({InputEvent::step(1)});
        CHECK(f->ui.selected_step() == 4 && f->sink.sent.empty() && f->ui.pattern_bits(0) == 0x1111, "Velocity : un pas éteint est ignoré");

        f->feed({InputEvent::wheel(-1)});
        CHECK(f->ui.step_velocity(0, 4) == 123 && f->ui.dirty(), "un cran vers le bas : -4 points");
        CHECK(f->sink.sent.size() == 2 && f->sink.at(0).type == Command::SetStepVelocity && f->sink.at(0).track == 2 &&
                  f->sink.at(0).step == 4 && f->sink.at(0).value == 123.0f,
              "SetStepVelocity sur la piste moteur 2, pas 4");
        CHECK(f->sink.at(1).type == Command::AuditionStep && f->sink.at(1).track == 2 && f->sink.at(1).step == 4,
              "puis la note d'essai de ce pas");
        f->sink.sent.clear();

        // Plusieurs crans, bornes.
        f->tick(100);
        f->feed({InputEvent::wheel(5)});
        CHECK(f->ui.step_velocity(0, 4) == 127 && f->sink.sent.size() == 2 && f->sink.at(0).value == 127.0f, "+5 crans : borné à 127");
        f->tick(100);
        f->sink.sent.clear();
        f->feed({InputEvent::wheel(1)});
        CHECK(f->sink.sent.empty(), "déjà à 127 : aucune commande, aucune note d'essai");
        f->feed({InputEvent::wheel(-100)});
        CHECK(f->ui.step_velocity(0, 4) == 1 && f->sink.at(0).value == 1.0f, "-100 crans : borné à 1");
        f->tick(100);
        f->sink.sent.clear();
        f->feed({InputEvent::wheel(-1)});
        CHECK(f->sink.sent.empty(), "déjà à 1 : rien");
        f->tick(100);
        f->feed({InputEvent::wheel(1)});
        CHECK(f->ui.step_velocity(0, 4) == 5, "1 + 4 = 5");

        // Les autres pas ne bougent pas.
        CHECK(f->ui.step_velocity(0, 0) == 127 && f->ui.step_velocity(0, 8) == 127 && f->ui.step_pitch(0, 4) == 0, "autres valeurs intactes");
    }

    // ----- Mode Pitch : un demi-ton par cran ; la sélection survit aux changements de mode -----
    {
        auto f = make();
        f->feed({InputEvent::mode(), InputEvent::step(8), InputEvent::mode()});  // Velocity, sélection du pas 8, puis Pitch
        CHECK(f->ui.edit_mode() == Mode::Pitch && f->ui.selected_step() == 8, "la sélection survit au changement de mode");
        f->feed({InputEvent::wheel(3)});
        CHECK(f->ui.step_pitch(0, 8) == 3 && f->sink.at(0).type == Command::SetStepPitch && f->sink.at(0).track == 2 &&
                  f->sink.at(0).step == 8 && f->sink.at(0).value == 3.0f,
              "Pitch : +3 crans = +3 demi-tons");
        f->tick(100);
        f->feed({InputEvent::wheel(100)});
        CHECK(f->ui.step_pitch(0, 8) == 12, "borné à +12");
        f->tick(100);
        f->feed({InputEvent::wheel(-100)});
        CHECK(f->ui.step_pitch(0, 8) == -12, "borné à -12");
        f->tick(100);
        f->sink.sent.clear();
        f->feed({InputEvent::wheel(-1)});
        CHECK(f->sink.sent.empty(), "déjà à -12 : rien");

        // Valeurs conservées d'un mode à l'autre ; la roue ne fait rien en Edit.
        f->feed({InputEvent::mode(), InputEvent::mode(), InputEvent::mode()});
        CHECK(f->ui.edit_mode() == Mode::Edit && f->ui.step_pitch(0, 8) == -12, "retour en Edit : valeur conservée");
        f->tick(100);
        f->feed({InputEvent::wheel(2)});
        CHECK(f->sink.sent.empty() && f->ui.step_pitch(0, 8) == -12, "Edit : la roue ne règle rien");
    }

    // ----- Sans sélection, ou sur un pas éteint ensuite, la roue ne fait rien -----
    {
        auto f = make();
        f->feed({InputEvent::mode(), InputEvent::wheel(2)});
        CHECK(f->sink.sent.empty(), "aucun pas sélectionné : la roue ne fait rien");

        f->feed({InputEvent::step(4)});
        f->feed({InputEvent::mode(), InputEvent::mode(), InputEvent::mode(), InputEvent::mode(), InputEvent::step(4)});  // Pitch, Timing, Balance, Edit, puis éteint le pas 4
        CHECK(f->ui.pattern_bits(0) == 0x1101, "le pas 4 est éteint en Edit");
        f->sink.sent.clear();
        f->feed({InputEvent::mode(), InputEvent::wheel(2)});
        CHECK(f->ui.selected_step() == 4 && f->sink.sent.empty(), "pas sélectionné devenu silencieux : la roue ne fait rien");
        f->feed({InputEvent::mode(), InputEvent::mode(), InputEvent::mode(), InputEvent::mode(), InputEvent::step(4), InputEvent::mode(), InputEvent::wheel(-2)});
        CHECK(f->ui.step_velocity(0, 4) == 119 && f->ui.pattern_bits(0) == 0x1111, "rallumé en Edit : la vélocité se règle de nouveau");

        // File de commandes pleine : le modèle reste cohérent avec le moteur.
        auto g = make();
        g->feed({InputEvent::mode(), InputEvent::step(0)});
        g->sink.budget = 0;
        g->feed({InputEvent::wheel(-3)});
        CHECK(g->ui.step_velocity(0, 0) == 127 && !g->ui.dirty(), "file pleine : valeur inchangée");
    }

    // ----- Note d'essai : à chaque réglage, avec une cadence limitée, toujours la valeur finale -----
    {
        auto f = make();
        f->feed({InputEvent::mode(), InputEvent::step(4)});
        f->feed({InputEvent::wheel(-1)});  // t = 1000 : note jouée tout de suite
        CHECK(f->sink.count(Command::AuditionStep) == 1, "premier réglage : note d'essai immédiate");

        f->feed({InputEvent::wheel(-1), InputEvent::wheel(-1)});  // même instant : retenues
        CHECK(f->sink.count(Command::AuditionStep) == 1 && f->sink.count(Command::SetStepVelocity) == 3, "réglages rapprochés : une seule note, toutes les valeurs envoyées");
        f->tick(UiController::AUDITION_MIN_MS - 1);
        CHECK(f->sink.count(Command::AuditionStep) == 1, "pas encore 60 ms");
        f->tick(1);
        CHECK(f->sink.count(Command::AuditionStep) == 2, "60 ms plus tard : la note retenue part (valeur finale)");
        f->tick(500);
        CHECK(f->sink.count(Command::AuditionStep) == 2, "une seule fois");

        // Retenue puis abandonnée : on quitte la vue ou le mode avant qu'elle parte.
        f->feed({InputEvent::wheel(-1)});
        f->feed({InputEvent::wheel(-1)});
        const int before = f->sink.count(Command::AuditionStep);
        f->feed({InputEvent::mode()});  // Pitch
        f->tick(200);
        CHECK(f->sink.count(Command::AuditionStep) == before, "changement de mode : la note retenue est abandonnée");

        f->feed({InputEvent::mode(), InputEvent::mode(), InputEvent::mode()});  // Timing, Balance, Edit
        f->feed({InputEvent::mode()});  // Velocity
        f->tick(200);
        f->feed({InputEvent::wheel(-1)});
        const int sent = f->sink.count(Command::AuditionStep);
        f->feed({InputEvent::wheel(-1)});
        f->feed({InputEvent::back()});
        f->tick(200);
        CHECK(f->sink.count(Command::AuditionStep) == sent, "sortie de la vue : la note retenue est abandonnée");
    }

    // ----- Rendu : niveaux de remplissage, texte, mode -----
    {
        uint8_t vel[16];
        int8_t  pit[16] = {};
        for (int i = 0; i < 16; ++i) vel[i] = 127;
        vel[4]  = 64;
        vel[8]  = 1;
        vel[12] = 32;
        auto f = std::unique_ptr<Fixture>(new Fixture(vel, pit));
        Canvas c;

        // Edit : boîte pleine (49) ou contour (24).
        f->ui.render(c);
        CHECK(box(c, 0) == 49 && box(c, 4) == 49 && box(c, 1) == 24, "Edit : pas joué = boîte pleine, pas éteint = contour");
        Canvas ref;
        CHECK(header_is(c, "a", 'E'), "en-tête : « E » (Edit) à gauche du BPM");
        CHECK(same_rows(c, ref, 56, 64), "plus de ligne « Mode: ... » en bas");

        // Velocity : 127 = pleine, 64 = 3 lignes, 1 = 1 ligne ; un pas joué se distingue toujours d'un pas éteint.
        f->feed({InputEvent::mode(), InputEvent::step(4)});
        f->ui.render(c);
        CHECK(box(c, 0) == 49 && box(c, 4) == 24 + 3 * 5 && box(c, 8) == 24 + 1 * 5 && box(c, 1) == 24,
              "Velocity : 127 = pleine, 64 = 3 lignes, 1 = 1 ligne, éteint = contour");
        CHECK(box(c, 8) > box(c, 1), "vélocité minimale visible : plus que le contour d'un pas éteint");
        CHECK(box(c, 12) == 24 + 2 * 5, "vélocité 32 : 2 lignes (le niveau est arrondi, pas tronqué)");
        CHECK(c.get_pixel(4 * 8 + 3, 22), "le pas sélectionné est repéré par un trait au-dessus");
        CHECK(c.get_pixel(4 * 8 + 3, 24 + 5) && c.get_pixel(4 * 8 + 3, 24 + 3) && !c.get_pixel(4 * 8 + 3, 24 + 2),
              "vélocité 64 : remplie du bas jusqu'à la 3e ligne");
        CHECK(header_is(c, "a", 'V'), "en-tête : « V » (lettre simple, comme « E »)");

        // Jauge sous les boîtes : vélocité 64 = 49 pixels de remplissage sur 98 ; valeur exacte à droite.
        CHECK(gauge(c) == 210 + 49 * 5, "Velocity 64 : jauge remplie à moitié environ");
        CHECK(value_is(c, "64"), "la valeur « 64 » est écrite à droite de la jauge");
        ref.clear();
        CHECK(same_rows(c, ref, 56, 64), "rien sous la jauge");
        CHECK(c.get_pixel(0, 48) && c.get_pixel(99, 54) && c.get_pixel(1, 51) && c.get_pixel(49, 51) && !c.get_pixel(50, 51) && !c.get_pixel(60, 51), "le remplissage part de la gauche et s'arrête à la colonne 49");

        f->feed({InputEvent::step(0)});  // vélocité 127 : jauge pleine
        f->ui.render(c);
        CHECK(gauge(c) == 100 * 7 && value_is(c, "127"), "Velocity 127 : jauge entièrement pleine, « 127 »");

        // Plus bas dans la plage : la vélocité 1 montre une seule ligne remplie.
        f->feed({InputEvent::step(8)});
        f->ui.render(c);
        CHECK(box(c, 8) == 24 + 1 * 5, "Velocity 1 : une seule ligne remplie dans la boîte");
        CHECK(gauge(c) == 210 + 1 * 5 && value_is(c, "1"), "Velocity 1 : un pixel de remplissage dans la jauge, « 1 »");
    }
    {
        uint8_t vel[16];
        int8_t  pit[16] = {};
        for (int i = 0; i < 16; ++i) vel[i] = 127;
        pit[0]  = 0;
        pit[4]  = 12;
        pit[8]  = -12;
        pit[12] = 4;
        auto f = std::unique_ptr<Fixture>(new Fixture(vel, pit));
        Canvas c;
        f->feed({InputEvent::mode(), InputEvent::mode()});
        f->ui.render(c);

        // Pitch 0 : ligne centrale seule ; +12 / -12 : 2 lignes de plus, vers le haut / le bas ; +4 : 1 ligne de plus.
        CHECK(box(c, 0) == 24 + 5 && box(c, 4) == 24 + 3 * 5 && box(c, 8) == 24 + 3 * 5 && box(c, 12) == 24 + 2 * 5 && box(c, 1) == 24,
              "Pitch : niveaux de remplissage");
        CHECK(c.get_pixel(4 * 8 + 3, 24 + 1) && c.get_pixel(4 * 8 + 3, 24 + 3) && !c.get_pixel(4 * 8 + 3, 24 + 5), "+12 : rempli vers le haut");
        CHECK(c.get_pixel(8 * 8 + 3, 24 + 5) && c.get_pixel(8 * 8 + 3, 24 + 3) && !c.get_pixel(8 * 8 + 3, 24 + 1), "-12 : rempli vers le bas");
        CHECK(c.get_pixel(0 * 8 + 3, 24 + 3) && !c.get_pixel(0 * 8 + 3, 24 + 2) && !c.get_pixel(0 * 8 + 3, 24 + 4), "0 : la ligne centrale seulement");

        f->feed({InputEvent::step(4)});
        f->ui.render(c);
        Canvas ref;
        CHECK(header_is(c, "a", 'P'), "en-tête : « P » (lettre simple)");

        // Jauge du pitch : à partir du centre, 4 pixels par demi-ton, un trait dépassant marque le 0.
        CHECK(gauge(c) == 210 + 48 * 5 + 5 + 2 && value_is(c, "+12"), "Pitch +12 : moitié droite pleine, « +12 »");
        CHECK(c.get_pixel(60, 51) && c.get_pixel(98, 51) && !c.get_pixel(40, 51), "+12 : rempli à droite du centre");
        CHECK(c.get_pixel(50, 47) && c.get_pixel(50, 55), "le trait central dépasse de la jauge");
        CHECK(same_rows(c, ref, 56, 64), "rien sous la jauge");

        f->feed({InputEvent::step(8)});
        f->ui.render(c);
        CHECK(box(c, 8) == 24 + 3 * 5, "pitch négatif : boîte remplie vers le bas");
        CHECK(gauge(c) == 210 + 48 * 5 + 5 + 2 && value_is(c, "-12"), "Pitch -12 : moitié gauche pleine, « -12 »");
        CHECK(c.get_pixel(40, 51) && c.get_pixel(2, 51) && !c.get_pixel(60, 51), "-12 : rempli à gauche du centre");

        f->feed({InputEvent::step(0)});
        f->ui.render(c);
        CHECK(gauge(c) == 210 + 5 + 2 && value_is(c, "0"), "Pitch 0 : le contour et le trait central seulement, « 0 »");

        f->feed({InputEvent::step(12)});
        f->ui.render(c);
        CHECK(gauge(c) == 210 + 16 * 5 + 5 + 2 && value_is(c, "+4"), "Pitch +4 : 16 pixels à droite du centre, « +4 »");

        // Sans sélection : ni jauge ni texte. Un pas éteint ne se sélectionne pas : toujours rien.
        auto g = std::unique_ptr<Fixture>(new Fixture(vel, pit));
        g->feed({InputEvent::mode()});
        g->ui.render(c);
        ref.clear();
        CHECK(same_rows(c, ref, 48, 64), "Velocity sans sélection : ni jauge ni texte");
        g->feed({InputEvent::step(1)});
        g->ui.render(c);
        CHECK(same_rows(c, ref, 48, 64), "pas éteint : rien");
        g->feed({InputEvent::mode()});
        g->ui.render(c);
        CHECK(same_rows(c, ref, 48, 64), "Pitch sans sélection : ni jauge ni texte");
        // Un pas sélectionné puis éteint en Edit n'a plus de jauge quand on revient en Velocity ou Pitch.
        auto h = std::unique_ptr<Fixture>(new Fixture(vel, pit));
        h->feed({InputEvent::mode(), InputEvent::step(4)});  // Velocity, pas 4 sélectionné
        h->ui.render(c);
        CHECK(gauge(c) > 0, "(jauge présente tant que le pas est joué)");
        h->feed({InputEvent::mode(), InputEvent::mode(), InputEvent::mode(), InputEvent::mode(), InputEvent::step(4), InputEvent::mode()});  // Edit : éteint le pas 4, puis Velocity
        h->ui.render(c);
        ref.clear();
        CHECK(h->ui.selected_step() == 4 && same_rows(c, ref, 48, 64), "pas sélectionné devenu silencieux : plus de jauge");
        h->feed({InputEvent::mode()});  // Pitch
        h->ui.render(c);
        CHECK(same_rows(c, ref, 48, 64), "idem en Pitch");
        g->feed({InputEvent::mode(), InputEvent::mode(), InputEvent::mode()});
        g->feed({InputEvent::step(4)});
        g->ui.render(c);
        CHECK(lit(c, 0, 48, 128, 16) == 0, "Edit : ni texte ni jauge en bas de l'écran");
        CHECK(!c.get_pixel(99, 54) || lit(c, 0, 48, 100, 7) < 200, "Edit : pas de contour de jauge");
    }

    // ----- La jauge suit la roue -----
    {
        auto f = make();
        f->feed({InputEvent::mode(), InputEvent::step(4)});
        Canvas c;
        f->ui.render(c);
        CHECK(gauge(c) == 100 * 7 && value_is(c, "127"), "départ : jauge pleine, « 127 »");
        f->feed({InputEvent::wheel(-1)});
        f->ui.render(c);
        CHECK(gauge(c) == 210 + 94 * 5 && value_is(c, "123"), "un cran vers le bas : 123, jauge un peu moins pleine");
        f->tick(100);
        f->feed({InputEvent::wheel(-100)});
        f->ui.render(c);
        CHECK(gauge(c) == 210 + 1 * 5 && value_is(c, "1"), "tout en bas : « 1 », un pixel");

        f->feed({InputEvent::mode()});
        f->tick(100);
        f->feed({InputEvent::wheel(3)});
        f->ui.render(c);
        CHECK(gauge(c) == 210 + 12 * 5 + 5 + 2 && value_is(c, "+3"), "Pitch : +3 demi-tons = 12 pixels à droite du centre");
    }

    // ----- Titre long : coupé à 13 caractères pour ne pas toucher la lettre du mode -----
    {
        RecordingSink sink;
        FakeStore     store;
        UiController  ui(sink, nullptr, &store);
        ui.add_track("un_nom_de_fichier_beaucoup_trop_long.wav", 0);
        ui.handle(InputEvent::ok());
        ui.handle(InputEvent::mode());

        Canvas got;
        ui.render(got);
        CHECK(header_is(got, "un_nom_de_fic", 'V'), "titre coupé à 13 caractères, lettre « V » à sa place");
        bool gap_empty = true;
        for (int y = 0; y < 8; ++y)
            for (int x = 13 * 6; x < 88; ++x) gap_empty = gap_empty && !got.get_pixel(x, y);
        CHECK(gap_empty, "un espace libre entre le titre et la lettre");
    }

    // ----- Sauvegarde : les réglages faits à l'écran arrivent dans le profil -----
    {
        auto f = make();
        f->feed({InputEvent::mode(), InputEvent::step(8), InputEvent::wheel(-5), InputEvent::mode(), InputEvent::wheel(-7)});
        CHECK(f->ui.step_velocity(0, 8) == 107 && f->ui.step_pitch(0, 8) == -7, "réglages faits");
        f->feed({InputEvent::back(), InputEvent::wheel(-5), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok()});
        const ProfileData& p = f->store.file;
        CHECK(p.loop_count == 1 && p.loops[0].tracks[0].step_velocity[8] == 107 && p.loops[0].tracks[0].step_pitch[8] == -7 &&
                  p.loops[0].tracks[0].step_velocity[4] == 127 && p.loops[0].tracks[0].step_pitch[4] == 0,
              "sauvegardés avec la loop");
    }

    if (failures == 0) std::printf("OK\n");
    return failures ? 1 : 0;
}
