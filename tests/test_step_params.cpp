// Vélocité et pitch par pas : séquenceur, moteur, lecteur de samples.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#include "../config.h"
#include "../core/dsp_utils.h"
#include "../core/engine.h"
#include "../core/sample_player.h"
#include "../core/synth_kick.h"

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
// Rampe : permet de lire la vitesse de lecture dans les valeurs rendues.
static const int16_t ramp[] = {0, 8000, 16000, 24000, 32000, 0};

static float expected(float gain) { return std::tanh(cfg::MASTER_GAIN * 0.5f * gain); }

static Command cmd(Command::Type type, uint8_t track, uint8_t step, float value) {
    Command c;
    c.type  = type;
    c.track = track;
    c.step  = step;
    c.value = value;
    return c;
}

static Command set_sample(uint8_t track, const int16_t* data, uint32_t length) {
    Command c;
    c.type   = Command::SetTrackSample;
    c.track  = track;
    c.data   = data;
    c.length = length;
    return c;
}

static std::vector<float> render(Engine& e, int frames) {
    std::vector<float> l(static_cast<std::size_t>(frames)), r(static_cast<std::size_t>(frames));
    e.render(l.data(), r.data(), frames);
    return l;
}

// Une piste 2 avec `sample`, pas 0 joué ; `setup` règle le reste ; rend 64 échantillons de la lecture.
static std::vector<float> play_step0(const int16_t* sample, uint32_t length, const std::function<void(Engine&)>& setup) {
    Engine e;
    e.push(set_sample(2, sample, length));
    e.push(cmd(Command::SetStep, 2, 0, 1.0f));
    setup(e);
    e.push(cmd(Command::Play, 0, 0, 0.0f));
    return render(e, 64);
}

// Même chose avec AuditionStep (sans séquenceur) sur un pas donné.
static std::vector<float> audition(const int16_t* sample, uint32_t length, int step, const std::function<void(Engine&)>& setup) {
    Engine e;
    e.push(set_sample(2, sample, length));
    setup(e);
    e.push(cmd(Command::AuditionStep, 2, static_cast<uint8_t>(step), 0.0f));
    return render(e, 64);
}

int main() {
    // ----- Vélocité : le volume suit vélocité / 127 -----
    {
        for (int v : {127, 100, 64, 1}) {
            const auto out = play_step0(impulse, 2, [&](Engine& e) { e.push(cmd(Command::SetStepVelocity, 2, 0, static_cast<float>(v))); });
            CHECK(near(out[0], expected(static_cast<float>(v) / 127.0f)), "vélocité : niveau = tanh(master x 0,5 x v/127)");
        }
        const auto def = play_step0(impulse, 2, [](Engine&) {});
        CHECK(near(def[0], expected(1.0f)), "vélocité par défaut = 127 : volume plein, comme avant");

        // Le gain de la piste se multiplie avec la vélocité.
        const auto both = play_step0(impulse, 2, [](Engine& e) {
            e.push(cmd(Command::SetTrackGain, 2, 0, 0.5f));
            e.push(cmd(Command::SetStepVelocity, 2, 0, 64.0f));
        });
        CHECK(near(both[0], expected(0.5f * 64.0f / 127.0f)), "gain de la piste x vélocité");
    }

    // ----- Vélocité : arrondi et bornes -----
    {
        auto level = [](float value) {
            return audition(impulse, 2, 0, [&](Engine& e) { e.push(cmd(Command::SetStepVelocity, 2, 0, value)); })[0];
        };
        CHECK(near(level(90.6f), expected(91.0f / 127.0f)), "arrondi à l'entier le plus proche");
        CHECK(near(level(0.0f), expected(1.0f / 127.0f)), "0 borné à 1 (jamais muet)");
        CHECK(near(level(-40.0f), expected(1.0f / 127.0f)), "négatif borné à 1");
        CHECK(near(level(500.0f), expected(1.0f)), "au-delà de 127 borné à 127");
    }

    // ----- Pitch : même résultat qu'un pitch de piste équivalent -----
    {
        auto with_step = [](float semitones) {
            return audition(ramp, 6, 0, [&](Engine& e) { e.push(cmd(Command::SetStepPitch, 2, 0, semitones)); });
        };
        auto with_track = [](float ratio) {
            return audition(ramp, 6, 0, [&](Engine& e) { e.push(cmd(Command::SetTrackPitch, 2, 0, ratio)); });
        };
        const auto base = audition(ramp, 6, 0, [](Engine&) {});

        CHECK(with_step(0.0f) == base, "pitch 0 : rien ne change");
        CHECK(with_step(12.0f) == with_track(2.0f), "+12 demi-tons = une octave plus haut (vitesse x2)");
        CHECK(with_step(-12.0f) == with_track(0.5f), "-12 demi-tons = une octave plus bas (vitesse x0,5)");
        CHECK(with_step(7.0f) == with_track(dsp::semitone_ratio(7)), "+7 demi-tons : quinte");
        CHECK(with_step(12.0f) != base && with_step(-12.0f) != base, "le pitch change bien le son");
        CHECK(with_step(50.0f) == with_step(12.0f), "au-delà de +12 borné à +12");
        CHECK(with_step(-50.0f) == with_step(-12.0f), "en-deçà de -12 borné à -12");

        // +12 : un échantillon sur deux de la rampe (0, 16000, 32000) -> sortie croissante puis fin.
        const auto up = with_step(12.0f);
        CHECK(up[0] == 0.0f && up[1] > up[0] && up[2] > up[1] && up[3] == 0.0f, "+12 : un échantillon sur deux, trois valeurs");

        // Le pitch de la piste et celui du pas se multiplient (bornés à 0,05 - 8).
        const auto cancel = audition(ramp, 6, 0, [](Engine& e) {
            e.push(cmd(Command::SetTrackPitch, 2, 0, 0.5f));
            e.push(cmd(Command::SetStepPitch, 2, 0, 12.0f));
        });
        CHECK(cancel == base, "piste x0,5 et pas +12 : vitesse normale");
        const auto clamped = audition(ramp, 6, 0, [](Engine& e) {
            e.push(cmd(Command::SetTrackPitch, 2, 0, 8.0f));
            e.push(cmd(Command::SetStepPitch, 2, 0, 12.0f));
        });
        CHECK(clamped == with_track(8.0f), "le résultat est borné à x8");
    }

    // ----- Les valeurs sont conservées quand le pas est éteint, puis rallumé -----
    {
        const auto before = play_step0(ramp, 6, [](Engine& e) {
            e.push(cmd(Command::SetStepVelocity, 2, 0, 50.0f));
            e.push(cmd(Command::SetStepPitch, 2, 0, 5.0f));
        });
        const auto after = play_step0(ramp, 6, [](Engine& e) {
            e.push(cmd(Command::SetStepVelocity, 2, 0, 50.0f));
            e.push(cmd(Command::SetStepPitch, 2, 0, 5.0f));
            e.push(cmd(Command::SetStep, 2, 0, 0.0f));  // éteint
            e.push(cmd(Command::ToggleStep, 2, 0, 0.0f));  // rallumé
            e.push(cmd(Command::SetTrackPattern, 2, 0, 0.0f));  // (pattern vide : n'efface pas les valeurs)
            e.push(cmd(Command::SetStep, 2, 0, 1.0f));
        });
        CHECK(before == after, "vélocité et pitch conservés quand le pas est éteint puis rallumé");
        CHECK(before != play_step0(ramp, 6, [](Engine&) {}), "(les valeurs ont bien un effet)");
    }

    // ----- ClearTrack et ClearAll remettent les valeurs par défaut -----
    {
        for (int which = 0; which < 2; ++which) {
            Engine e;
            e.push(set_sample(2, ramp, 6));
            e.push(cmd(Command::SetStepVelocity, 2, 0, 30.0f));
            e.push(cmd(Command::SetStepPitch, 2, 0, -7.0f));
            e.push(cmd(Command::SetStep, 2, 0, 1.0f));
            e.push(which == 0 ? cmd(Command::ClearTrack, 2, 0, 0.0f) : cmd(Command::ClearAll, 0, 0, 0.0f));
            e.push(set_sample(2, ramp, 6));
            e.push(cmd(Command::SetStep, 2, 0, 1.0f));
            e.push(cmd(Command::Play, 0, 0, 0.0f));
            const auto out = render(e, 64);
            CHECK(out == play_step0(ramp, 6, [](Engine&) {}), which == 0 ? "ClearTrack : valeurs par défaut" : "ClearAll : valeurs par défaut");
        }
    }

    // ----- AuditionStep : joue la note sans toucher au séquenceur -----
    {
        Engine e;
        e.push(set_sample(2, impulse, 2));
        e.push(cmd(Command::SetStepVelocity, 2, 4, 64.0f));
        e.push(cmd(Command::AuditionStep, 2, 4, 0.0f));  // le pas 4 n'est pas joué : l'écoute marche quand même
        const auto out = render(e, 64);
        CHECK(near(out[0], expected(64.0f / 127.0f)), "écoute d'un pas éteint, avec sa vélocité");
        CHECK(!e.state().playing.load() && e.state().step.load() == -1, "le séquenceur n'a pas bougé");
        CHECK(e.state().triggers[2].load() == 1, "compté comme un déclenchement de la piste");

        e.push(cmd(Command::AuditionStep, 2, 4, 0.0f));
        e.push(cmd(Command::AuditionStep, 2, 4, 0.0f));
        const auto twice = render(e, 64);
        CHECK(near(twice[0], expected(64.0f / 127.0f)), "écoutes rapprochées : la note repart (pas de cumul)");

        // Piste sans voix, piste ou pas invalides : ignorés sans effet.
        e.push(cmd(Command::AuditionStep, 9, 0, 0.0f));
        e.push(cmd(Command::AuditionStep, 200, 0, 0.0f));
        e.push(cmd(Command::AuditionStep, 2, 200, 0.0f));
        const auto none = render(e, 64);
        bool silent = true;
        for (float v : none) silent = silent && v == 0.0f;
        CHECK(silent, "commandes invalides : silence, sans plantage");
    }

    // ----- Séquenceur : chaque pas a sa vélocité, trigs toujours à l'échantillon près -----
    {
        Engine e;
        e.push(set_sample(0, impulse, 2));
        e.push(set_sample(1, impulse, 2));
        for (int t = 0; t < 2; ++t) {
            e.push(cmd(Command::SetStep, static_cast<uint8_t>(t), 0, 1.0f));
            e.push(cmd(Command::SetStep, static_cast<uint8_t>(t), 4, 1.0f));
        }
        e.push(cmd(Command::SetStepVelocity, 0, 4, 64.0f));  // piste 0, pas 4 : plus faible
        e.push(cmd(Command::SetStepVelocity, 1, 0, 32.0f));  // piste 1, pas 0 : plus faible
        e.push(cmd(Command::Play, 0, 0, 0.0f));
        const auto out = render(e, 22100);

        // 120 BPM : le pas 4 tombe à ceil(4 x 5512,5) = 22050.
        CHECK(near(out[0], expected(1.0f + 32.0f / 127.0f)), "pas 0 : piste 0 pleine + piste 1 à 32");
        CHECK(near(out[22050], expected(64.0f / 127.0f + 1.0f)), "pas 4 : piste 0 à 64 + piste 1 pleine, à l'échantillon 22050");
        CHECK(out[22049] == 0.0f, "rien juste avant");
    }

    // ----- Le pitch d'un pas ne change pas la tenue du rythme -----
    {
        Engine e;
        e.push(set_sample(0, ramp, 6));
        e.push(cmd(Command::SetStep, 0, 0, 1.0f));
        e.push(cmd(Command::SetStep, 0, 1, 1.0f));
        e.push(cmd(Command::SetStepPitch, 0, 1, 12.0f));
        e.push(cmd(Command::Play, 0, 0, 0.0f));
        const auto out = render(e, 12000);
        const int  second = static_cast<int>(std::ceil(5512.5));
        // Pas 0 : vitesse normale (0, 8000, 16000...). Pas 1 : vitesse x2 (0, 16000, 32000, puis fin).
        CHECK(out[1] > 0.0f && out[second] == 0.0f && out[second + 1] > 0.0f && out[second + 2] > out[second + 1] &&
                  out[second + 3] == 0.0f,
              "le pas 1 démarre à l'échantillon 5513 et se lit à vitesse x2");
    }

    // ----- SamplePlayer : note avec son rapport de pitch -----
    {
        SamplePlayer p;
        p.set_sample(ramp, 6);
        float out[8] = {};

        p.trigger_pitched(1.0f, 2.0f);
        p.render(out, 8);
        CHECK(near(out[0], 0.0f) && near(out[1], 16000.0f / 32768.0f) && near(out[2], 32000.0f / 32768.0f) && !p.active(),
              "trigger_pitched(x2) : un échantillon sur deux");

        for (float& v : out) v = 0.0f;
        p.set_pitch(0.5f);
        p.trigger_pitched(1.0f, 2.0f);  // piste x0,5, note x2 : vitesse normale
        p.render(out, 8);
        CHECK(near(out[1], 8000.0f / 32768.0f) && near(out[4], 32000.0f / 32768.0f), "pitch de la piste x pitch de la note");

        for (float& v : out) v = 0.0f;
        p.trigger(1.0f);  // un déclenchement ordinaire oublie le pitch de la note précédente
        p.render(out, 8);
        CHECK(near(out[1], 4000.0f / 32768.0f), "trigger() : seul le pitch de la piste (x0,5)");

        // Le pitch de la piste peut changer pendant la note : le nouveau rapport s'applique tout de suite.
        static int16_t long_ramp[20];
        for (int i = 0; i < 20; ++i) long_ramp[i] = static_cast<int16_t>(i * 1000);
        SamplePlayer q;
        q.set_sample(long_ramp, 20);
        float seq[3] = {};
        q.trigger_pitched(1.0f, 2.0f);
        q.render(seq, 1);       // échantillon 0, puis on avance de 2
        q.set_pitch(2.0f);      // piste x2 : 2 x 2 = x4 à partir de maintenant
        q.render(seq + 1, 1);   // échantillon 2, puis on avance de 4
        q.render(seq + 2, 1);   // échantillon 6 (et non 4 : la vitesse a bien changé)
        CHECK(near(seq[1], 2000.0f / 32768.0f) && near(seq[2], 6000.0f / 32768.0f), "changement de pitch de la piste pendant la note");

        // Voix sans pitch (synthé en sommeil) : le pitch de la note est ignoré, le son est le même.
        SynthKick plain(static_cast<float>(cfg::SAMPLE_RATE)), pitched(static_cast<float>(cfg::SAMPLE_RATE));
        float     a[64] = {}, b[64] = {};
        plain.trigger(1.0f);
        pitched.trigger_pitched(1.0f, 2.0f);
        plain.render(a, 64);
        pitched.render(b, 64);
        CHECK(std::memcmp(a, b, sizeof a) == 0, "voix sans pitch : le pitch de la note est ignoré");
    }

    // ----- Constantes -----
    CHECK(cfg::VELOCITY_MAX == 127 && cfg::VELOCITY_MIN == 1 && cfg::STEP_PITCH_MIN == -12 && cfg::STEP_PITCH_MAX == 12, "bornes");
    CHECK(near(dsp::semitone_ratio(12), 2.0f) && near(dsp::semitone_ratio(-12), 0.5f) && near(dsp::semitone_ratio(0), 1.0f), "rapports de demi-tons");

    if (failures == 0) std::printf("OK\n");
    return failures ? 1 : 0;
}
