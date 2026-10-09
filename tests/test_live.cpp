// Modifications « live » du moteur : ajout / remplacement / suppression de pistes par commandes.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "../config.h"
#include "../core/engine.h"

static int failures = 0;

#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            std::printf("FAIL: %s\n", msg);         \
            ++failures;                             \
        }                                           \
    } while (0)

static std::vector<float> render(Engine& e, int frames) {
    std::vector<float> l(frames), r(frames);
    e.render(l.data(), r.data(), frames);
    return l;
}

// Indices des échantillons où un trig audible (impulsion) tombe.
static std::vector<int> render_trigs_of(Engine& e, int frames) {
    const auto out = render(e, frames);
    std::vector<int> hits;
    for (int i = 0; i < frames; ++i)
        if (out[static_cast<std::size_t>(i)] > 0.1f) hits.push_back(i);
    return hits;
}

static Command set_sample(uint8_t track, const int16_t* data, uint32_t length) {
    Command c;
    c.type   = Command::SetTrackSample;
    c.track  = track;
    c.data   = data;
    c.length = length;
    return c;
}

static int count_released(Engine& e, const int16_t* expected) {
    int            n = 0;
    const int16_t* p = nullptr;
    while (e.pop_released(p)) {
        if (p == expected) ++n;
        else n += 1000;  // pointeur inattendu
    }
    return n;
}

int main() {
    // Deux « samples » d'un échantillon d'amplitude différente : on les distingue au niveau.
    const int16_t loud[]  = {32767, 0};
    const int16_t quiet[] = {8000, 0};

    // Ajout d'une piste en direct : rien avant la commande, un trig audible après.
    {
        Engine e;
        e.push({Command::SetStep, 3, 0, 1.0f});
        e.push({Command::Play, 0, 0, 0.0f});
        auto out = render(e, 256);
        CHECK(out[0] == 0.0f, "piste sans sample : silence");

        Engine f;
        f.push(set_sample(3, loud, 2));
        f.push({Command::SetStep, 3, 0, 1.0f});
        f.push({Command::Play, 0, 0, 0.0f});
        out = render(f, 256);
        CHECK(out[0] > 0.5f, "piste ajoutée : le sample joue au pas 0");
        CHECK(out[1] == 0.0f, "impulsion d'un seul échantillon");
        CHECK(f.state().triggers[3].load() == 1, "compteur de trigs de la piste");
        CHECK(f.state().triggers[2].load() == 0, "autres pistes à zéro");
    }

    // La piste 0 (kick synthétique par défaut) est remplacée par un sample.
    {
        Engine e;
        e.push(set_sample(0, loud, 2));
        e.push({Command::SetStep, 0, 0, 1.0f});
        e.push({Command::Play, 0, 0, 0.0f});
        auto out = render(e, 2000);
        bool silent_after = true;
        for (int i = 1; i < 2000; ++i) silent_after = silent_after && out[i] == 0.0f;
        CHECK(out[0] > 0.5f && silent_after, "sample sur la piste 0 : plus de kick synthétique");
    }

    // Remplacement : l'ancien buffer est rendu une seule fois, le nouveau joue.
    {
        Engine e;
        e.push(set_sample(1, loud, 2));
        e.push({Command::SetStep, 1, 0, 1.0f});
        render(e, 128);
        CHECK(count_released(e, loud) == 0, "rien à rendre tant que le buffer est utilisé");

        e.push(set_sample(1, quiet, 2));
        e.push({Command::Play, 0, 0, 0.0f});
        auto out = render(e, 128);
        CHECK(count_released(e, loud) == 1, "ancien buffer rendu exactement une fois");
        CHECK(out[0] > 0.1f && out[0] < 0.4f, "le nouveau sample (plus faible) joue");
    }

    // Même pointeur renvoyé : jamais rendu (il est toujours lu).
    {
        Engine e;
        e.push(set_sample(1, loud, 2));
        e.push(set_sample(1, loud, 2));
        render(e, 128);
        CHECK(count_released(e, loud) == 0, "même buffer redonné : pas rendu");
    }

    // Suppression : buffer rendu, pattern vidé, gain remis à 1, piste muette.
    {
        Engine e;
        e.push(set_sample(2, loud, 2));
        e.push({Command::SetTrackGain, 2, 0, 0.3f});
        e.push({Command::SetStep, 2, 0, 1.0f});
        e.push({Command::SetStep, 2, 4, 1.0f});
        e.push({Command::ClearTrack, 2, 0, 0.0f});
        e.push({Command::Play, 0, 0, 0.0f});
        auto out = render(e, 6 * 5513);
        bool silent = true;
        for (float s : out) silent = silent && s == 0.0f;
        CHECK(silent, "piste supprimée : aucun son, pattern vidé");
        CHECK(count_released(e, loud) == 1, "buffer rendu à la suppression");

        // On peut réutiliser l'index : la piste repart de zéro (pattern vide, gain 1).
        e.push(set_sample(2, quiet, 2));
        e.push({Command::Stop, 0, 0, 0.0f});
        e.push({Command::Play, 0, 0, 0.0f});
        out = render(e, 6 * 5513);
        bool silent2 = true;
        for (float s : out) silent2 = silent2 && s == 0.0f;
        CHECK(silent2, "piste recréée : pattern vide, rien ne joue");
        e.push({Command::ToggleStep, 2, 0, 0.0f});
        e.push({Command::Stop, 0, 0, 0.0f});
        e.push({Command::Play, 0, 0, 0.0f});
        out = render(e, 256);
        CHECK(out[0] > 0.1f, "piste recréée : le pas activé joue");
    }

    // Pitch par commande.
    {
        const int16_t ramp[] = {0, 8000, 16000, 24000, 32000, 0};
        Engine e;
        e.push(set_sample(5, ramp, 6));
        e.push({Command::SetTrackPitch, 5, 0, 2.0f});
        e.push({Command::SetStep, 5, 0, 1.0f});
        e.push({Command::Play, 0, 0, 0.0f});
        auto out = render(e, 8);
        // pitch 2.0 : échantillons 0, 2, 4 de la rampe -> 0, 16000, 32000 (avant tanh et gain master)
        CHECK(out[0] == 0.0f && out[1] > out[0] && out[2] > out[1] && out[3] == 0.0f,
              "pitch 2.0 : trois valeurs croissantes puis la fin");
    }

    // SetTrackPattern : les 16 pas d'une piste en un seul message.
    {
        Engine e;
        e.push(set_sample(4, loud, 2));
        Command c;
        c.type   = Command::SetTrackPattern;
        c.track  = 4;
        c.length = 0x8005;  // pas 0, 2 et 15
        e.push(c);
        e.push({Command::Play, 0, 0, 0.0f});
        auto hits = render_trigs_of(e, 17 * 5513);
        CHECK(hits.size() == 4, "pas 0, 2, 15 puis 0 du tour suivant");
        if (hits.size() == 4) {
            CHECK(hits[0] == 0 && hits[1] == static_cast<int>(std::ceil(2 * 5512.5)) &&
                      hits[2] == static_cast<int>(std::ceil(15 * 5512.5)) && hits[3] == static_cast<int>(std::ceil(16 * 5512.5)),
                  "SetTrackPattern : trigs aux bons pas");
        }

        // Un nouveau pattern remplace l'ancien en entier (pas de cumul).
        c.length = 0x0002;
        e.push(c);
        e.push({Command::Stop, 0, 0, 0.0f});
        e.push({Command::Play, 0, 0, 0.0f});
        hits = render_trigs_of(e, 5 * 5513);
        CHECK(hits.size() == 1 && hits[0] == static_cast<int>(std::ceil(1 * 5512.5)), "le nouveau pattern remplace l'ancien");

        c.track = 200;  // piste hors limites : ignorée
        e.push(c);
        render(e, 16);
    }

    // ClearAll : toutes les pistes disparaissent, chaque buffer est rendu une fois.
    {
        Engine e;
        e.push(set_sample(0, loud, 2));
        e.push(set_sample(3, quiet, 2));
        e.push({Command::SetStep, 0, 0, 1.0f});
        e.push({Command::SetStep, 3, 0, 1.0f});
        e.push({Command::SetTrackGain, 3, 0, 0.2f});
        e.push({Command::ClearAll, 0, 0, 0.0f});
        e.push({Command::Play, 0, 0, 0.0f});
        auto out = render(e, 6 * 5513);
        bool silent = true;
        for (float s : out) silent = silent && s == 0.0f;
        CHECK(silent, "ClearAll : plus aucun son");
        int            released = 0;
        const int16_t* p        = nullptr;
        bool           both     = false, other = false;
        int            seen_loud = 0, seen_quiet = 0;
        while (e.pop_released(p)) {
            ++released;
            if (p == loud) ++seen_loud;
            else if (p == quiet) ++seen_quiet;
            else other = true;
        }
        both = seen_loud == 1 && seen_quiet == 1;
        CHECK(released == 2 && both && !other, "ClearAll : les deux buffers rendus une seule fois");

        // Les index sont réutilisables, repartis de zéro (pattern vide, gain 1).
        e.push(set_sample(3, loud, 2));
        e.push({Command::Stop, 0, 0, 0.0f});
        e.push({Command::Play, 0, 0, 0.0f});
        out = render(e, 6 * 5513);
        bool silent2 = true;
        for (float s : out) silent2 = silent2 && s == 0.0f;
        CHECK(silent2, "après ClearAll, une piste recréée est vide");
    }

    // Écoute (preview) : joue hors piste, même séquenceur à l'arrêt ; buffers rendus au remplacement / à l'arrêt.
    {
        Engine e;
        auto   silent = render(e, 256);
        CHECK(silent[0] == 0.0f, "pas d'écoute au départ");

        Command p;
        p.type   = Command::PreviewSample;
        p.data   = loud;
        p.length = 2;
        e.push(p);
        auto out = render(e, 256);
        CHECK(out[0] > 0.5f && out[1] == 0.0f, "l'écoute joue le sample, séquenceur à l'arrêt");
        CHECK(e.state().triggers[0].load() == 0, "l'écoute ne compte pas comme un trig de piste");
        CHECK(count_released(e, loud) == 0, "buffer d'écoute gardé tant qu'il n'est pas remplacé");

        p.data = quiet;
        e.push(p);
        out = render(e, 256);
        CHECK(out[0] > 0.1f && out[0] < 0.4f, "nouvelle écoute : le nouveau sample");
        CHECK(count_released(e, loud) == 1, "ancien buffer d'écoute rendu au remplacement");

        // L'écoute se mélange aux pistes.
        e.push(set_sample(2, loud, 2));
        e.push({Command::SetStep, 2, 0, 1.0f});
        e.push({Command::Play, 0, 0, 0.0f});
        e.push(p);
        out = render(e, 256);
        CHECK(out[0] > 0.6f, "écoute + piste : les deux sont audibles");

        // Arrêt : silence et buffer rendu.
        e.push({Command::StopPreview, 0, 0, 0.0f});
        e.push({Command::Stop, 0, 0, 0.0f});
        out = render(e, 256);
        CHECK(count_released(e, quiet) == 1, "StopPreview rend le buffer d'écoute");
        e.push({Command::StopPreview, 0, 0, 0.0f});
        render(e, 16);
        CHECK(count_released(e, quiet) == 0, "second StopPreview : rien à rendre");
    }

    // Commandes invalides ignorées.
    {
        Engine e;
        e.push(set_sample(200, loud, 2));          // piste hors limites
        e.push(set_sample(1, nullptr, 2));         // pointeur nul
        e.push(set_sample(1, loud, 0));            // longueur nulle
        e.push({Command::ClearTrack, 200, 0, 0.0f});
        e.push({Command::SetStep, 1, 0, 1.0f});
        e.push({Command::Play, 0, 0, 0.0f});
        auto out = render(e, 256);
        CHECK(out[0] == 0.0f, "commandes invalides sans effet");
        CHECK(count_released(e, loud) == 0, "rien rendu pour des commandes invalides");
    }

    if (failures == 0) std::printf("OK\n");
    return failures ? 1 : 0;
}
