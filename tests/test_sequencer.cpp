#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "../config.h"
#include "../core/engine.h"
#include "../core/sample_player.h"

static int failures = 0;

#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            std::printf("FAIL: %s\n", msg);         \
            ++failures;                             \
        }                                           \
    } while (0)

// Rend `frames` échantillons par morceaux de tailles irrégulières et retourne les indices
// où un trig audible (impulsion du SamplePlayer) tombe.
static std::vector<int> render_trigs(Engine& e, int frames) {
    std::vector<float> l(frames), r(frames);
    const int sizes[] = {128, 37, 200, 1, 64};
    int       k = 0;
    for (int pos = 0; pos < frames;) {
        int n = sizes[k++ % 5];
        if (frames - pos < n) n = frames - pos;
        e.render(l.data() + pos, r.data() + pos, n);
        pos += n;
    }
    std::vector<int> hits;
    for (int i = 0; i < frames; ++i)
        if (l[i] > 0.1f) hits.push_back(i);
    return hits;
}

int main() {
    // Voix de test : une impulsion d'un seul échantillon -> l'indice du trig est directement lisible.
    const int16_t impulse[] = {32767, 0};
    SamplePlayer  click;
    click.set_sample(impulse, 2);

    // 120 BPM : un pas = 44100 * 60 / 120 / 4 = 5512,5 échantillons -> trig k à ceil(k * 5512,5).
    {
        Engine e;
        e.set_voice(0, &click);
        e.push({Command::SetBpm, 0, 0, 120.0f});
        for (int s = 0; s < cfg::MAX_STEPS; ++s) e.push({Command::SetStep, 0, static_cast<uint8_t>(s), 1.0f});
        e.push({Command::Play, 0, 0, 0.0f});

        const int frames = 20 * 5513;
        const auto hits  = render_trigs(e, frames);

        bool ok = hits.size() >= 19;
        for (std::size_t k = 0; k < hits.size(); ++k)
            ok = ok && hits[k] == static_cast<int>(std::ceil(k * 5512.5));
        CHECK(ok, "trigs à ceil(k * 5512,5), sans dérive, malgré des blocs irréguliers");
        CHECK(e.state().playing.load(), "état : playing");
        CHECK(e.state().step.load() >= 0, "état : pas courant mis à jour");
    }

    // Seuls les pas actifs jouent ; la grille boucle sur 16 pas.
    {
        Engine e;
        e.set_voice(0, &click);
        e.push({Command::SetStep, 0, 0, 1.0f});
        e.push({Command::SetStep, 0, 4, 1.0f});
        e.push({Command::Play, 0, 0, 0.0f});

        const auto hits = render_trigs(e, 17 * 5513);
        CHECK(hits.size() == 3, "pas 0, 4 puis 0 du tour suivant");
        if (hits.size() == 3) {
            CHECK(hits[0] == 0, "premier trig à l'échantillon 0");
            CHECK(hits[1] == static_cast<int>(std::ceil(4 * 5512.5)), "pas 4");
            CHECK(hits[2] == static_cast<int>(std::ceil(16 * 5512.5)), "retour au pas 0 après 16 pas");
        }
    }

    // Stop, ToggleStep et changement de BPM.
    {
        Engine e;
        e.set_voice(0, &click);
        e.push({Command::ToggleStep, 0, 0, 0.0f});
        e.push({Command::ToggleStep, 0, 1, 0.0f});
        e.push({Command::ToggleStep, 0, 1, 0.0f});  // 2e toggle : pas 1 de nouveau éteint
        e.push({Command::SetBpm, 0, 0, 240.0f});     // un pas = 2756,25 échantillons
        e.push({Command::Play, 0, 0, 0.0f});
        auto hits = render_trigs(e, 3 * 2757);
        CHECK(hits.size() == 1 && hits[0] == 0, "toggle x2 = éteint, seul le pas 0 joue");

        e.push({Command::Stop, 0, 0, 0.0f});
        hits = render_trigs(e, 16 * 3000);
        CHECK(hits.empty(), "plus aucun trig après Stop");
        CHECK(!e.state().playing.load(), "état : arrêté");
    }

    // La file refuse quand elle est pleine, sans bloquer.
    {
        Engine e;
        int    accepted = 0;
        for (int i = 0; i < 200; ++i) accepted += e.push({Command::SetBpm, 0, 0, 100.0f}) ? 1 : 0;
        CHECK(accepted == 64, "file pleine : 64 commandes acceptées puis refus");
    }

    if (failures == 0) std::printf("OK\n");
    return failures ? 1 : 0;
}
