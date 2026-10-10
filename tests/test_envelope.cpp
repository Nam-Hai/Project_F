// Enveloppe ADSR par piste : niveaux de chaque étage, Release à la place de la coupure nette, queues, commandes.
#include <algorithm>
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

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

// 1 ms = 44,1 échantillons : on choisit des temps qui donnent des nombres ronds d'échantillons.
static constexpr int   MS10 = 441;     // 10 ms
static constexpr float HALF = 0.5f;    // l'échantillon tenu vaut 0,5

static int16_t held[40000];       // 0,5 tenu
static int16_t burst[88200];      // sinus de 50 Hz à décroissance lente, qui démarre à 0 (comme un 808)

static std::vector<float> play(SamplePlayer& p, int n) {
    std::vector<float> out(static_cast<std::size_t>(n), 0.0f);
    p.render(out.data(), n);
    return out;
}

static float max_jump(const std::vector<float>& v, std::size_t from, std::size_t to) {
    float m = 0.0f;
    for (std::size_t i = from; i + 1 < to && i + 1 < v.size(); ++i) m = std::max(m, std::fabs(v[i + 1] - v[i]));
    return m;
}

int main() {
    for (int16_t& v : held) v = 16384;
    for (int i = 0; i < 88200; ++i) {
        const double t = i / 44100.0;
        burst[i]       = static_cast<int16_t>(30000.0 * std::sin(2.0 * 3.14159265358979 * 50.0 * t) * std::exp(-t / 0.8));
    }

    // ----- Par défaut : le son d'une note seule est inchangé -----
    {
        SamplePlayer p;
        p.set_sample(held, 40000);
        p.trigger(1.0f);
        auto out = play(p, 2000);
        bool flat = true;
        for (float v : out) flat = flat && v == HALF;
        CHECK(flat, "enveloppe par défaut : 0,5 constant, à l'échantillon près");
    }

    // ----- Attack : de 0 à 1 en linéaire -----
    {
        SamplePlayer p;
        p.set_sample(held, 40000);
        p.set_envelope(10, 0, 100, 5);
        p.trigger(1.0f);
        auto out = play(p, 1000);
        CHECK(out[0] == 0.0f, "attack : la note démarre à 0");
        CHECK(near(out[100], HALF * 100.0f / MS10) && near(out[220], HALF * 220.0f / MS10) && near(out[MS10 - 1], HALF * (MS10 - 1) / MS10),
              "attack : rampe linéaire, 0,5 x k / 441");
        CHECK(near(out[MS10 + 1], HALF) && near(out[999], HALF), "après l'attack : plein niveau");
        bool mono = true;
        for (int k = 1; k < MS10; ++k) mono = mono && out[static_cast<std::size_t>(k)] >= out[static_cast<std::size_t>(k - 1)];
        CHECK(mono, "attack : croissant");
    }

    // ----- Decay et Sustain -----
    {
        SamplePlayer p;
        p.set_sample(held, 40000);
        p.set_envelope(0, 10, 50, 5);
        p.trigger(1.0f);
        auto out = play(p, 1500);
        CHECK(out[0] == HALF, "attack 0 : plein niveau tout de suite");
        CHECK(near(out[220], HALF * (1.0f - 0.5f * 220.0f / MS10)) && near(out[MS10 - 1], HALF * (1.0f - 0.5f * (MS10 - 1) / MS10)), "decay : de 1 à 0,5 en 441 échantillons");
        CHECK(near(out[MS10 + 2], HALF * 0.5f) && near(out[1499], HALF * 0.5f), "sustain : 0,5 tenu jusqu'à la fin du sample");

        SamplePlayer z;  // sustain 0 : la note s'éteint après le decay
        z.set_sample(held, 40000);
        z.set_envelope(0, 10, 0, 5);
        z.trigger(1.0f);
        auto oz = play(z, 1500);
        CHECK(near(oz[MS10 + 2], 0.0f, 1e-6f) && near(oz[1400], 0.0f, 1e-6f) && oz[10] > 0.4f, "sustain 0 : silence après le decay");

        SamplePlayer q;  // sustain 100 % : le decay ne change rien
        q.set_sample(held, 40000);
        q.set_envelope(0, 500, 100, 5);
        q.trigger(1.0f);
        auto oq = play(q, 1000);
        bool flat = true;
        for (float v : oq) flat = flat && v == HALF;
        CHECK(flat, "sustain 100 % : le decay est sans effet");
    }
    {
        // Attack puis decay puis sustain, les trois enchaînés : A 10 ms, D 10 ms, S 40 %.
        SamplePlayer p;
        p.set_sample(held, 40000);
        p.set_envelope(10, 10, 40, 5);
        p.trigger(1.0f);
        auto out = play(p, 1500);
        CHECK(near(out[MS10 - 1], HALF * (MS10 - 1) / MS10), "A puis D : fin de l'attack");
        CHECK(near(out[MS10], HALF) || near(out[MS10], HALF * (1.0f - 0.6f / MS10)), "A puis D : sommet au changement d'étage");
        CHECK(near(out[2 * MS10 + 5], HALF * 0.4f) && near(out[1400], HALF * 0.4f), "A puis D : puis sustain à 40 %");
        // La vélocité multiplie l'enveloppe.
        SamplePlayer v;
        v.set_sample(held, 40000);
        v.set_envelope(10, 0, 100, 5);
        v.trigger(0.5f);
        auto ov = play(v, 600);
        CHECK(near(ov[220], 0.5f * HALF * 220.0f / MS10) && near(ov[500], 0.25f), "la vélocité multiplie l'enveloppe");
    }

    // ----- Release : la note remplacée finit en douceur -----
    {
        // Sample qui démarre à 0 : à la relance, l'ancienne note descend pendant que la nouvelle démarre, sans saut.
        auto retrigger = [&](int release_ms, int attack_ms) {
            SamplePlayer p;
            p.set_sample(burst, 88200);
            p.set_envelope(attack_ms, 0, 100, release_ms);
            p.trigger(1.0f);
            std::vector<float> out = play(p, 5513);  // jusqu'à un sommet du sinus (6,25 périodes)
            p.trigger(1.0f);
            std::vector<float> after = play(p, 2000);
            out.insert(out.end(), after.begin(), after.end());
            return out;
        };
        const auto cut  = retrigger(0, 0);
        const auto soft = retrigger(5, 0);
        const float jump_cut  = max_jump(cut, 5500, 5530);
        const float jump_soft = max_jump(soft, 5500, 5530);
        CHECK(jump_cut > 0.3f, "(Release 0 : saut net de l'amplitude de l'ancienne note)");
        CHECK(jump_soft < 0.03f, "Release 5 ms : plus de saut à la relance (la courbe reste lisse)");

        // Après le Release (220,5 échantillons), seule la nouvelle note reste : identique à une note seule.
        SamplePlayer fresh;
        fresh.set_sample(burst, 88200);
        fresh.trigger(1.0f);
        const auto alone = play(fresh, 2000);
        bool tail_gone = true;
        for (int k = 225; k < 2000; ++k) tail_gone = tail_gone && soft[static_cast<std::size_t>(5513 + k)] == alone[static_cast<std::size_t>(k)];
        CHECK(tail_gone, "après le Release : exactement la nouvelle note seule");

        // Pendant le Release : nouvelle note + ancienne x (1 - k / 220,5).
        SamplePlayer old_only;
        old_only.set_sample(burst, 88200);
        old_only.trigger(1.0f);
        const auto cont = play(old_only, 5513 + 400);
        bool shape = true;
        for (int k = 0; k < 200; k += 17) {
            const float expected = alone[static_cast<std::size_t>(k)] + cont[static_cast<std::size_t>(5513 + k)] * (1.0f - static_cast<float>(k) / 220.5f);
            shape = shape && near(soft[static_cast<std::size_t>(5513 + k)], expected, 2e-4f);
        }
        CHECK(shape, "pendant le Release : nouvelle note + ancienne note x (1 - k / 220,5)");

        // Attack lent + Release : la nouvelle note monte pendant que l'ancienne descend (pas de saut non plus).
        const auto both = retrigger(10, 10);
        CHECK(max_jump(both, 5500, 6100) < 0.03f, "Attack 10 ms et Release 10 ms : lisse");

        // Release 0 : l'ancienne note est coupée, rien ne reste.
        bool cut_exact = true;
        for (int k = 0; k < 2000; ++k) cut_exact = cut_exact && cut[static_cast<std::size_t>(5513 + k)] == alone[static_cast<std::size_t>(k)];
        CHECK(cut_exact, "Release 0 : la nouvelle note seule, comme avant l'enveloppe");
    }
    {
        // Relance avant le moindre échantillon produit : pas de queue (pas de double volume).
        SamplePlayer p;
        p.set_sample(held, 40000);
        p.trigger(1.0f);
        p.trigger(1.0f);
        auto out = play(p, 50);
        CHECK(out[0] == HALF && out[10] == HALF, "deux notes au même instant : une seule se fait entendre");
    }

    // ----- Plusieurs queues, vol de queue, fin de lecture -----
    {
        SamplePlayer p;
        p.set_sample(held, 40000);
        p.set_envelope(0, 0, 100, 100);  // Release de 100 ms : les relances le dépassent
        p.trigger(1.0f);
        for (int i = 0; i < 8; ++i) {  // 8 relances de 50 échantillons : plus que de queues
            play(p, 50);
            p.trigger(1.0f);
        }
        auto out = play(p, 100);
        bool finite = true;
        for (float v : out) finite = finite && std::isfinite(v) && v >= 0.0f && v <= 5.0f * HALF + 1e-4f;
        CHECK(finite, "relances plus rapides que le Release : tout reste borné (au plus 1 note + 4 queues)");
        CHECK(p.active(), "des queues sonnent encore");
        play(p, 10000);
        CHECK(p.active(), "(l'échantillon tenu est long : la lecture continue)");

        SamplePlayer q;  // un sample court : tout finit de lui-même
        static const int16_t shortbuf[] = {16384, 16384, 16384, 16384};
        q.set_sample(shortbuf, 4);
        q.set_envelope(0, 0, 100, 5);
        q.trigger(1.0f);
        play(q, 2);
        q.trigger(1.0f);
        CHECK(q.active(), "note et queue en cours");
        play(q, 50);
        CHECK(!q.active(), "samples terminés : plus rien ne sonne");
    }
    {
        // Changer de sample coupe aussi les queues (elles liraient l'ancien buffer).
        SamplePlayer p;
        p.set_sample(held, 40000);
        p.set_envelope(0, 0, 100, 100);
        p.trigger(1.0f);
        play(p, 100);
        p.trigger(1.0f);
        play(p, 10);
        static const int16_t other[] = {1000, 1000};
        p.set_sample(other, 2);
        CHECK(!p.active(), "set_sample : la note et les queues sont arrêtées");
        auto out = play(p, 100);
        bool silent = true;
        for (float v : out) silent = silent && v == 0.0f;
        CHECK(silent, "…et plus rien ne sort");
    }
    {
        // Une queue garde le pitch de sa note ; la nouvelle note prend celui de la piste.
        SamplePlayer p;
        p.set_sample(burst, 88200);
        p.set_envelope(0, 0, 100, 10);
        p.trigger(1.0f);
        play(p, 1000);
        p.trigger_pitched(1.0f, 1.0f);
        p.set_pitch(2.0f);  // la note en cours passe à x2, la queue reste à x1
        auto out = play(p, 100);

        SamplePlayer tail_ref;  // la même ancienne note, jouée seule au pitch d'origine
        tail_ref.set_sample(burst, 88200);
        tail_ref.trigger(1.0f);
        auto cont = play(tail_ref, 1100);
        SamplePlayer head_ref;
        head_ref.set_sample(burst, 88200);
        head_ref.set_pitch(2.0f);
        head_ref.trigger(1.0f);
        auto h2 = play(head_ref, 100);
        bool ok = true;
        for (int k = 0; k < 100; k += 9) ok = ok && near(out[static_cast<std::size_t>(k)], h2[static_cast<std::size_t>(k)] + cont[static_cast<std::size_t>(1000 + k)] * (1.0f - k / 441.0f), 3e-4f);
        CHECK(ok, "la queue garde son pitch, la note en cours suit celui de la piste");
    }
    {
        // Valeurs hors bornes : bornées.
        SamplePlayer p;
        p.set_sample(held, 40000);
        p.set_envelope(-5, 99999, 150, -3);  // A 0, D 2000 ms, S 100 %, R 0
        p.trigger(1.0f);
        auto out = play(p, 600);
        bool flat = true;
        for (float v : out) flat = flat && v == HALF;
        CHECK(flat, "attack négatif = 0, sustain borné à 100 % : plein niveau constant");
        p.trigger(1.0f);
        auto cut = play(p, 5);
        CHECK(cut[0] == HALF && cut[4] == HALF, "release négatif = 0 : coupure nette (pas de queue)");
    }

    // ----- Cas particuliers du lecteur -----
    {
        // Release mesuré sur le niveau atteint : une note à 50 % descend de 0,5 à 0 en 10 ms (pas en 5).
        SamplePlayer p;
        p.set_sample(held, 40000);
        p.set_envelope(0, 0, 50, 10);
        p.trigger(1.0f);
        play(p, 1000);
        p.trigger(1.0f);
        auto out = play(p, 600);
        // Nouvelle note à 50 % + ancienne (0,5 x 0,5) x (1 - k / 441).
        CHECK(near(out[0], HALF * 0.5f + HALF * 0.5f) && near(out[220], HALF * 0.5f + HALF * 0.5f * (1.0f - 220.0f / MS10)) &&
                  near(out[330], HALF * 0.5f + HALF * 0.5f * (1.0f - 330.0f / MS10)) && near(out[500], HALF * 0.5f),
              "Release : du niveau atteint (50 %) à 0 en 10 ms, linéaire");

        // Attack remis à 0 en cours de montée : la note finit sa montée tout de suite (pas de blocage).
        SamplePlayer q;
        q.set_sample(held, 40000);
        q.set_envelope(100, 0, 100, 5);
        q.trigger(1.0f);
        play(q, 100);
        q.set_envelope(0, 0, 100, 5);
        auto oq = play(q, 10);
        CHECK(near(oq[3], HALF) && near(oq[9], HALF), "attack ramené à 0 pendant la montée : plein niveau tout de suite");

        // Sustain hors bornes (appel direct) : borné.
        SamplePlayer n;
        n.set_sample(held, 40000);
        n.set_envelope(0, 1, -20, 5);
        n.trigger(1.0f);
        auto on = play(n, 200);
        bool non_negative = true;
        for (float v : on) non_negative = non_negative && v >= 0.0f;
        CHECK(non_negative && near(on[199], 0.0f, 1e-6f), "sustain négatif : borné à 0 %, jamais de niveau négatif");
        SamplePlayer h;
        h.set_sample(held, 40000);
        h.set_envelope(0, 1, 150, 5);
        h.trigger(1.0f);
        auto oh = play(h, 200);
        CHECK(near(oh[199], HALF), "sustain 150 % : borné à 100 %");
    }
    {
        // Release borné à 2000 ms (88 200 échantillons) : une queue ne dure pas plus.
        static int16_t long_held[120000];
        for (int16_t& v : long_held) v = 16384;
        SamplePlayer p;
        p.set_sample(long_held, 120000);
        p.set_envelope(0, 0, 100, 30000);  // 30 s demandées
        p.trigger(1.0f);
        play(p, 100);
        p.trigger(1.0f);
        auto out = play(p, 90000);
        CHECK(near(out[44100], HALF + HALF * 0.5f, 2e-3f), "Release de 30 s demandé : borné à 2 s (la queue est à mi-chemin après 1 s)");
        CHECK(near(out[89000], HALF), "…et finie après 2 s");
    }
    {
        // Pool de queues : plus de 4 relances rapprochées ; on reprend la queue la plus basse (la plus ancienne).
        SamplePlayer p;
        p.set_sample(held, 40000);
        p.set_envelope(0, 0, 100, 1000);  // Release de 44 100 échantillons
        p.trigger(1.0f);
        for (int i = 1; i <= 5; ++i) {
            play(p, 100);
            p.trigger(1.0f);
        }
        auto out = play(p, 10);
        // Relances à 100, 200, 300, 400, 500 : la première queue (démarrée à 100) est reprise par la cinquième.
        // Restent les queues démarrées à 200, 300, 400 et 500, à j échantillons après la dernière relance.
        float expected = HALF;
        for (int start : {200, 300, 400, 500}) expected += HALF * (1.0f - static_cast<float>(500 - start) / 44100.0f);
        CHECK(near(out[0], expected, 5e-4f), "cinquième relance : la queue la plus ancienne est reprise, les plus récentes restent");
    }

    // ----- Commandes du moteur -----
    {
        auto cmd = [](Command::Type type, int track, float value) {
            Command c;
            c.type  = type;
            c.track = static_cast<uint8_t>(track);
            c.value = value;
            return c;
        };
        auto set = [](Engine& e, const int16_t* data, uint32_t n) {
            Command c;
            c.type   = Command::SetTrackSample;
            c.track  = 2;
            c.data   = data;
            c.length = n;
            e.push(c);
        };
        auto render = [](Engine& e, int n) {
            std::vector<float> l(static_cast<std::size_t>(n)), r(l.size());
            e.render(l.data(), r.data(), n);
            return l;
        };
        auto out_of = [](float v) { return std::tanh(cfg::MASTER_GAIN * v); };
        auto note = [&](Engine& e, int track = 2) {
            Command c = cmd(Command::Note, track, 127.0f);
            e.push(c);
        };

        // Attack par commande : la note monte de 0 à 0,5 en 441 échantillons.
        Engine e;
        set(e, held, 40000);
        e.push(cmd(Command::SetTrackAttack, 2, 10.0f));
        note(e);
        auto o = render(e, 600);
        CHECK(o[0] == 0.0f && near(o[220], out_of(HALF * 220.0f / MS10)) && near(o[500], out_of(HALF)), "SetTrackAttack : rampe sur la piste");

        // Decay et sustain.
        Engine d;
        set(d, held, 40000);
        d.push(cmd(Command::SetTrackDecay, 2, 10.0f));
        d.push(cmd(Command::SetTrackSustain, 2, 40.0f));
        note(d);
        o = render(d, 900);
        CHECK(near(o[0], out_of(HALF)) && near(o[800], out_of(HALF * 0.4f)), "SetTrackDecay et SetTrackSustain");

        // Arrondi, bornes, piste invalide.
        Engine b;
        set(b, held, 40000);
        b.push(cmd(Command::SetTrackAttack, 2, 9.6f));   // 10 ms
        b.push(cmd(Command::SetTrackAttack, 200, 500.0f));
        note(b);
        o = render(b, 500);
        CHECK(near(o[220], out_of(HALF * 220.0f / MS10)), "valeur arrondie (9,6 -> 10 ms), piste invalide ignorée");
        Engine c2;
        set(c2, held, 40000);
        c2.push(cmd(Command::SetTrackSustain, 2, 500.0f));  // borné à 100
        c2.push(cmd(Command::SetTrackDecay, 2, 100.0f));
        note(c2);
        o = render(c2, 300);
        CHECK(near(o[0], out_of(HALF)) && near(o[299], out_of(HALF)), "sustain borné à 100 %");
        Engine c3;
        set(c3, held, 40000);
        c3.push(cmd(Command::SetTrackSustain, 2, -20.0f));  // borné à 0
        c3.push(cmd(Command::SetTrackDecay, 2, 1.0f));
        note(c3);
        o = render(c3, 300);
        CHECK(near(o[299], 0.0f, 1e-6f), "sustain négatif : 0 %");
        Engine c4;
        set(c4, held, 40000);
        c4.push(cmd(Command::SetTrackAttack, 2, 99999.0f));  // borné à 2000 ms = 88 200 échantillons
        note(c4);
        o = render(c4, 10000);
        CHECK(near(o[8820], out_of(HALF * 8820.0f / 88200.0f), 1e-3f), "attack borné à 2000 ms");

        // Chaque piste son enveloppe.
        Engine t;
        set(t, held, 40000);
        Command c = cmd(Command::SetTrackSample, 3, 0.0f);
        c.data    = held;
        c.length  = 40000;
        t.push(c);
        t.push(cmd(Command::SetTrackAttack, 3, 10.0f));
        note(t, 2);
        note(t, 3);
        o = render(t, 300);
        CHECK(near(o[0], out_of(HALF)) && near(o[220], out_of(HALF + HALF * 220.0f / MS10)), "deux pistes : une seule a une attack");

        // ClearTrack remet les valeurs par défaut.
        Engine r;
        set(r, held, 40000);
        r.push(cmd(Command::SetTrackAttack, 2, 100.0f));
        r.push(cmd(Command::SetTrackSustain, 2, 10.0f));
        r.push(cmd(Command::SetTrackRelease, 2, 900.0f));
        r.push(cmd(Command::ClearTrack, 2, 0.0f));
        set(r, held, 40000);
        note(r);
        o = render(r, 3000);
        bool flat = true;
        for (float v : o) flat = flat && near(v, out_of(HALF), 1e-6f);
        CHECK(flat, "ClearTrack : enveloppe par défaut");
        Engine r2;
        set(r2, held, 40000);
        r2.push(cmd(Command::SetTrackAttack, 2, 100.0f));
        r2.push(cmd(Command::ClearAll, 0, 0.0f));
        set(r2, held, 40000);
        note(r2);
        o = render(r2, 10);
        CHECK(near(o[0], out_of(HALF), 1e-6f), "ClearAll : enveloppe par défaut");
    }

    // ----- De bout en bout : le clic à la relance d'un 808 dans le séquenceur -----
    {
        auto run = [&](bool release0) {
            Engine e;
            Command c;
            c.type   = Command::SetTrackSample;
            c.track  = 2;
            c.data   = burst;
            c.length = 88200;
            e.push(c);
            if (release0) {
                Command r;
                r.type  = Command::SetTrackRelease;
                r.track = 2;
                r.value = 0.0f;
                e.push(r);
            }
            for (int step : {0, 1}) {
                Command s;
                s.type  = Command::SetStep;
                s.track = 2;
                s.step  = static_cast<uint8_t>(step);
                s.value = 1.0f;
                e.push(s);
            }
            Command pl;
            pl.type = Command::Play;
            e.push(pl);
            std::vector<float> l(7000), r(7000);
            e.render(l.data(), r.data(), 7000);
            return l;
        };
        const auto hard = run(true);
        const auto soft = run(false);
        // Le pas 1 démarre à l'échantillon 5513 (un pas = 5512,5), au sommet du sinus de 50 Hz.
        CHECK(max_jump(hard, 5500, 5530) > 0.2f, "(sans Release : un clic net à la relance)");
        CHECK(max_jump(soft, 5500, 5530) < 0.03f, "séquenceur, Release par défaut (5 ms) : la relance d'un 808 ne clique plus");
    }

    if (failures == 0) std::printf("OK : test_envelope\n");
    return failures == 0 ? 0 : 1;
}
