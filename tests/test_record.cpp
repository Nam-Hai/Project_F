// Enregistrement dans le moteur : pré-compte, datation des frappes, règle de prise, retour vers l'UI.
// Une frappe (Command::Note) est datée au début du bloc de rendu qui la suit : les tests rendent donc jusqu'à
// l'instant voulu, puis envoient la frappe. Un pas = L = 5512,5 échantillons (120 BPM), un tour = 88200.
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

static constexpr double L   = 5512.5;
static constexpr int    BAR = 88200;

static const int16_t impulse[] = {16384, 0};

static Command cmd(Command::Type type, int track = 0, int step = 0, float value = 0.0f) {
    Command c;
    c.type  = type;
    c.track = static_cast<uint8_t>(track);
    c.step  = static_cast<uint8_t>(step);
    c.value = value;
    return c;
}

static Command note(int track, int velocity = 127, int pitch = 0) {
    Command c = cmd(Command::Note, track, 0, static_cast<float>(velocity));
    c.pitch   = static_cast<int8_t>(pitch);
    return c;
}

// Moteur de test : piste 2 sur une impulsion, tout le rendu cumulé dans `out`.
struct Rig {
    Engine             e;
    std::vector<float> out;
    int                pos = 0;

    Rig() {
        Command c = cmd(Command::SetTrackSample, 2);
        c.data    = impulse;
        c.length  = 2;
        e.push(c);
    }
    void to(int frame) {  // rend jusqu'à l'échantillon `frame` (exclu)
        if (frame < pos) {
            std::printf("FAIL: test mal écrit (retour en arrière à %d)\n", frame);
            ++failures;
            return;
        }
        std::vector<float> l(static_cast<std::size_t>(frame - pos)), r(l.size());
        if (!l.empty()) e.render(l.data(), r.data(), static_cast<int>(l.size()));
        out.insert(out.end(), l.begin(), l.end());
        pos = frame;
    }
    void hit_at(int frame, int velocity = 127, int pitch = 0, int track = 2) {
        to(frame);
        e.push(note(track, velocity, pitch));
    }
    // Indices des notes au-dessus du seuil dans [from, to).
    std::vector<int> hits(int from, int to, float threshold = 0.02f) const {
        std::vector<int> h;
        for (int i = from; i < to && i < static_cast<int>(out.size()); ++i)
            if (out[static_cast<std::size_t>(i)] > threshold) h.push_back(i);
        return h;
    }
    std::vector<StepEvent> events() {
        std::vector<StepEvent> v;
        StepEvent              ev;
        while (e.pop_step_event(ev)) v.push_back(ev);
        return v;
    }
};

static int idx(int k, double o) { return static_cast<int>(std::ceil(k * L + o / 128.0 * L)); }
static bool eq(const std::vector<int>& a, std::initializer_list<int> b) { return a == std::vector<int>(b); }

int main() {
    // ----- Jeu libre : rien n'est enregistré -----
    {
        Rig r;
        r.e.push(cmd(Command::Play));
        r.hit_at(10000);
        r.to(BAR * 2);
        CHECK(r.events().empty(), "jeu libre : aucun événement");
        CHECK(eq(r.hits(0, 20000), {10000}), "jeu libre : la note sonne tout de suite, une seule fois");
        CHECK(r.hits(BAR, 2 * BAR).empty(), "jeu libre : le pattern n'a pas bougé");
        CHECK(r.e.state().record_phase.load() == 0, "phase : repos");
    }

    // ----- Pré-compte : clics seuls, puis la prise -----
    {
        Rig r;
        // Une autre piste (3) joue ses pas 0 et 5 ; la piste enregistrée (2) en avait aussi, effacés par StartRecord.
        Command c3 = cmd(Command::SetTrackSample, 3);
        c3.data    = impulse;
        c3.length  = 2;
        r.e.push(c3);
        r.e.push(cmd(Command::SetStep, 3, 0, 1.0f));
        r.e.push(cmd(Command::SetStep, 3, 5, 1.0f));
        r.e.push(cmd(Command::SetStep, 2, 0, 1.0f));
        r.e.push(cmd(Command::SetStep, 2, 5, 1.0f));
        r.e.push(cmd(Command::StartRecord, 2));
        r.to(1);
        CHECK(r.e.state().record_phase.load() == 1 && r.e.state().playing.load(), "StartRecord : pré-compte, lecture lancée");

        r.to(30000);
        CHECK(r.e.state().precount_step.load() == 5 && r.e.state().step.load() == 5, "pas du pré-compte publié");
        r.to(BAR - 1);
        CHECK(r.e.state().precount_step.load() == 15, "dernier pas du pré-compte");
        r.to(BAR + 1);
        CHECK(r.e.state().record_phase.load() == 2 && r.e.state().precount_step.load() == -1, "après un tour : prise");

        // Clics aux temps 0, 4, 8, 12 (22050 échantillons d'écart), rien entre eux, et aucune note de piste.
        auto energy = [&](int from, int to) {
            float m = 0.0f;
            for (int i = from; i < to; ++i) m = std::fmax(m, std::fabs(r.out[static_cast<std::size_t>(i)]));
            return m;
        };
        bool clicks = true;
        for (int k = 0; k < 4; ++k) clicks = clicks && energy(k * 22050 + 100, k * 22050 + 1200) > 0.1f && energy(k * 22050 + 1500, (k + 1) * 22050 - 1) < 1e-6f;
        CHECK(clicks, "quatre clics, un par temps, sans rien d'autre");
        auto crossings = [&](int from) {
            int n = 0;
            for (int i = from + 1; i < from + 1300; ++i) n += r.out[static_cast<std::size_t>(i - 1)] < 0.0f && r.out[static_cast<std::size_t>(i)] >= 0.0f;
            return n;
        };
        CHECK(crossings(0) > crossings(22050) + 5, "le premier temps est plus aigu");
        // Le pattern ne joue qu'à partir de la prise (pas 0 du tour suivant).
        r.to(BAR + 6000);
        CHECK(!r.hits(BAR - 5, BAR + 5, 0.01f).empty() && r.e.state().triggers[3].load() >= 1, "pas 0 de l'autre piste : joué au début de la prise");
        CHECK(r.e.state().triggers[2].load() == 0, "pas de la piste enregistrée : effacés au lancement, rien ne joue");
    }

    // ----- Datation d'une frappe -----
    {
        // Pas 3 et quart : pas 3, décalage +32.
        Rig r;
        r.e.push(cmd(Command::StartRecord, 2));
        const int T = BAR + 17916;
        r.hit_at(T);
        r.to(T + 1);
        auto ev = r.events();
        CHECK(ev.size() == 1 && ev[0].track == 2 && ev[0].step == 3 && ev[0].main.offset == 32 && ev[0].main.velocity == 127 &&
                  ev[0].main.pitch == 0 && ev[0].extra_count == 0,
              "frappe à 3,25 : pas 3, décalage +32");
        r.to(3 * BAR);
        CHECK(eq(r.hits(BAR + 17000, 2 * BAR), {T}), "tour de la prise : la frappe ne sonne qu'une fois (pas de double coup)");
        CHECK(eq(r.hits(2 * BAR, 3 * BAR), {2 * BAR + idx(3, 32)}), "tour suivant : la note enregistrée sonne à sa place exacte");
    }
    {
        // Juste avant le pas 4, après le pas 3 : pas 4, décalage négatif.
        Rig r;
        r.e.push(cmd(Command::StartRecord, 2));
        r.hit_at(BAR + idx(4, -20), 90, 5);
        r.to(BAR + idx(4, -20) + 1);
        auto ev = r.events();
        CHECK(ev.size() == 1 && ev[0].step == 4 && ev[0].main.offset == -20 && ev[0].main.velocity == 90 && ev[0].main.pitch == 5,
              "frappe en avance : pas 4, décalage -20, vélocité et pitch conservés");
        r.to(3 * BAR);
        CHECK(eq(r.hits(2 * BAR, 3 * BAR), {2 * BAR + idx(4, -20)}), "rejouée en avance au tour suivant");
    }
    {
        // Sur la frontière, avant et après.
        Rig r;
        r.e.push(cmd(Command::StartRecord, 2));
        r.hit_at(BAR + 11025);  // exactement 2 pas : frontière due
        r.hit_at(BAR + 5513 + 11025);  // 5513 : pas 1 vient de commencer (frontière à 5512,5) + 2 pas
        r.to(BAR + 30000);
        CHECK(r.e.state().triggers[2].load() == 2, "frontière due à l'arrivée de la frappe : chaque frappe ne déclenche la piste qu'une fois");
        CHECK(r.hits(BAR + 11020, BAR + 11030).size() == 1 && r.hits(BAR + 5513 + 11020, BAR + 5513 + 11030).size() == 1,
              "frontière due à l'arrivée de la frappe : pas de double coup");
        auto ev = r.events();
        CHECK(ev.size() == 2 && ev[0].step == 2 && ev[0].main.offset == 0, "à l'instant d'une frontière : décalage 0 sur le pas qui commence");
        CHECK(ev.size() == 2 && ev[1].step == 3 && std::abs(ev[1].main.offset) <= 1, "juste après une frontière fractionnaire : décalage nul");
    }
    {
        // Presque à mi-chemin entre deux pas : le décalage est borné à +63.
        Rig r;
        r.e.push(cmd(Command::StartRecord, 2));
        r.hit_at(BAR + 24795);
        r.to(BAR + 24796);
        auto ev = r.events();
        CHECK(ev.size() == 1 && ev[0].step == 4 && ev[0].main.offset == 63, "décalage borné à +63");
    }
    {
        // Latence : 10 ms = 441 échantillons retirés.
        Rig r;
        r.e.push(cmd(Command::SetLatency, 0, 0, 10.0f));
        r.e.push(cmd(Command::StartRecord, 2));
        const int    T   = BAR + 17916;
        r.hit_at(T);
        r.to(T + 1);
        auto         ev  = r.events();
        const double pos = (17916.0 - 441.0) / L;  // 3,170
        const int    off = static_cast<int>(std::lround((pos - 3.0) * 128.0));
        CHECK(ev.size() == 1 && ev[0].step == 3 && ev[0].main.offset == off, "latence 10 ms retirée du décalage");

        // Latence qui fait passer la frappe sur le pas précédent.
        Rig q;
        q.e.push(cmd(Command::SetLatency, 0, 0, 100.0f));
        q.e.push(cmd(Command::StartRecord, 2));
        q.hit_at(BAR + 2 * 5513);  // 11026 - 4410 = 6616 : 1,2 pas
        q.to(BAR + 20000);
        ev = q.events();
        CHECK(ev.size() == 1 && ev[0].step == 1 && ev[0].main.offset == static_cast<int>(std::lround((11026.0 - 4410.0) / L * 128.0 - 128.0)),
              "latence 100 ms : la frappe va sur le pas précédent");

        Rig z;
        z.e.push(cmd(Command::SetLatency, 0, 0, 9999.0f));  // borné à 200 ms
        z.e.push(cmd(Command::StartRecord, 2));
        z.hit_at(BAR + 20000);
        z.to(BAR + 20001);
        ev = z.events();
        CHECK(ev.size() == 1 && ev[0].step == 2 && ev[0].main.offset == static_cast<int>(std::lround((20000.0 - 8820.0) / L * 128.0 - 256.0)),
              "latence bornée à 200 ms");
    }

    // ----- Avant le premier pas de la prise -----
    {
        // Frappe un peu avant la fin du pré-compte : rangée au pas 0, décalage négatif, sans double coup.
        Rig r;
        r.e.push(cmd(Command::StartRecord, 2));
        r.hit_at(BAR - 1000);
        r.to(BAR - 999);
        CHECK(r.events().empty(), "avant la prise : rien d'envoyé tout de suite");
        r.to(BAR + 2000);
        auto ev = r.events();
        const int off = static_cast<int>(std::lround((((BAR - 1000) - 15 * L) / L - 1.0) * 128.0));
        CHECK(ev.size() == 1 && ev[0].step == 0 && ev[0].main.offset == off && off < 0, "rangée au début de la prise : pas 0, décalage négatif");
        CHECK(eq(r.hits(BAR - 1000 - 5, BAR + 2000), {BAR - 1000}), "elle ne sonne qu'une fois");
        r.to(3 * BAR);
        CHECK(eq(r.hits(2 * BAR - 3000, 2 * BAR + 1000), {2 * BAR + idx(0, off)}), "rejouée en avance au tour suivant");
    }
    {
        // Pendant le reste du pré-compte, ou avant le dernier demi-pas : ignorée.
        Rig r;
        r.e.push(cmd(Command::StartRecord, 2));
        r.hit_at(20000);
        r.hit_at(BAR - 3500);  // 15,36 : plus proche du pas 15 que du pas 0
        r.to(BAR + 100);
        CHECK(r.events().empty(), "frappes du pré-compte ignorées");
        // Frappes très en retard sur le pas 15 du pré-compte, ou avant la prise à cause de la latence.
        Rig q;
        q.e.push(cmd(Command::SetLatency, 0, 0, 100.0f));
        q.e.push(cmd(Command::StartRecord, 2));
        q.hit_at(BAR + 100);
        q.to(BAR + 300);
        CHECK(q.events().empty(), "frappe qui, latence retirée, tombe avant la prise : ignorée");
        // Quatre frappes avant le premier pas : toutes gardées (roulement), les suivantes perdues.
        Rig m;
        m.e.push(cmd(Command::StartRecord, 2));
        for (int i = 0; i < 6; ++i) m.hit_at(BAR - 1400 + 100 * i);
        m.to(BAR + 3000);
        auto ev = m.events();
        CHECK(ev.size() == 4 && ev[3].step == 0 && ev[3].extra_count == 3, "quatre frappes d'avant la prise : un roulement au pas 0");
        CHECK(m.e.state().record_lost.load() == 2, "les deux suivantes sont comptées comme perdues");
    }

    // ----- Règle de prise -----
    {
        Rig r;
        r.e.push(cmd(Command::StartRecord, 2));
        const int base = BAR + 3 * 5513;
        r.hit_at(base + 200, 100, 1);  // pas 3, premier du tour : sa note principale
        r.to(base + 201);
        auto ev = r.events();
        CHECK(ev.size() == 1 && ev[0].extra_count == 0 && ev[0].main.velocity == 100 && ev[0].main.pitch == 1, "première frappe : note principale du pas");
        r.hit_at(base + 600, 90);
        r.hit_at(base + 900, 80);
        r.hit_at(base + 1200, 70);
        r.to(base + 1201);
        ev = r.events();
        CHECK(ev.size() == 3 && ev[0].extra_count == 1 && ev[1].extra_count == 2 && ev[2].extra_count == 3 && ev[2].extra[2].velocity == 70 &&
                  ev[2].main.velocity == 100,
              "frappes suivantes du même tour : s'ajoutent (roulement)");
        // Cinquième frappe : remplace la note la plus proche en temps (celle à base+1200).
        r.hit_at(base + 1300, 60);
        r.to(base + 1301);
        ev = r.events();
        CHECK(ev.size() == 1 && ev[0].extra_count == 3 && ev[0].extra[2].velocity == 60 && ev[0].extra[1].velocity == 80 && ev[0].main.velocity == 100,
              "cinquième frappe : remplace la plus proche en temps");

        // Tour suivant : la première frappe remplace de nouveau tout le pas.
        r.hit_at(base + BAR + 300, 50);
        r.to(base + BAR + 301);
        ev = r.events();
        CHECK(ev.size() == 1 && ev[0].extra_count == 0 && ev[0].main.velocity == 50, "nouveau tour : la première frappe remplace les quatre notes");

        // Un pas non touché de la prise reste tel quel d'un tour à l'autre : un pas enregistré au tour 1 rejoue au tour 2.
        Rig q;
        q.e.push(cmd(Command::StartRecord, 2));
        q.hit_at(BAR + 9 * 5513);
        q.to(2 * BAR + 60000);
        CHECK(!q.hits(2 * BAR + 9 * 5512, 2 * BAR + 9 * 5514).empty() && q.hits(2 * BAR + 3 * 5512, 2 * BAR + 3 * 5514).empty(),
              "pas touché au tour 1 : rejoué au tour 2 ; pas jamais touché : vide");
    }
    {
        // La première frappe d'un pas, au tour suivant, retire l'ancienne note de ce pas encore en attente (décalée
        // plus tard dans le pas) : sinon elle sonnerait après la nouvelle.
        Rig r;
        r.e.push(cmd(Command::StartRecord, 2));
        r.hit_at(BAR + 3 * 5513 + 2600);                 // tour 1 : pas 3, décalage d'environ +60
        r.hit_at(2 * BAR + 3 * 5513 + 600);              // tour 2 : pas 3, décalage d'environ +14 (la note de +60 est en attente)
        r.to(2 * BAR + 3 * 5513 + 9000);
        CHECK(r.hits(2 * BAR + 3 * 5512, 2 * BAR + 3 * 5512 + 8000).size() == 1, "ancienne note du pas retirée : seule la nouvelle frappe sonne");
    }
    {
        // StartRecord efface les pas de la piste enregistrée, mais garde sa quantification et ne touche pas aux autres pistes.
        Rig r;
        Command c3 = cmd(Command::SetTrackSample, 3);
        c3.data    = impulse;
        c3.length  = 2;
        r.e.push(c3);
        r.e.push(cmd(Command::SetStep, 2, 9, 1.0f));
        r.e.push(cmd(Command::SetStep, 3, 9, 1.0f));
        r.e.push(cmd(Command::SetTrackQuantize, 2, 0, 50.0f));
        r.e.push(cmd(Command::StartRecord, 2));
        r.hit_at(BAR + 17916);  // pas 3, décalage +32 : à 50 % de quantification, rejoué à +16
        r.to(3 * BAR);
        CHECK(r.hits(2 * BAR + 9 * 5512, 2 * BAR + 9 * 5514).size() == 1 && r.e.state().triggers[3].load() >= 1,
              "la piste 3 garde son pas 9 (le pas 9 de la piste 2 est effacé : un seul coup, celui de la piste 3)");
        CHECK(eq(r.hits(2 * BAR + 3 * 5500, 2 * BAR + 3 * 5520 + 4000), {2 * BAR + idx(3, 16)}), "la quantification de la piste est conservée : rejoué à +16 (50 %)");
    }
    {
        // Le plus proche en temps : 4 notes à environ -23, 0, +23 et +46 ; une cinquième à +48 remplace la dernière.
        Rig r;
        r.e.push(cmd(Command::StartRecord, 2));
        const int c0 = BAR + 5 * 5513;  // pas 5 (frontière à 27562,5 -> 27563)
        r.hit_at(c0 - 1000, 100);       // main
        r.hit_at(c0, 90);
        r.hit_at(c0 + 1000, 80);
        r.hit_at(c0 + 2000, 70);
        r.hit_at(c0 + 2100, 60);
        r.to(c0 + 2101);
        auto ev = r.events();
        CHECK(ev.size() == 5 && ev[4].step == 5 && ev[4].extra_count == 3, "cinq frappes sur le pas 5");
        CHECK(ev.size() == 5 && ev[4].main.velocity == 100 && ev[4].extra[0].velocity == 90 && ev[4].extra[1].velocity == 80 &&
                  ev[4].extra[2].velocity == 60,
              "la plus proche en temps (+46) est remplacée par la nouvelle (+48)");
    }

    // ----- Autres pistes et fin de prise -----
    {
        Rig r;
        r.e.push(cmd(Command::StartRecord, 2));
        r.hit_at(BAR + 8000, 127, 0, 5);  // une autre piste : sonne (si elle a une voix), mais n'est pas enregistrée
        r.to(BAR + 8001);
        CHECK(r.events().empty(), "autre piste : non enregistrée");

        r.e.push(cmd(Command::StopRecord));
        r.to(BAR + 8100);
        CHECK(r.e.state().record_phase.load() == 0 && r.e.state().playing.load(), "StopRecord : la lecture continue");
        r.hit_at(BAR + 9000);
        r.to(BAR + 9001);
        CHECK(r.events().empty(), "après la prise : jeu libre");
    }
    {
        // Annuler pendant le pré-compte arrête aussi la lecture lancée par lui.
        Rig r;
        r.e.push(cmd(Command::StartRecord, 2));
        r.to(20000);
        r.e.push(cmd(Command::StopRecord));
        r.to(20100);
        CHECK(r.e.state().record_phase.load() == 0 && !r.e.state().playing.load() && r.e.state().precount_step.load() == -1,
              "StopRecord pendant le pré-compte : tout s'arrête");
        r.to(BAR * 2);
        CHECK(r.hits(20100, BAR * 2).empty(), "plus de clics ni de notes");

        // Stop et Play mettent fin à la prise.
        Rig s;
        s.e.push(cmd(Command::StartRecord, 2));
        s.to(BAR + 100);
        s.e.push(cmd(Command::Stop));
        s.hit_at(BAR + 200);
        s.to(BAR + 300);
        CHECK(s.e.state().record_phase.load() == 0 && s.events().empty(), "Stop : fin de la prise");
        Rig p;
        p.e.push(cmd(Command::StartRecord, 2));
        p.to(30000);
        p.e.push(cmd(Command::Play));
        p.hit_at(30100);
        p.to(30200);
        CHECK(p.e.state().record_phase.load() == 0 && p.e.state().precount_step.load() == -1 && p.events().empty(), "Play pendant le pré-compte : prise annulée");

        // Recommencer pendant la prise : nouveau pré-compte depuis le début.
        Rig q;
        q.e.push(cmd(Command::StartRecord, 2));
        q.to(BAR + 20000);
        q.e.push(cmd(Command::StartRecord, 2));
        q.to(BAR + 20100);
        CHECK(q.e.state().record_phase.load() == 1, "StartRecord pendant une prise : repart du pré-compte");
        q.hit_at(BAR + 20200);
        q.to(BAR + 20300);
        CHECK(q.events().empty(), "frappe pendant le nouveau pré-compte : ignorée");
    }

    // ----- Retour vers l'UI : file pleine -----
    {
        Rig r;
        r.e.push(cmd(Command::StartRecord, 2));
        r.to(BAR + 100);
        for (int block = 0; block < 2; ++block) {
            for (int i = 0; i < 35; ++i) r.e.push(note(2));
            r.to(r.pos + 10);
        }
        // 70 frappes sur le même pas : 70 événements, la file en porte 64.
        auto ev = r.events();
        CHECK(ev.size() == 64, "file de retour pleine : 64 événements");
        CHECK(r.e.state().record_lost.load() == 6, "les 6 suivants sont comptés comme perdus");
        r.e.push(note(2));
        r.to(r.pos + 10);
        CHECK(r.events().size() == 1, "la file se vide et repart");
    }

    // ----- Les valeurs aberrantes ne cassent rien -----
    {
        Rig r;
        r.e.push(cmd(Command::StartRecord, 200));  // piste inexistante : ignorée
        r.to(100);
        CHECK(r.e.state().record_phase.load() == 0, "StartRecord sur une piste inexistante : ignoré");
        r.e.push(cmd(Command::SetLatency, 0, 0, -50.0f));
        r.e.push(cmd(Command::StartRecord, 2));
        r.hit_at(BAR + 10000, 5000, 99);  // vélocité et pitch bornés
        r.to(BAR + 10001);
        auto ev = r.events();
        CHECK(ev.size() == 1 && ev[0].main.velocity == 127 && ev[0].main.pitch == 12, "vélocité et pitch bornés ; latence négative = 0");
    }

    if (failures == 0) std::printf("OK : test_record\n");
    return failures == 0 ? 0 : 1;
}
