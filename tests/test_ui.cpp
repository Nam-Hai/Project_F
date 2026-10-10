#include <cstdio>
#include <cstring>
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

// Enregistre les commandes au lieu de les envoyer à un moteur.
struct RecordingSink : CommandSink {
    std::vector<Command> sent;
    bool                 accept = true;

    bool push(const Command& c) override {
        if (!accept) return false;
        sent.push_back(c);
        return true;
    }
};

// Bibliothèque de samples factice : un seul buffer de test, enregistre les appels.
struct FakeLibrary : SampleLibrary {
    std::vector<std::string>   names;
    bool                       fail_load  = false;
    int                        loads      = 0;
    int                        last_index = -1;
    std::vector<const int16_t*> discarded;
    int16_t                    data[4] = {1, 2, 3, 4};

    int         count() const override { return static_cast<int>(names.size()); }
    const char* name(int i) const override { return names[static_cast<std::size_t>(i)].c_str(); }
    int         find(const char* f) const override {
        for (std::size_t i = 0; i < names.size(); ++i)
            if (names[i] == f) return static_cast<int>(i);
        return -1;
    }
    bool        load(int i, const int16_t*& d, uint32_t& length) override {
        if (fail_load) return false;
        ++loads;
        last_index = i;
        d          = data;
        length     = 4;
        return true;
    }
    void discard(const int16_t* d) override { discarded.push_back(d); }
};

// Compare une bande de l'écran (lignes de texte y0..y1) à un rendu de référence.
static bool same_rows(const Canvas& a, const Canvas& b, int y0, int y1) {
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < Canvas::W; ++x)
            if (a.get_pixel(x, y) != b.get_pixel(x, y)) return false;
    return true;
}

static int lit_pixels(const Canvas& c, int x, int y, int w, int h) {
    int n = 0;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) n += c.get_pixel(x + i, y + j);
    return n;
}

static void feed(UiController& ui, std::initializer_list<InputEvent> events) {
    for (const InputEvent& e : events) ui.handle(e);
}

int main() {
    // Noms : l'extension « .wav » est retirée, les noms trop longs sont tronqués.
    {
        RecordingSink sink;
        UiController  ui(sink);
        CHECK(ui.add_track("Kick1.wav", 0) == 0, "première piste");
        CHECK(ui.add_track("SNARE.WAV", 1) == 1, "extension en majuscules");
        ui.add_track("un_nom_de_fichier_beaucoup_trop_long.wav", 2);

        Canvas got, ref;
        ui.render(got);
        ref.rect(0, 8, 128, 8, true);  // la piste sous le curseur est une barre inversée
        ref.text(0, 8, " Kick1", true);
        CHECK(same_rows(got, ref, 8, 16), "piste 0 : nom sans .wav, ligne inversée");

        ref.clear();
        ref.text(0, 16, " SNARE");
        CHECK(same_rows(got, ref, 16, 24), "piste 1 : nom sans .WAV, ligne normale");

        ref.clear();
        ref.text(0, 24, " un_nom_de_fichier_b");  // 19 caractères conservés
        CHECK(same_rows(got, ref, 24, 32), "nom trop long tronqué à 19 caractères");
    }

    // Écran d'accueil : en-tête « Pistes », BPM et pause en haut à droite.
    {
        RecordingSink sink;
        UiController  ui(sink);
        ui.add_track("Kick1.wav", 0);

        Canvas got, ref;
        ui.render(got);
        ref.text(0, 0, "Pistes");
        const int bpm_x = Canvas::W - 6 - 4 - Canvas::text_width("120");
        ref.text(bpm_x, 0, "120");
        ref.text(bpm_x - 6 - Canvas::text_width("L*"), 0, "L*");  // loop jamais sauvegardée
        ref.rect(Canvas::W - 6, 0, 2, 7, true);
        ref.rect(Canvas::W - 3, 0, 2, 7, true);
        CHECK(same_rows(got, ref, 0, 8), "en-tête : titre, L*, BPM 120, icône pause");

        // Dernière ligne de la liste : « + Ajouter piste ».
        ref.clear();
        ref.text(0, 16, " + Ajouter piste");
        CHECK(same_rows(got, ref, 16, 24), "dernière ligne : + Ajouter piste");
    }

    // Navigation à la roue : bornes, défilement.
    {
        RecordingSink sink;
        UiController  ui(sink);
        for (int i = 0; i < 10; ++i) ui.add_track("t.wav", static_cast<uint8_t>(i));  // 10 pistes + « Ajouter »

        ui.handle(InputEvent::wheel(-3));
        CHECK(ui.cursor() == -1 && ui.scroll() == 0, "borne haute : la ligne de titre");
        ui.handle(InputEvent::wheel(7));
        CHECK(ui.cursor() == 6 && ui.scroll() == 0, "dernière ligne visible sans défiler");
        ui.handle(InputEvent::wheel(1));
        CHECK(ui.cursor() == 7 && ui.scroll() == 1, "défilement vers le bas");
        ui.handle(InputEvent::wheel(100));
        CHECK(ui.cursor() == 10 && ui.scroll() == 4, "borne basse : « Ajouter piste »");
        ui.handle(InputEvent::wheel(-100));
        CHECK(ui.cursor() == -1 && ui.scroll() == 0, "retour en haut, sur la ligne de titre");

        Canvas c;
        ui.handle(InputEvent::wheel(11));
        ui.render(c);
        Canvas ref;
        ref.rect(0, 7 * 8, 128, 8, true);
        ref.text(0, 7 * 8, " + Ajouter piste", true);
        CHECK(same_rows(c, ref, 56, 64), "« Ajouter piste » visible en bas après défilement");
    }

    // Vue Pattern : Valider ouvre la piste, les boutons de pas envoient SetStep, Back revient.
    {
        RecordingSink sink;
        UiController  ui(sink);
        ui.add_track("Kick1.wav", 0, 0x0001);
        ui.add_track("Snare1.wav", 5, 0x0000);

        feed(ui, {InputEvent::step(3)});
        CHECK(sink.sent.empty(), "boutons de pas ignorés dans la liste");

        feed(ui, {InputEvent::wheel(1), InputEvent::ok()});
        CHECK(ui.view() == UiController::View::Pattern && ui.selected_track() == 1, "Valider ouvre la piste 1");

        feed(ui, {InputEvent::step(3)});
        CHECK(sink.sent.size() == 1 && sink.sent[0].type == Command::SetStep && sink.sent[0].track == 5 &&
                  sink.sent[0].step == 3 && sink.sent[0].value > 0.5f,
              "pas 3 activé : SetStep sur la piste moteur 5");
        CHECK(ui.pattern_bits(1) == 0x0008 && ui.selected_step() == 3, "modèle mis à jour, pas sélectionné");

        feed(ui, {InputEvent::step(3)});
        CHECK(sink.sent.size() == 2 && sink.sent[1].value < 0.5f && ui.pattern_bits(1) == 0, "deuxième appui : désactivé");

        feed(ui, {InputEvent::step(15), InputEvent::step(0)});
        CHECK(ui.pattern_bits(1) == 0x8001, "pas 0 et 15");
        feed(ui, {InputEvent::step(16)});
        CHECK(sink.sent.size() == 4, "pas hors limites ignoré");

        // Rendu : boîtes pleines pour 0 et 15, vides ailleurs.
        Canvas c;
        ui.render(c);
        CHECK(lit_pixels(c, 0, 24, 7, 7) == 49 && lit_pixels(c, 15 * 8, 24, 7, 7) == 49, "pas actifs : boîtes pleines");
        CHECK(lit_pixels(c, 8, 24, 7, 7) == 24, "pas inactif : contour seul (24 pixels)");

        Canvas ref;
        ref.text(0, 0, "Snare1");
        bool title_ok = true;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 6 * 6; ++x) title_ok = title_ok && c.get_pixel(x, y) == ref.get_pixel(x, y);
        CHECK(title_ok, "titre : nom de la piste, sans .wav");

        feed(ui, {InputEvent::back()});
        CHECK(ui.view() == UiController::View::Tracks && ui.cursor() == 1, "Back : retour à la liste sur la même piste");
        feed(ui, {InputEvent::back()});
        CHECK(ui.view() == UiController::View::Tracks, "Back dans la liste : rien");
    }

    // Mode Edit : plus de texte sur le pas touché (le pas se voit dans sa boîte).
    {
        RecordingSink sink;
        UiController  ui(sink);
        ui.add_track("k.wav", 0);
        feed(ui, {InputEvent::ok(), InputEvent::step(4)});

        Canvas c, ref;
        ui.render(c);
        CHECK(same_rows(c, ref, 48, 64), "Edit : aucun texte en bas de l'écran");
    }

    // Sans bibliothèque de samples, « Ajouter piste » ne fait rien.
    {
        RecordingSink sink;
        UiController  ui(sink);
        ui.add_track("k.wav", 0);
        feed(ui, {InputEvent::wheel(1), InputEvent::ok()});
        CHECK(ui.view() == UiController::View::Tracks && sink.sent.empty(), "pas de bibliothèque : rien");
    }

    // Vue Ajout : liste des samples, création d'une piste en direct.
    {
        FakeLibrary   lib;
        lib.names = {"Clave.wav", "Kick1.wav", "Snare1.wav"};
        RecordingSink sink;
        UiController  ui(sink, &lib);
        ui.add_track("Kick1.wav", 0, 0x0001);
        ui.add_track("Snare1.wav", 2, 0x0000);  // la piste moteur 1 est libre

        feed(ui, {InputEvent::wheel(2), InputEvent::ok()});
        CHECK(ui.view() == UiController::View::Add && ui.add_cursor() == 0, "Valider sur « Ajouter » ouvre la vue Ajout");

        // Rendu : en-tête, puis les trois samples sans « .wav », le premier sélectionné.
        Canvas got, ref;
        ui.render(got);
        ref.text(0, 0, "Samples");
        bool title_ok = true;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 7 * 6; ++x) title_ok = title_ok && got.get_pixel(x, y) == ref.get_pixel(x, y);
        CHECK(title_ok, "titre « Samples »");
        ref.clear();
        ref.rect(0, 8, 128, 8, true);
        ref.text(0, 8, " Clave", true);
        CHECK(same_rows(got, ref, 8, 16), "premier sample sélectionné, sans .wav");
        ref.clear();
        ref.text(0, 16, " Kick1");
        CHECK(same_rows(got, ref, 16, 24), "deuxième sample");

        // Navigation : bornes, Back sans rien créer.
        feed(ui, {InputEvent::wheel(-1)});
        CHECK(ui.add_cursor() == 0, "borne haute");
        feed(ui, {InputEvent::wheel(10)});
        CHECK(ui.add_cursor() == 2, "borne basse");
        feed(ui, {InputEvent::back()});
        CHECK(ui.view() == UiController::View::Tracks && ui.cursor() == 2 && sink.sent.empty() && ui.track_count() == 2,
              "Back : retour sur « Ajouter », rien créé");

        // Choix du 2e sample : SetTrackSample sur la piste moteur libre (1), puis ouverture du Pattern.
        feed(ui, {InputEvent::ok(), InputEvent::wheel(1), InputEvent::ok()});
        CHECK(lib.loads == 1 && lib.last_index == 1, "le sample choisi est chargé");
        CHECK(sink.sent.size() == 1 && sink.sent[0].type == Command::SetTrackSample && sink.sent[0].track == 1 &&
                  sink.sent[0].data == lib.data && sink.sent[0].length == 4,
              "SetTrackSample sur la piste moteur libre, avec le buffer chargé");
        CHECK(ui.track_count() == 3 && ui.engine_track(2) == 1 && ui.pattern_bits(2) == 0, "piste ajoutée, pattern vide");
        CHECK(ui.view() == UiController::View::Pattern && ui.selected_track() == 2, "la nouvelle piste est ouverte");

        // On peut remplir tout de suite le pattern de la nouvelle piste.
        feed(ui, {InputEvent::step(0)});
        CHECK(sink.sent.back().type == Command::SetStep && sink.sent.back().track == 1 && ui.pattern_bits(2) == 1,
              "SetStep sur la nouvelle piste");

        // Retour à la liste : le curseur est sur la nouvelle piste ; « Ajouter » est en dessous.
        feed(ui, {InputEvent::back()});
        CHECK(ui.cursor() == 2, "retour à la liste sur la nouvelle piste");
        ui.render(got);
        ref.clear();
        ref.rect(0, 24, 128, 8, true);
        ref.text(0, 24, " Kick1", true);  // nom du sample 1 : « Kick1 »
        CHECK(same_rows(got, ref, 24, 32), "la nouvelle piste apparaît dans la liste");
        ref.clear();
        ref.text(0, 32, " + Ajouter piste");
        CHECK(same_rows(got, ref, 32, 40), "« Ajouter piste » reste en dernière ligne");
    }

    // Vue Ajout : défilement d'une longue liste.
    {
        FakeLibrary lib;
        for (int i = 0; i < 12; ++i) lib.names.push_back("s" + std::to_string(i) + ".wav");
        RecordingSink sink;
        UiController  ui(sink, &lib);
        feed(ui, {InputEvent::ok()});  // la seule ligne de la liste est « Ajouter piste »
        CHECK(ui.view() == UiController::View::Add, "vue Ajout");
        ui.handle(InputEvent::wheel(9));
        CHECK(ui.add_cursor() == 9 && ui.add_scroll() == 3, "défilement vers le bas");
        ui.handle(InputEvent::wheel(100));
        CHECK(ui.add_cursor() == 11 && ui.add_scroll() == 5, "dernier sample visible");
    }

    // Échecs : chargement impossible, file pleine, UI pleine.
    {
        FakeLibrary lib;
        lib.names = {"a.wav"};
        RecordingSink sink;
        UiController  ui(sink, &lib);
        EngineState   st;
        for (auto& n : st.triggers) n.store(0);
        ui.update(st, 1000);

        feed(ui, {InputEvent::ok()});
        lib.fail_load = true;
        feed(ui, {InputEvent::ok()});
        CHECK(ui.view() == UiController::View::Add && ui.track_count() == 0 && sink.sent.empty(), "échec du chargement : on reste dans la vue");
        CHECK(ui.error_shown(), "message d'erreur affiché");
        Canvas c, ref;
        ui.render(c);
        ref.text(0, 56, " Echec du chargement");
        CHECK(same_rows(c, ref, 56, 64), "texte du message d'erreur");

        ui.update(st, 1000 + UiController::ERROR_MS + 1);
        CHECK(!ui.error_shown(), "le message disparaît après 2 s");

        lib.fail_load = true;
        feed(ui, {InputEvent::ok()});
        CHECK(ui.error_shown(), "erreur de nouveau");
        feed(ui, {InputEvent::wheel(1)});
        CHECK(!ui.error_shown(), "toute action efface l'erreur");

        lib.fail_load = false;
        sink.accept   = false;  // file de commandes pleine
        feed(ui, {InputEvent::ok()});
        CHECK(lib.discarded.size() == 1 && lib.discarded[0] == lib.data, "file pleine : le buffer chargé est rendu à la bibliothèque");
        CHECK(ui.track_count() == 0 && ui.view() == UiController::View::Add && ui.error_shown(), "file pleine : aucune piste créée");
    }
    {
        FakeLibrary lib;
        lib.names = {"a.wav"};
        RecordingSink sink;
        UiController  ui(sink, &lib);
        for (int i = 0; i < cfg::MAX_TRACKS; ++i) ui.add_track("x.wav", static_cast<uint8_t>(i));
        feed(ui, {InputEvent::wheel(100), InputEvent::ok()});
        CHECK(ui.view() == UiController::View::Tracks && lib.loads == 0, "UI pleine : « Ajouter » n'ouvre rien");

        FakeLibrary empty;
        UiController ui2(sink, &empty);
        feed(ui2, {InputEvent::ok()});
        CHECK(ui2.view() == UiController::View::Tracks, "bibliothèque vide : « Ajouter » n'ouvre rien");
    }

    // Bouton Menu : menu contextuel de la piste, depuis la liste (sans entrer dans le pattern) ou le pattern.
    {
        FakeLibrary lib;
        lib.names = {"Clave.wav", "Kick1.wav", "Snare1.wav"};
        RecordingSink sink;
        UiController  ui(sink, &lib);
        ui.add_track("Kick1.wav", 0, 0x0001);
        ui.add_track("Snare1.wav", 2, 0x0410);

        feed(ui, {InputEvent::wheel(2), InputEvent::menu()});
        CHECK(ui.view() == UiController::View::Tracks, "« Ajouter piste » n'a pas de menu");

        feed(ui, {InputEvent::wheel(-1), InputEvent::menu()});
        CHECK(ui.view() == UiController::View::Menu && ui.selected_track() == 1 && ui.menu_cursor() == 0,
              "Menu depuis la liste : menu de la piste sous le curseur, sans passer par le pattern");

        Canvas got, ref;
        ui.render(got);
        ref.text(0, 0, "Snare1");
        bool title_ok = true;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 6 * 6; ++x) title_ok = title_ok && got.get_pixel(x, y) == ref.get_pixel(x, y);
        CHECK(title_ok, "menu : titre = nom de la piste");
        ref.clear();
        ref.rect(0, 8, 128, 8, true);
        ref.text(0, 8, " Changer le sample", true);
        ref.text(0, 16, " Supprimer la piste");
        ref.text(0, 24, " Annuler");
        CHECK(same_rows(got, ref, 8, 32), "menu : trois entrées, la première sélectionnée");

        feed(ui, {InputEvent::wheel(5)});
        CHECK(ui.menu_cursor() == 2, "menu : borne basse");
        feed(ui, {InputEvent::wheel(-9)});
        CHECK(ui.menu_cursor() == 0, "menu : borne haute");

        // Annuler, Back et Menu rappuyé : retour à la liste, curseur inchangé, rien envoyé.
        feed(ui, {InputEvent::wheel(2), InputEvent::ok()});
        CHECK(ui.view() == UiController::View::Tracks && ui.cursor() == 1 && sink.sent.empty(), "Annuler : retour à la liste");
        feed(ui, {InputEvent::menu(), InputEvent::back()});
        CHECK(ui.view() == UiController::View::Tracks && ui.cursor() == 1, "Back : retour à la liste");
        feed(ui, {InputEvent::menu(), InputEvent::menu()});
        CHECK(ui.view() == UiController::View::Tracks && sink.sent.empty(), "Menu rappuyé : referme le menu");

        // Valider dans le pattern n'ouvre plus rien ; le bouton Menu y ouvre le menu de la piste.
        feed(ui, {InputEvent::ok()});
        CHECK(ui.view() == UiController::View::Pattern && ui.selected_track() == 1, "Valider sur une piste : vue Pattern");
        feed(ui, {InputEvent::ok()});
        CHECK(ui.view() == UiController::View::Pattern, "Valider dans le pattern : rien");
        feed(ui, {InputEvent::menu()});
        CHECK(ui.view() == UiController::View::Menu, "Menu dans le pattern : menu de la piste");
        feed(ui, {InputEvent::back()});
        CHECK(ui.view() == UiController::View::Pattern, "Back : retour au pattern (vue d'origine)");
        feed(ui, {InputEvent::menu(), InputEvent::wheel(2), InputEvent::ok()});
        CHECK(ui.view() == UiController::View::Pattern && sink.sent.empty(), "Annuler : retour au pattern");

        // Dans la vue Ajout, pas de menu pour l'instant.
        feed(ui, {InputEvent::back(), InputEvent::wheel(5), InputEvent::ok(), InputEvent::menu()});
        CHECK(ui.view() == UiController::View::Add, "Menu dans la vue Ajout : rien");
    }

    // Suppression d'une piste : demande de confirmation, puis ClearTrack et décalage de la liste.
    {
        RecordingSink sink;
        UiController  ui(sink);
        ui.add_track("Kick1.wav", 0, 0x0001);
        ui.add_track("Snare1.wav", 2, 0x0410);

        feed(ui, {InputEvent::wheel(1), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok()});
        CHECK(ui.view() == UiController::View::Menu && ui.confirming_delete() && sink.sent.empty(),
              "première validation : confirmation demandée, rien supprimé");

        Canvas got, ref;
        ui.render(got);
        ref.rect(0, 16, 128, 8, true);
        ref.text(0, 16, " Confirmer ?", true);
        CHECK(same_rows(got, ref, 16, 24), "le libellé devient « Confirmer ? »");

        feed(ui, {InputEvent::wheel(1), InputEvent::wheel(-1)});
        CHECK(!ui.confirming_delete(), "bouger la roue annule la confirmation");
        feed(ui, {InputEvent::ok()});
        CHECK(ui.confirming_delete(), "nouvelle demande de confirmation");
        feed(ui, {InputEvent::back()});
        CHECK(ui.view() == UiController::View::Tracks && !ui.confirming_delete() && sink.sent.empty(), "Back annule la confirmation");

        feed(ui, {InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok(), InputEvent::ok()});
        CHECK(sink.sent.size() == 1 && sink.sent[0].type == Command::ClearTrack && sink.sent[0].track == 2,
              "seconde validation : ClearTrack sur la piste moteur 2");
        CHECK(ui.track_count() == 1 && ui.view() == UiController::View::Tracks && ui.cursor() == 1,
              "piste retirée, retour à la liste sur « Ajouter piste »");
        CHECK(std::strcmp(ui.track_name(0), "Kick1") == 0, "l'autre piste reste");
    }
    {
        RecordingSink sink;
        UiController  ui(sink);
        ui.add_track("Kick1.wav", 0, 0x0001);
        ui.add_track("Snare1.wav", 2, 0x0410);
        feed(ui, {InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok(), InputEvent::ok()});
        CHECK(sink.sent.size() == 1 && sink.sent[0].track == 0, "suppression de la première piste : ClearTrack 0");
        CHECK(ui.track_count() == 1 && std::strcmp(ui.track_name(0), "Snare1") == 0 && ui.engine_track(0) == 2 &&
                  ui.pattern_bits(0) == 0x0410 && ui.cursor() == 0,
              "les pistes suivantes remontent avec leur pattern");
    }
    {
        // Depuis le pattern : après la suppression, on revient à la liste (la piste n'existe plus).
        RecordingSink sink;
        UiController  ui(sink);
        ui.add_track("Kick1.wav", 0);
        feed(ui, {InputEvent::ok(), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok(), InputEvent::ok()});
        CHECK(ui.track_count() == 0 && ui.view() == UiController::View::Tracks && ui.cursor() == 0,
              "suppression depuis le pattern : retour à la liste");
    }
    {
        RecordingSink sink;
        UiController  ui(sink);
        ui.add_track("Kick1.wav", 0);
        feed(ui, {InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok()});
        sink.accept = false;  // file pleine au moment de confirmer
        feed(ui, {InputEvent::ok()});
        CHECK(ui.track_count() == 1 && ui.view() == UiController::View::Menu && ui.error_shown() && !ui.confirming_delete(),
              "file pleine : la piste n'est pas supprimée, erreur affichée");
        Canvas c, ref;
        ui.render(c);
        ref.text(0, 56, " Echec de l'action");
        CHECK(same_rows(c, ref, 56, 64), "texte du message d'erreur du menu");
    }

    // Changer le sample d'une piste depuis la liste : le pattern est conservé.
    {
        FakeLibrary lib;
        lib.names = {"Clave.wav", "Kick1.wav", "Snare1.wav"};
        RecordingSink sink;
        UiController  ui(sink, &lib);
        ui.add_track("Kick1.wav", 0, 0x0001);
        ui.add_track("Snare1.wav", 2, 0x0410);
        EngineState st;
        for (auto& n : st.triggers) n.store(0);
        ui.update(st, 1000);

        feed(ui, {InputEvent::wheel(1), InputEvent::menu(), InputEvent::ok()});
        CHECK(ui.view() == UiController::View::Add && ui.replacing() && ui.add_cursor() == 0,
              "« Changer le sample » ouvre la vue Ajout en mode remplacement");

        Canvas got, ref;
        ui.render(got);
        ref.text(0, 0, "Samples");
        bool title_ok = true;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 7 * 6; ++x) title_ok = title_ok && got.get_pixel(x, y) == ref.get_pixel(x, y);
        CHECK(title_ok, "le mode remplacement a le même titre « Samples »");

        feed(ui, {InputEvent::wheel(1)});
        ui.update(st, 1150);  // la roue s'est arrêtée : écoute du sample 1
        CHECK(lib.loads == 1 && lib.last_index == 1 && sink.sent.size() == 1 && sink.sent[0].type == Command::PreviewSample,
              "écoute du sample survolé");
        sink.sent.clear();

        feed(ui, {InputEvent::ok()});
        CHECK(sink.sent.size() == 2 && sink.sent[0].type == Command::StopPreview && sink.sent[1].type == Command::SetTrackSample &&
                  sink.sent[1].track == 2 && sink.sent[1].data == lib.data,
              "Valider : coupe l'écoute puis donne le sample à la piste moteur 2");
        CHECK(ui.view() == UiController::View::Tracks && ui.cursor() == 1 && ui.track_count() == 2,
              "retour à la liste (vue d'origine), aucune piste créée");
        CHECK(ui.pattern_bits(1) == 0x0410 && std::strcmp(ui.track_name(1), "Kick1") == 0, "pattern conservé, nouveau nom");

        // Back en mode remplacement : retour à la vue d'origine, sans rien envoyer.
        sink.sent.clear();
        feed(ui, {InputEvent::menu(), InputEvent::ok(), InputEvent::back()});
        CHECK(ui.view() == UiController::View::Tracks && sink.sent.empty(), "Back en remplacement : retour à la liste");
    }
    {
        // Depuis le pattern : on revient au pattern.
        FakeLibrary lib;
        lib.names = {"Clave.wav", "Kick1.wav"};
        RecordingSink sink;
        UiController  ui(sink, &lib);
        ui.add_track("Snare1.wav", 2, 0x0410);
        feed(ui, {InputEvent::ok(), InputEvent::menu(), InputEvent::ok(), InputEvent::back()});
        CHECK(ui.view() == UiController::View::Pattern, "Back en remplacement : retour au pattern");
        feed(ui, {InputEvent::menu(), InputEvent::ok(), InputEvent::ok()});
        CHECK(ui.view() == UiController::View::Pattern && std::strcmp(ui.track_name(0), "Clave") == 0 && ui.pattern_bits(0) == 0x0410,
              "remplacement validé : retour au pattern, pattern conservé");
    }
    {
        RecordingSink sink;
        UiController  ui(sink);  // pas de bibliothèque
        ui.add_track("Kick1.wav", 0);
        feed(ui, {InputEvent::menu(), InputEvent::ok()});
        CHECK(ui.view() == UiController::View::Menu, "sans bibliothèque, « Changer le sample » ne fait rien");
    }

    // Écoute dans la vue Ajout : après un court arrêt de la roue, jamais à l'ouverture.
    {
        FakeLibrary lib;
        lib.names = {"a.wav", "b.wav", "c.wav", "d.wav"};
        RecordingSink sink;
        UiController  ui(sink, &lib);
        EngineState   st;
        for (auto& n : st.triggers) n.store(0);
        ui.update(st, 1000);

        feed(ui, {InputEvent::ok()});
        ui.update(st, 1500);
        CHECK(sink.sent.empty() && lib.loads == 0, "pas d'écoute à l'ouverture de la vue");

        feed(ui, {InputEvent::wheel(-1)});
        ui.update(st, 2000);
        CHECK(sink.sent.empty(), "la roue n'a pas bougé (borne) : pas d'écoute");

        feed(ui, {InputEvent::wheel(1)});
        ui.update(st, 2149);
        CHECK(sink.sent.empty(), "pas d'écoute avant 150 ms d'arrêt de la roue");
        ui.update(st, 2150);
        CHECK(sink.sent.size() == 1 && sink.sent[0].type == Command::PreviewSample && sink.sent[0].data == lib.data &&
                  sink.sent[0].length == 4 && lib.last_index == 1,
              "écoute du sample 1 après 150 ms");
        ui.update(st, 2400);
        CHECK(sink.sent.size() == 1, "une seule écoute par arrêt de la roue");

        // La roue continue : seul le dernier sample survolé est joué.
        ui.update(st, 3000);
        feed(ui, {InputEvent::wheel(1)});
        ui.update(st, 3100);
        feed(ui, {InputEvent::wheel(1)});
        ui.update(st, 3240);
        CHECK(sink.sent.size() == 1, "le délai repart à chaque cran de roue");
        ui.update(st, 3250);
        CHECK(sink.sent.size() == 2 && lib.last_index == 3, "seul le dernier sample survolé est joué");

        // Back : l'écoute est coupée une seule fois.
        feed(ui, {InputEvent::back()});
        CHECK(sink.sent.size() == 3 && sink.sent[2].type == Command::StopPreview, "Back coupe l'écoute");
        feed(ui, {InputEvent::ok()});
        feed(ui, {InputEvent::back()});
        CHECK(sink.sent.size() == 3, "pas d'écoute en cours : pas de StopPreview inutile");
    }
    {
        FakeLibrary lib;
        lib.names = {"a.wav", "b.wav"};
        RecordingSink sink;
        UiController  ui(sink, &lib);
        EngineState   st;
        for (auto& n : st.triggers) n.store(0);
        ui.update(st, 0);

        feed(ui, {InputEvent::ok(), InputEvent::wheel(1)});
        lib.fail_load = true;
        ui.update(st, 200);
        CHECK(sink.sent.empty() && !ui.error_shown(), "échec de chargement de l'écoute : silencieux");

        feed(ui, {InputEvent::wheel(-1)});
        lib.fail_load = false;
        sink.accept   = false;
        ui.update(st, 400);
        CHECK(lib.discarded.size() == 1, "file pleine : le buffer d'écoute est rendu à la bibliothèque");
        feed(ui, {InputEvent::back()});
        CHECK(sink.sent.empty(), "l'écoute n'a pas démarré : pas de StopPreview");
    }

    // Titre trop long : coupé (16 caractères dans la plupart des vues), il ne touche jamais l'indicateur.
    {
        RecordingSink sink;
        UiController  ui(sink);
        ui.add_track("un_nom_de_fichier_beaucoup_trop_long.wav", 0);  // 19 caractères conservés

        // Vue Menu : 16 caractères, comme les autres vues sans lettre de mode.
        ui.handle(InputEvent::menu());
        Canvas got, ref;
        ui.render(got);
        ref.text(0, 0, "un_nom_de_fichie");  // 16 caractères
        bool title_ok = true;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 16 * 6; ++x) title_ok = title_ok && got.get_pixel(x, y) == ref.get_pixel(x, y);
        CHECK(title_ok, "titre coupé à 16 caractères");

        // Entre la fin du titre (96 px) et le BPM (à partir de 100 px) : rien.
        bool gap_empty = true;
        for (int y = 0; y < 8; ++y)
            for (int x = 96; x < 100; ++x) gap_empty = gap_empty && !got.get_pixel(x, y);
        CHECK(gap_empty, "le titre ne touche pas l'indicateur de droite");

        // Vue Pattern : 13 caractères, pour laisser la place à la lettre du mode.
        ui.handle(InputEvent::back());
        ui.handle(InputEvent::ok());
        ui.render(got);
        ref.clear();
        ref.text(0, 0, "un_nom_de_fic");
        title_ok = true;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 13 * 6; ++x) title_ok = title_ok && got.get_pixel(x, y) == ref.get_pixel(x, y);
        CHECK(title_ok, "vue Pattern : titre coupé à 13 caractères");
    }

    // Play / Stop.
    {
        RecordingSink sink;
        UiController  ui(sink);
        feed(ui, {InputEvent::play()});
        CHECK(sink.sent.size() == 1 && sink.sent[0].type == Command::Play && ui.playing(), "Play");
        feed(ui, {InputEvent::play()});
        CHECK(sink.sent.size() == 2 && sink.sent[1].type == Command::Stop && !ui.playing(), "Stop");

        sink.accept = false;
        feed(ui, {InputEvent::play()});
        CHECK(!ui.playing(), "file pleine : l'état de l'UI ne change pas");
    }

    // Potard BPM : 0 -> 60, 1 -> 200, pas de commande si le BPM ne change pas.
    {
        RecordingSink sink;
        UiController  ui(sink);
        feed(ui, {InputEvent::pot_bpm(0.0f)});
        CHECK(ui.bpm() == 60 && sink.sent.size() == 1 && sink.sent[0].type == Command::SetBpm && sink.sent[0].value == 60.0f,
              "potard à 0 : 60 BPM");
        feed(ui, {InputEvent::pot_bpm(0.001f)});
        CHECK(sink.sent.size() == 1, "mouvement infime : aucune commande");
        feed(ui, {InputEvent::pot_bpm(1.0f)});
        CHECK(ui.bpm() == 200 && sink.sent.back().value == 200.0f, "potard à 1 : 200 BPM");
        feed(ui, {InputEvent::pot_bpm(0.5f)});
        CHECK(ui.bpm() == 130, "potard au milieu : 130 BPM");
        feed(ui, {InputEvent::pot_bpm(7.0f)});
        CHECK(ui.bpm() == 200, "valeur hors plage bornée");
    }

    // Retour du moteur : BPM, lecture, pas courant, indicateur de jeu des pistes.
    {
        RecordingSink sink;
        UiController  ui(sink);
        ui.add_track("Kick1.wav", 2);
        ui.add_track("Snare1.wav", 3);

        EngineState st;
        for (auto& n : st.triggers) n.store(0);
        st.playing.store(true);
        st.bpm.store(98.0f);
        st.step.store(5);
        ui.update(st, 1000);
        CHECK(ui.bpm() == 98 && ui.playing(), "update : BPM et lecture lus depuis le moteur");

        Canvas c, ref;
        ui.render(c);
        // Snare1 est la 2e ligne (non sélectionnée) : l'indicateur est un carré 4x4 à droite.
        CHECK(lit_pixels(c, Canvas::W - 6, 16 + 2, 4, 4) == 0, "pas d'indicateur au premier update");

        st.triggers[3].store(1);  // la piste moteur 3 (Snare1) vient de jouer
        ui.update(st, 1010);
        ui.render(c);
        CHECK(lit_pixels(c, Canvas::W - 6, 16 + 2, 4, 4) == 16, "indicateur allumé sur la piste qui joue");
        CHECK(lit_pixels(c, Canvas::W - 6, 8 + 2, 4, 4) == 4 * 4, "la piste sélectionnée reste une barre pleine (Kick1 n'a pas joué)");

        ui.update(st, 1010 + UiController::LIT_MS - 1);
        ui.render(c);
        CHECK(lit_pixels(c, Canvas::W - 6, 18, 4, 4) == 16, "encore allumé juste avant la fin");
        ui.update(st, 1010 + UiController::LIT_MS + 1);
        ui.render(c);
        CHECK(lit_pixels(c, Canvas::W - 6, 18, 4, 4) == 0, "éteint après 120 ms");

        // Pas en cours de lecture dans la vue Pattern : barre sous la boîte du pas 5.
        feed(ui, {InputEvent::ok()});
        ui.update(st, 2000);
        ui.render(c);
        CHECK(lit_pixels(c, 5 * 8, 24 + 9, 7, 2) == 14, "pas 5 en lecture : barre sous la boîte");
        CHECK(lit_pixels(c, 4 * 8, 24 + 9, 7, 2) == 0, "autres pas : pas de barre");

        st.playing.store(false);
        ui.update(st, 2100);
        ui.render(c);
        CHECK(lit_pixels(c, 5 * 8, 24 + 9, 7, 2) == 0, "à l'arrêt : plus de barre de lecture");
    }

    // Capacité : plus de pistes que MAX_TRACKS refusées.
    {
        RecordingSink sink;
        UiController  ui(sink);
        for (int i = 0; i < cfg::MAX_TRACKS; ++i) CHECK(ui.add_track("x.wav", static_cast<uint8_t>(i)) == i, "ajout");
        CHECK(ui.add_track("x.wav", 0) == -1, "UI pleine : refus");
    }

    if (failures == 0) std::printf("OK\n");
    return failures ? 1 : 0;
}
