// Micro-timing : décalage des notes par rapport au pas, notes multiples (roulements), force de quantification.
// Tout est vérifié à l'échantillon près : une note du pas k de décalage o (en 1/128 de pas) tombe à l'indice
// ceil(k x L + o/128 x L), avec L = 5512,5 échantillons (un pas à 120 BPM).
#include <cmath>
#include <cstdint>
#include <cstdio>
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

static constexpr double L    = 5512.5;   // un pas à 120 BPM
static constexpr int    BAR  = 88200;    // 16 pas

// « Impulsion » : un seul échantillon à 0,5 -> la sortie vaut tanh(MASTER_GAIN x 0,5 x gain).
static const int16_t impulse[] = {16384, 0};
static const int16_t ramp[]    = {0, 8000, 16000, 24000, 32000, 0};

static float expected(float gain) { return std::tanh(cfg::MASTER_GAIN * 0.5f * gain); }
static bool  near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

// Indice attendu d'une note du pas k, de décalage o (1/128 de pas), après quantification q (%).
static int idx(int k, double o, int q = 0) {
    return static_cast<int>(std::ceil(k * L + o * (100 - q) / 100.0 / 128.0 * L));
}

static Command cmd(Command::Type type, int track, int step, float value) {
    Command c;
    c.type  = type;
    c.track = static_cast<uint8_t>(track);
    c.step  = static_cast<uint8_t>(step);
    c.value = value;
    return c;
}

static Command set_sample(int track, const int16_t* data, uint32_t length) {
    Command c = cmd(Command::SetTrackSample, track, 0, 0.0f);
    c.data    = data;
    c.length  = length;
    return c;
}

static Command offset_cmd(int track, int step, int offset) { return cmd(Command::SetStepOffset, track, step, static_cast<float>(offset)); }

static Command extra(int track, int step, int slot, int offset, int velocity, int pitch = 0) {
    Command c = cmd(Command::SetExtraHit, track, step, static_cast<float>(velocity));
    c.slot    = static_cast<uint8_t>(slot);
    c.offset  = static_cast<int8_t>(offset);
    c.pitch   = static_cast<int8_t>(pitch);
    return c;
}

// Rendu par morceaux de tailles irrégulières (les notes ne doivent pas dépendre du découpage).
static std::vector<float> render(Engine& e, int frames, bool chunked = true) {
    std::vector<float> l(static_cast<std::size_t>(frames)), r(static_cast<std::size_t>(frames));
    const int sizes[] = {128, 37, 200, 1, 64, 1000};
    int       k       = 0;
    for (int pos = 0; pos < frames;) {
        int n = chunked ? sizes[k++ % 6] : frames;
        if (frames - pos < n) n = frames - pos;
        e.render(l.data() + pos, r.data() + pos, n);
        pos += n;
    }
    return l;
}

static std::vector<int> hits(const std::vector<float>& out, float threshold = 0.02f) {
    std::vector<int> h;
    for (std::size_t i = 0; i < out.size(); ++i)
        if (out[i] > threshold) h.push_back(static_cast<int>(i));
    return h;
}

// Un moteur avec la piste 2 sur une impulsion, prêt à jouer ; `setup` règle le pattern.
template <typename F>
static std::vector<float> play(int frames, F&& setup, bool chunked = true) {
    Engine e;
    e.push(set_sample(2, impulse, 2));
    setup(e);
    e.push(cmd(Command::Play, 0, 0, 0.0f));
    return render(e, frames, chunked);
}

static bool eq(const std::vector<int>& a, std::initializer_list<int> b) { return a == std::vector<int>(b); }

int main() {
    // ----- Sans décalage : comme avant -----
    {
        const auto out = play(30000, [](Engine& e) {
            e.push(cmd(Command::SetStep, 2, 0, 1.0f));
            e.push(cmd(Command::SetStep, 2, 4, 1.0f));
        });
        CHECK(eq(hits(out), {0, idx(4, 0)}), "décalage 0 : trigs aux indices 0 et 22050");
    }

    // ----- Décalage positif et négatif -----
    {
        auto run = [](int step, int offset, int frames = 30000) {
            return hits(play(frames, [&](Engine& e) {
                e.push(cmd(Command::SetStep, 2, 0, 1.0f));
                e.push(cmd(Command::SetStep, 2, step, 1.0f));
                e.push(offset_cmd(2, step, offset));
            }));
        };
        CHECK(eq(run(4, 32), {0, idx(4, 32)}), "pas 4, +32 (+25 %) : indice 23429");
        CHECK(eq(run(4, -32), {0, idx(4, -32)}), "pas 4, -32 (-25 %) : indice 20672");
        CHECK(eq(run(4, -64), {0, idx(4, -64)}), "pas 4, -64 (-50 %) : le plus en avance");
        CHECK(eq(run(4, 63), {0, idx(4, 63)}), "pas 4, +63 : le plus en retard");
        CHECK(eq(run(1, -1), {0, idx(1, -1)}) && eq(run(1, 1), {0, idx(1, 1)}), "un seul cran (1/128 de pas) de chaque côté");
        CHECK(eq(run(5, -32), {0, idx(5, -32)}) && eq(run(3, 32), {0, idx(3, 32)}), "pas impairs : même formule");
        CHECK(eq(run(15, 32, 88000), {0, idx(15, 32)}) && eq(run(15, -32, 88000), {0, idx(15, -32)}), "dernier pas (15) : même formule, avant et après");
    }

    // ----- Le décalage est borné et arrondi -----
    {
        auto at = [](float value) {
            const auto h = hits(play(30000, [&](Engine& e) {
                e.push(cmd(Command::SetStep, 2, 4, 1.0f));
                e.push(cmd(Command::SetStepOffset, 2, 4, value));
            }));
            return h.empty() ? -1 : h[0];  // -1 : aucune note (le test échoue, sans planter)
        };
        CHECK(at(100.0f) == idx(4, 63) && at(-100.0f) == idx(4, -64), "décalage borné à -64..63");
        CHECK(at(31.6f) == idx(4, 32) && at(-31.6f) == idx(4, -32), "décalage arrondi à l'entier le plus proche");
    }

    // ----- Passage du pas 15 au pas 0 : le pattern est circulaire -----
    {
        const auto out = play(BAR + 2000, [](Engine& e) {
            e.push(cmd(Command::SetStep, 2, 0, 1.0f));
            e.push(offset_cmd(2, 0, -32));
        });
        // Premier tour : pas de pas précédent, la note part à l'instant 0. Tour suivant : avant la frontière.
        CHECK(eq(hits(out), {0, idx(16, -32)}), "pas 0 en avance : à 0 au premier tour, puis 1378 échantillons avant la fin de la mesure");
        CHECK(idx(16, -32) == 86822, "(indice attendu : 86822)");

        // Un pas 0 en retard : une seule fois par tour.
        const auto late = play(BAR + 5000, [](Engine& e) {
            e.push(cmd(Command::SetStep, 2, 0, 1.0f));
            e.push(offset_cmd(2, 0, 32));
        });
        CHECK(eq(hits(late), {idx(0, 32), idx(16, 32)}), "pas 0 en retard : 1379 puis 89579");
    }

    // ----- Démarrage de la lecture en cours de route : le « premier tour » est relatif au démarrage -----
    {
        Engine e;
        e.push(set_sample(2, impulse, 2));
        e.push(cmd(Command::SetStep, 2, 0, 1.0f));
        e.push(offset_cmd(2, 0, -32));
        render(e, 5000);  // lecture à l'arrêt
        e.push(cmd(Command::Play, 0, 0, 0.0f));
        const auto out = render(e, BAR + 2000);
        CHECK(eq(hits(out), {0, idx(16, -32)}), "Play après 5000 échantillons : la note du pas 0 part à l'instant du Play");
    }

    // ----- Roulement : 4 notes dans un pas -----
    {
        const auto out = play(30000, [](Engine& e) {
            e.push(cmd(Command::SetStep, 2, 4, 1.0f));
            e.push(offset_cmd(2, 4, -48));
            e.push(cmd(Command::SetStepVelocity, 2, 4, 127.0f));
            e.push(extra(2, 4, 1, -16, 100));
            e.push(extra(2, 4, 2, 16, 80));
            e.push(extra(2, 4, 3, 48, 60));
        });
        CHECK(eq(hits(out), {idx(4, -48), idx(4, -16), idx(4, 16), idx(4, 48)}), "roulement de 4 notes aux bons indices, dans l'ordre du temps");
        CHECK(out.size() > 30000 - 1 && near(out[static_cast<std::size_t>(idx(4, -48))], expected(127.0f / 127.0f)) && near(out[static_cast<std::size_t>(idx(4, -16))], expected(100.0f / 127.0f)) &&
                  near(out[static_cast<std::size_t>(idx(4, 16))], expected(80.0f / 127.0f)) && near(out[static_cast<std::size_t>(idx(4, 48))], expected(60.0f / 127.0f)),
              "chaque note a sa vélocité");

        // Le même roulement sur le pas 0 : les notes en avance partent au premier tour, puis avant la fin de la mesure.
        const auto first = play(BAR + 4000, [](Engine& e) {
            e.push(cmd(Command::SetStep, 2, 0, 1.0f));
            e.push(offset_cmd(2, 0, -48));
            e.push(extra(2, 0, 1, -16, 100));
            e.push(extra(2, 0, 2, 16, 80));
            e.push(extra(2, 0, 3, 48, 60));
        });
        // Premier tour : les deux notes en avance partent ensemble à 0 (un seul indice), puis les deux en retard.
        CHECK(eq(hits(first), {0, idx(0, 16), idx(0, 48), idx(16, -48), idx(16, -16), idx(16, 16), idx(16, 48)}),
              "roulement sur le pas 0 : au premier tour les notes en avance partent à 0, ensuite avant la fin de la mesure");
    }

    // ----- Les notes supplémentaires : bornes et commandes -----
    {
        auto count = [](Engine& e) { return e.state().triggers[2].load(); };
        Engine e;
        e.push(set_sample(2, impulse, 2));
        e.push(cmd(Command::SetStep, 2, 4, 1.0f));
        e.push(extra(2, 4, 1, 0, 100));
        e.push(extra(2, 4, 0, 10, 100));    // slot 0 : invalide
        e.push(extra(2, 4, 4, 10, 100));    // slot 4 : invalide
        e.push(extra(200, 4, 1, 10, 100));  // piste invalide
        e.push(extra(2, 200, 1, 10, 100));  // pas invalide
        e.push(cmd(Command::Play, 0, 0, 0.0f));
        render(e, 24000);
        CHECK(count(e) == 2, "slots 0 et 4, piste et pas invalides ignorés : 2 notes (principale + slot 1)");

        // Vélocité hors limites ramenée à 1..127 ; décalage hors limites à -64..63.
        const auto out = play(30000, [](Engine& e2) {
            e2.push(cmd(Command::SetStep, 2, 4, 1.0f));
            e2.push(extra(2, 4, 1, 100, 500));
        });
        CHECK(eq(hits(out), {idx(4, 0), idx(4, 63)}) && near(out[static_cast<std::size_t>(idx(4, 63))], expected(1.0f)), "note supplémentaire : vélocité 500 -> 127, décalage 100 -> 63");

        // Pitch borné à ±12 : comme une note de +12.
        auto with_pitch = [](int pitch) {
            Engine e3;
            e3.push(set_sample(2, ramp, 6));
            e3.push(cmd(Command::SetStep, 2, 4, 1.0f));
            e3.push(extra(2, 4, 1, 16, 127, pitch));
            e3.push(cmd(Command::Play, 0, 0, 0.0f));
            return render(e3, 30000);
        };
        CHECK(with_pitch(50) == with_pitch(12) && with_pitch(-50) == with_pitch(-12) && with_pitch(12) != with_pitch(0), "pitch d'une note supplémentaire borné à ±12");

        // ClearExtraHits retire les notes supplémentaires.
        const auto cleared = play(30000, [](Engine& e4) {
            e4.push(cmd(Command::SetStep, 2, 4, 1.0f));
            e4.push(extra(2, 4, 1, 16, 100));
            e4.push(extra(2, 4, 2, 32, 100));
            e4.push(cmd(Command::ClearExtraHits, 2, 4, 0.0f));
        });
        CHECK(eq(hits(cleared), {idx(4, 0)}), "ClearExtraHits : plus que la note principale");
    }

    // ----- Force de quantification par piste -----
    {
        auto run = [](int q, int offset) {
            const auto h = hits(play(30000, [&](Engine& e) {
                e.push(cmd(Command::SetStep, 2, 4, 1.0f));
                e.push(offset_cmd(2, 4, offset));
                e.push(cmd(Command::SetTrackQuantize, 2, 0, static_cast<float>(q)));
            }));
            return h.empty() ? -1 : h[0];
        };
        CHECK(run(0, 32) == idx(4, 32, 0), "force 0 % : décalage complet");
        CHECK(run(50, 32) == idx(4, 32, 50), "force 50 % : moitié du décalage");
        CHECK(run(25, 32) == idx(4, 32, 25), "force 25 %");
        CHECK(run(100, 32) == idx(4, 0) && run(100, -32) == idx(4, 0) && run(100, -64) == idx(4, 0), "force 100 % : pile sur la grille");
        CHECK(run(25, -32) == idx(4, -32, 25), "force 25 % sur un décalage négatif : le signe est conservé");
        CHECK(run(50, -64) == idx(4, -64, 50), "force 50 % sur -50 % : -25 %");
        CHECK(run(150, 32) == idx(4, 0) && run(-20, 32) == idx(4, 32, 0), "force bornée à 0..100");

        // La force est par piste.
        Engine e;
        e.push(set_sample(2, impulse, 2));
        e.push(set_sample(3, impulse, 2));
        for (int t : {2, 3}) {
            e.push(cmd(Command::SetStep, t, 4, 1.0f));
            e.push(offset_cmd(t, 4, 32));
        }
        e.push(cmd(Command::SetTrackQuantize, 3, 0, 100.0f));
        e.push(cmd(Command::Play, 0, 0, 0.0f));
        std::vector<float> a(30000), b(30000), unused(30000);
        // piste 2 et piste 3 sur deux sorties : on joue d'abord tout, puis on compare les compteurs d'indices.
        e.render(a.data(), unused.data(), 30000);
        const auto h = hits(a);
        CHECK(h.size() == 2 && h[0] == idx(4, 0) && h[1] == idx(4, 32), "deux pistes : l'une sur la grille (force 100 %), l'autre décalée");
    }

    // ----- Les notes ne dépendent pas du découpage du rendu -----
    {
        auto setup = [](Engine& e) {
            e.push(set_sample(3, impulse, 2));
            e.push(set_sample(4, impulse, 2));
            for (int s : {0, 3, 4, 9, 14, 15}) e.push(cmd(Command::SetStep, 2, s, 1.0f));
            e.push(offset_cmd(2, 0, -40));
            e.push(offset_cmd(2, 3, 33));
            e.push(offset_cmd(2, 15, -64));
            e.push(extra(2, 4, 1, -20, 90));
            e.push(extra(2, 4, 2, 25, 70));
            e.push(cmd(Command::SetStep, 3, 7, 1.0f));
            e.push(offset_cmd(3, 7, 50));
            e.push(cmd(Command::SetStep, 4, 8, 1.0f));
            e.push(offset_cmd(4, 8, -50));
            e.push(cmd(Command::SetTrackQuantize, 4, 0, 30.0f));
        };
        const auto chunked = play(2 * BAR + 500, setup, true);
        const auto whole   = play(2 * BAR + 500, setup, false);
        CHECK(chunked == whole, "le rendu est identique quel que soit le découpage en blocs");
        CHECK(hits(whole).size() > 14, "(la scène contient bien de nombreuses notes)");
    }

    // ----- Un tour sur l'autre : les notes reviennent aux mêmes instants (à une mesure près) -----
    {
        const auto out = play(3 * BAR, [](Engine& e) {
            for (int s = 1; s < 16; ++s) {
                e.push(cmd(Command::SetStep, 2, s, 1.0f));
                e.push(offset_cmd(2, s, (s * 37) % 128 - 64));  // décalages variés, tous les pas sauf le 0
            }
        });
        const auto h = hits(out);
        std::vector<int> bar1, bar2;
        for (int i : h) {
            if (i >= 2 * BAR && i < 3 * BAR - 5000) bar1.push_back(i - 2 * BAR);
            if (i >= BAR && i < 2 * BAR - 5000) bar2.push_back(i - BAR);
        }
        CHECK(!bar1.empty() && bar1 == bar2, "les mesures 2 et 3 sont identiques, à une mesure d'écart exacte");
        int sorted = 0;
        for (std::size_t i = 1; i < h.size(); ++i) sorted += h[i] > h[i - 1];
        CHECK(sorted == static_cast<int>(h.size()) - 1, "les notes sortent dans l'ordre du temps");
    }

    // ----- Arrêt : les notes déjà programmées ne partent plus -----
    {
        Engine e;
        e.push(set_sample(2, impulse, 2));
        e.push(cmd(Command::SetStep, 2, 4, 1.0f));
        e.push(offset_cmd(2, 4, 32));
        e.push(cmd(Command::Play, 0, 0, 0.0f));
        render(e, 22100);  // la frontière du pas 4 (22050) est passée, la note (23429) est programmée
        e.push(cmd(Command::Stop, 0, 0, 0.0f));
        const auto after = render(e, 3000);
        CHECK(hits(after).empty(), "Stop : la note programmée pour 23429 ne part pas");

        // Notes en avance déjà programmées : Stop avant leur échéance.
        Engine e2;
        e2.push(set_sample(2, impulse, 2));
        e2.push(cmd(Command::SetStep, 2, 4, 1.0f));
        e2.push(offset_cmd(2, 4, -32));
        e2.push(cmd(Command::Play, 0, 0, 0.0f));
        render(e2, 17000);  // frontière du pas 3 passée : la note du pas 4 (20672) est programmée
        e2.push(cmd(Command::Stop, 0, 0, 0.0f));
        CHECK(hits(render(e2, 8000)).empty(), "Stop : la note en avance programmée ne part pas non plus");

        // Un nouveau Play repart de zéro, sans reste de l'ancienne lecture.
        e.push(cmd(Command::Play, 0, 0, 0.0f));
        const auto again = render(e, 30000);
        CHECK(eq(hits(again), {idx(4, 32)}), "après Stop puis Play : la note revient à son instant, une seule fois");
    }

    // ----- Play pendant la lecture : on repart de zéro, sans reste de l'ancienne lecture -----
    {
        Engine e;
        e.push(set_sample(2, impulse, 2));
        e.push(cmd(Command::SetStep, 2, 4, 1.0f));
        e.push(offset_cmd(2, 4, 32));
        e.push(cmd(Command::Play, 0, 0, 0.0f));
        render(e, 22100);  // la note du pas 4 (23429) est programmée
        e.push(cmd(Command::Play, 0, 0, 0.0f));  // redémarrage sans Stop
        const auto out = render(e, 30000);
        CHECK(eq(hits(out), {idx(4, 32)}), "Play pendant la lecture : la note de l'ancienne lecture ne part pas, seule la nouvelle");
    }

    // ----- Changement de tempo : les notes déjà programmées gardent leur instant -----
    {
        Engine e;
        e.push(set_sample(2, impulse, 2));
        e.push(cmd(Command::SetStep, 2, 4, 1.0f));
        e.push(offset_cmd(2, 4, 32));
        e.push(cmd(Command::Play, 0, 0, 0.0f));
        auto out = render(e, 22100);
        e.push(cmd(Command::SetBpm, 0, 0, 240.0f));
        const auto rest = render(e, 4000);
        CHECK(eq(hits(rest), {idx(4, 32) - 22100}), "tempo changé après la frontière : la note programmée part à 23429");
    }

    // ----- Capacité : 16 pistes x 4 notes sur le même pas -----
    {
        Engine e;
        for (int t = 0; t < cfg::MAX_TRACKS; ++t) {
            e.push(set_sample(t, impulse, 2));
            e.push(cmd(Command::SetStep, t, 0, 1.0f));
            e.push(offset_cmd(t, 0, -48));
            e.push(extra(t, 0, 1, -16, 100));
            e.push(extra(t, 0, 2, 16, 80));
            e.push(extra(t, 0, 3, 48, 60));
            if (t % 4 == 3) render(e, 64);  // la file de commandes ne contient que 64 entrées : on la vide
        }
        e.push(cmd(Command::Play, 0, 0, 0.0f));
        render(e, 100000);
        bool all = true;
        for (int t = 0; t < cfg::MAX_TRACKS; ++t) all = all && e.state().triggers[t].load() == 8;
        CHECK(all, "16 pistes x 4 notes : 8 notes par piste sur 100 000 échantillons (4 au premier tour, 4 au suivant), aucune perdue");
    }

    // ----- Pire cas : 128 notes programmées à la même frontière (16 pistes, 4 notes en retard sur le pas 4 et 4 en avance sur le pas 5) -----
    {
        Engine e;
        for (int t = 0; t < cfg::MAX_TRACKS; ++t) {
            e.push(set_sample(t, impulse, 2));
            e.push(cmd(Command::SetStep, t, 4, 1.0f));
            e.push(cmd(Command::SetStep, t, 5, 1.0f));
            e.push(offset_cmd(t, 4, 8));
            e.push(extra(t, 4, 1, 24, 100));
            e.push(extra(t, 4, 2, 40, 100));
            e.push(extra(t, 4, 3, 56, 100));
            e.push(offset_cmd(t, 5, -8));
            e.push(extra(t, 5, 1, -24, 100));
            e.push(extra(t, 5, 2, -40, 100));
            e.push(extra(t, 5, 3, -56, 100));
            if (t % 3 == 2) render(e, 64);  // la file de commandes ne contient que 64 entrées : on la vide
        }
        render(e, 64);
        e.push(cmd(Command::Play, 0, 0, 0.0f));
        render(e, BAR + 6 * 5513 + 3000);
        bool all = true;
        for (int t = 0; t < cfg::MAX_TRACKS; ++t) all = all && e.state().triggers[t].load() == 16;
        CHECK(all, "128 notes programmées d'un coup : les 16 notes de chaque piste sonnent sur deux tours, aucune perdue");
    }

    // ----- Valeurs conservées quand le pas est éteint ; pas de note si le pas est éteint -----
    {
        const auto off = play(30000, [](Engine& e) {
            e.push(cmd(Command::SetStep, 2, 0, 1.0f));
            e.push(offset_cmd(2, 4, 32));
            e.push(extra(2, 4, 1, -20, 100));  // le pas 4 est éteint
        });
        CHECK(eq(hits(off), {0}), "pas éteint : ni la note principale ni les supplémentaires ne jouent");

        const auto back = play(30000, [](Engine& e) {
            e.push(offset_cmd(2, 4, 32));
            e.push(extra(2, 4, 1, -20, 100));
            e.push(cmd(Command::SetStep, 2, 4, 1.0f));
            e.push(cmd(Command::SetStep, 2, 4, 0.0f));
            e.push(cmd(Command::ToggleStep, 2, 4, 0.0f));  // éteint puis rallumé
        });
        CHECK(eq(hits(back), {idx(4, -20), idx(4, 32)}), "pas éteint puis rallumé : retrouve son décalage et sa note supplémentaire");
    }

    // ----- ClearTrack et ClearAll remettent tout à zéro -----
    {
        for (int which = 0; which < 2; ++which) {
            Engine e;
            e.push(set_sample(2, impulse, 2));
            e.push(offset_cmd(2, 4, 32));
            e.push(extra(2, 4, 1, -20, 100));
            e.push(cmd(Command::SetTrackQuantize, 2, 0, 60.0f));
            e.push(which == 0 ? cmd(Command::ClearTrack, 2, 0, 0.0f) : cmd(Command::ClearAll, 0, 0, 0.0f));
            e.push(set_sample(2, impulse, 2));
            e.push(cmd(Command::SetStep, 2, 4, 1.0f));
            e.push(offset_cmd(2, 8, 20));
            e.push(cmd(Command::Play, 0, 0, 0.0f));
            const auto out = render(e, 30000);
            CHECK(eq(hits(out), {idx(4, 0)}), which == 0 ? "ClearTrack : décalages, notes supplémentaires et quantification remis à zéro" : "ClearAll : idem");
        }
    }

    // ----- Autres fonctions inchangées -----
    {
        // AuditionStep joue la note principale du pas, sans décalage et sans toucher au séquenceur.
        Engine e;
        e.push(set_sample(2, impulse, 2));
        e.push(offset_cmd(2, 4, 40));
        e.push(extra(2, 4, 1, 20, 60));
        e.push(cmd(Command::AuditionStep, 2, 4, 0.0f));
        const auto out = render(e, 2000);
        CHECK(eq(hits(out), {0}) && !e.state().playing.load(), "AuditionStep : la note principale tout de suite, séquenceur à l'arrêt");

        // Note joue tout de suite et n'est pas décalée.
        Engine n;
        n.push(set_sample(2, impulse, 2));
        n.push(offset_cmd(2, 4, 40));
        Command note = cmd(Command::Note, 2, 0, 127.0f);
        n.push(note);
        CHECK(eq(hits(render(n, 2000)), {0}), "Note : tout de suite");
    }

    if (failures == 0) std::printf("OK\n");
    return failures ? 1 : 0;
}
