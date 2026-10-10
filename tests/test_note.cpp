// Commande Note : la piste joue tout de suite, à une vélocité et un pitch donnés (clavier de piano).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

#include "../config.h"
#include "../core/dsp_utils.h"
#include "../core/engine.h"

static int failures = 0;

#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            std::printf("FAIL: %s\n", msg);         \
            ++failures;                             \
        }                                           \
    } while (0)

static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

// « Impulsion » : un seul échantillon à 0,5 -> la sortie vaut tanh(MASTER_GAIN x 0,5 x gain).
static const int16_t impulse[] = {16384, 0};
// Rampe : la vitesse de lecture se lit dans les valeurs rendues.
static const int16_t ramp[] = {0, 8000, 16000, 24000, 32000, 0};

static float expected(float gain) { return std::tanh(cfg::MASTER_GAIN * 0.5f * gain); }

static Command cmd(Command::Type type, int track, int step, float value) {
    Command c;
    c.type  = type;
    c.track = static_cast<uint8_t>(track);
    c.step  = static_cast<uint8_t>(step);
    c.value = value;
    return c;
}

static Command note(int track, float velocity, int pitch) {
    Command c = cmd(Command::Note, track, 0, velocity);
    c.pitch   = static_cast<int8_t>(pitch);
    return c;
}

static Command set_sample(int track, const int16_t* data, uint32_t length) {
    Command c = cmd(Command::SetTrackSample, track, 0, 0.0f);
    c.data    = data;
    c.length  = length;
    return c;
}

static std::vector<float> render(Engine& e, int frames) {
    std::vector<float> l(static_cast<std::size_t>(frames)), r(static_cast<std::size_t>(frames));
    e.render(l.data(), r.data(), frames);
    return l;
}

// Une piste 2 avec `sample` ; `setup` règle le reste ; on joue une Note et on rend 64 échantillons.
static std::vector<float> play_note(const int16_t* sample, uint32_t length, float velocity, int pitch,
                                    const std::function<void(Engine&)>& setup = [](Engine&) {}) {
    Engine e;
    e.push(set_sample(2, sample, length));
    setup(e);
    e.push(note(2, velocity, pitch));
    return render(e, 64);
}

int main() {
    // ----- Vélocité : le volume suit vélocité / 127 -----
    {
        for (int v : {127, 100, 64, 1}) {
            const auto out = play_note(impulse, 2, static_cast<float>(v), 0);
            CHECK(near(out[0], expected(static_cast<float>(v) / 127.0f)), "vélocité : niveau = tanh(master x 0,5 x v/127)");
        }
        CHECK(near(play_note(impulse, 2, 90.6f, 0)[0], expected(91.0f / 127.0f)), "vélocité arrondie à l'entier le plus proche");
        CHECK(near(play_note(impulse, 2, 0.0f, 0)[0], expected(1.0f / 127.0f)), "vélocité 0 bornée à 1 (jamais muette)");
        CHECK(near(play_note(impulse, 2, -40.0f, 0)[0], expected(1.0f / 127.0f)), "vélocité négative bornée à 1");
        CHECK(near(play_note(impulse, 2, 500.0f, 0)[0], expected(1.0f)), "vélocité au-delà de 127 bornée à 127");

        // Le gain de la piste se multiplie avec la vélocité.
        const auto both = play_note(impulse, 2, 64.0f, 0, [](Engine& e) { e.push(cmd(Command::SetTrackGain, 2, 0, 0.5f)); });
        CHECK(near(both[0], expected(0.5f * 64.0f / 127.0f)), "gain de la piste x vélocité");
    }

    // ----- Pitch : même résultat qu'un pitch de piste équivalent -----
    {
        auto with_track = [](float ratio) {
            return play_note(ramp, 6, 127.0f, 0, [&](Engine& e) { e.push(cmd(Command::SetTrackPitch, 2, 0, ratio)); });
        };
        const auto base = play_note(ramp, 6, 127.0f, 0);

        CHECK(play_note(ramp, 6, 127.0f, 12) == with_track(2.0f), "+12 demi-tons = une octave plus haut (vitesse x2)");
        CHECK(play_note(ramp, 6, 127.0f, -12) == with_track(0.5f), "-12 demi-tons = une octave plus bas (vitesse x0,5)");
        CHECK(play_note(ramp, 6, 127.0f, 7) == with_track(dsp::semitone_ratio(7)), "+7 demi-tons : quinte");
        CHECK(play_note(ramp, 6, 127.0f, -5) == with_track(dsp::semitone_ratio(-5)), "-5 demi-tons : quarte plus bas");
        CHECK(play_note(ramp, 6, 127.0f, 12) != base && play_note(ramp, 6, 127.0f, -12) != base, "le pitch change bien le son");

        // Bornes : au-delà de ±12, la valeur est ramenée à ±12.
        Command high = note(2, 127.0f, 0);
        high.pitch   = 100;
        Command low  = note(2, 127.0f, 0);
        low.pitch    = -100;
        auto run     = [&](const Command& c) {
            Engine e;
            e.push(set_sample(2, ramp, 6));
            e.push(c);
            return render(e, 64);
        };
        CHECK(run(high) == play_note(ramp, 6, 127.0f, 12), "pitch > 12 borné à +12");
        CHECK(run(low) == play_note(ramp, 6, 127.0f, -12), "pitch < -12 borné à -12");

        // Le pitch de la note et celui de la piste se multiplient.
        const auto cancel = play_note(ramp, 6, 127.0f, 12, [](Engine& e) { e.push(cmd(Command::SetTrackPitch, 2, 0, 0.5f)); });
        CHECK(cancel == base, "piste x0,5 et note +12 : vitesse normale");

        // Pitch par défaut de la commande : 0.
        Command plain = cmd(Command::Note, 2, 0, 127.0f);
        CHECK(run(plain) == base, "sans pitch précisé : pitch 0");
    }

    // ----- Rien d'autre n'est touché : ni le séquenceur, ni le pattern -----
    {
        Engine e;
        e.push(set_sample(2, impulse, 2));
        e.push(note(2, 127.0f, 0));
        const auto out = render(e, 256);
        CHECK(near(out[0], expected(1.0f)) && out[1] == 0.0f, "la note sonne tout de suite, séquenceur à l'arrêt");
        CHECK(!e.state().playing.load() && e.state().step.load() == -1, "le séquenceur n'a pas bougé");
        CHECK(e.state().triggers[2].load() == 1, "compté comme un déclenchement de la piste");

        // Le pattern est resté vide : lancer la lecture ne joue rien.
        e.push(cmd(Command::Play, 0, 0, 0.0f));
        const auto after = render(e, 6 * 5513);
        bool silent = true;
        for (float v : after) silent = silent && v == 0.0f;
        CHECK(silent, "aucun pas n'a été ajouté au pattern");
    }

    // ----- Une Note pendant la lecture ne dérange pas le rythme -----
    {
        auto trigs = [](bool with_notes) {
            Engine e;
            e.push(set_sample(2, impulse, 2));
            e.push(set_sample(3, ramp, 6));
            e.push(cmd(Command::SetStep, 2, 0, 1.0f));
            e.push(cmd(Command::SetStep, 2, 4, 1.0f));
            e.push(cmd(Command::Play, 0, 0, 0.0f));
            std::vector<float> l(22100), r(22100);
            int pos = 0;
            const int sizes[] = {128, 37, 200, 1, 64};
            int k = 0;
            while (pos < 22100) {
                if (with_notes && k % 7 == 3) e.push(note(3, 100.0f, 5));  // piste 3 : on joue des notes autour
                int n = sizes[k++ % 5];
                if (22100 - pos < n) n = 22100 - pos;
                e.render(l.data() + pos, r.data() + pos, n);
                pos += n;
            }
            return l;
        };
        const auto quiet = trigs(false);
        const auto busy  = trigs(true);
        // Les trigs de la piste 2 (impulsions fortes) sont aux mêmes échantillons, avec ou sans notes sur la piste 3.
        CHECK(quiet[0] > 0.3f && busy[0] > 0.3f && quiet[22050] > 0.3f && busy[22050] > 0.3f, "trigs de la piste 2 aux échantillons 0 et 22050");
        int hits_quiet = 0, hits_busy = 0;
        for (std::size_t i = 0; i < quiet.size(); ++i) {
            hits_quiet += quiet[i] > 0.3f;
            hits_busy += busy[i] > 0.3f;
        }
        CHECK(hits_quiet == 2 && hits_busy >= 2, "le rythme de la piste 2 est inchangé");
        bool diff = false;
        for (std::size_t i = 0; i < quiet.size(); ++i) diff = diff || quiet[i] != busy[i];
        CHECK(diff, "(les notes de la piste 3 s'entendent bien)");
    }

    // ----- Relance : deux notes de suite ne s'additionnent pas -----
    {
        Engine e;
        e.push(set_sample(2, impulse, 2));
        e.push(note(2, 127.0f, 0));
        e.push(note(2, 127.0f, 0));
        const auto out = render(e, 64);
        CHECK(near(out[0], expected(1.0f)), "deux notes de suite : la seconde relance l'échantillon (pas de cumul)");
        CHECK(e.state().triggers[2].load() == 2, "les deux sont comptées");
    }

    // ----- Le pitch d'une note ne persiste pas : le pas suivant joue au pitch de la piste -----
    {
        Engine e;
        e.push(set_sample(2, ramp, 6));
        e.push(note(2, 127.0f, 12));
        render(e, 64);  // la note (vitesse x2) se termine
        e.push(cmd(Command::SetStep, 2, 0, 1.0f));
        e.push(cmd(Command::Play, 0, 0, 0.0f));
        const auto step = render(e, 64);
        CHECK(step == play_note(ramp, 6, 127.0f, 0), "un pas joué après une note aiguë retrouve sa vitesse normale");
    }

    // ----- Pistes sans voix ou invalides : ignorées -----
    {
        Engine e;
        e.push(set_sample(2, impulse, 2));
        e.push(note(9, 127.0f, 0));      // piste sans voix
        e.push(note(200, 127.0f, 0));    // piste hors limites
        const auto out = render(e, 64);
        bool silent = true;
        for (float v : out) silent = silent && v == 0.0f;
        CHECK(silent, "piste sans voix ou invalide : rien ne sonne, sans plantage");
        CHECK(e.state().triggers[9].load() == 0, "aucun déclenchement compté");

        // Après un ClearTrack, la piste n'a plus de voix.
        e.push(cmd(Command::ClearTrack, 2, 0, 0.0f));
        e.push(note(2, 127.0f, 0));
        const auto cleared = render(e, 64);
        silent = true;
        for (float v : cleared) silent = silent && v == 0.0f;
        CHECK(silent, "piste supprimée : plus de son");
    }

    if (failures == 0) std::printf("OK\n");
    return failures ? 1 : 0;
}
