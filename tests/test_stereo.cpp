// Moteur stéréo et pan : lecture de samples stéréo, panoramique par note (balance linéaire, gain 1 au centre),
// pan fixé au déclenchement, pan des pas, des notes supplémentaires, des notes décalées et de la prise.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "../config.h"
#include "../core/dsp_utils.h"
#include "../core/engine.h"
#include "../core/ui/ui_controller.h"
#include "../platform/desktop/sample_bank.h"
#include "../platform/desktop/sample_library.h"

static int failures = 0;

#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            std::printf("FAIL: %s\n", msg);         \
            ++failures;                             \
        }                                           \
    } while (0)

static constexpr double L = 5512.5;

static const int16_t mono_impulse[]   = {16384, 0};          // 0,5
static const int16_t stereo_impulse[] = {16384, 8192, 0, 0}; // G = 0,5, D = 0,25 ; 2 images

// Sortie après master : tanh(0,8 x valeur).
static float out_of(float v) { return std::tanh(cfg::MASTER_GAIN * v); }
static bool  near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

static Command cmd(Command::Type type, int track = 0, int step = 0, float value = 0.0f) {
    Command c;
    c.type  = type;
    c.track = static_cast<uint8_t>(track);
    c.step  = static_cast<uint8_t>(step);
    c.value = value;
    return c;
}
static Command set_sample(int track, const int16_t* data, uint32_t frames, int channels = 1) {
    Command c  = cmd(Command::SetTrackSample, track);
    c.data     = data;
    c.length   = frames;
    c.channels = static_cast<uint8_t>(channels);
    return c;
}
static Command note(int track, int pitch = 0, int pan = 0, int velocity = 127) {
    Command c = cmd(Command::Note, track, 0, static_cast<float>(velocity));
    c.pitch   = static_cast<int8_t>(pitch);
    c.pan     = static_cast<int8_t>(pan);
    return c;
}

struct Out {
    std::vector<float> l, r;
};

static Out render(Engine& e, int frames) {
    Out o;
    o.l.resize(static_cast<std::size_t>(frames));
    o.r.resize(static_cast<std::size_t>(frames));
    const int sizes[] = {128, 37, 200, 1, 64, 1000};
    int       k       = 0;
    for (int pos = 0; pos < frames;) {
        int n = sizes[k++ % 6];
        if (frames - pos < n) n = frames - pos;
        e.render(o.l.data() + pos, o.r.data() + pos, n);
        pos += n;
    }
    return o;
}

// Valeurs gauche et droite au premier échantillon après une note jouée à l'instant 0.
static Out first(const int16_t* data, uint32_t frames, int channels, Command n, int count = 4) {
    Engine e;
    e.push(set_sample(2, data, frames, channels));
    e.push(n);
    return render(e, count);
}

int main() {
    // ----- La loi de pan -----
    {
        float l, r;
        dsp::pan_gains(0, l, r);
        CHECK(l == 1.0f && r == 1.0f, "centre : gain 1 des deux côtés");
        dsp::pan_gains(-50, l, r);
        CHECK(l == 1.0f && r == 0.0f, "tout à gauche");
        dsp::pan_gains(50, l, r);
        CHECK(l == 0.0f && r == 1.0f, "tout à droite");
        dsp::pan_gains(25, l, r);
        CHECK(near(l, 0.5f, 1e-6f) && r == 1.0f, "+25 : la gauche à moitié");
        dsp::pan_gains(-10, l, r);
        CHECK(l == 1.0f && near(r, 0.8f, 1e-6f), "-10 : la droite à 80 %");
        dsp::pan_gains(500, l, r);
        CHECK(l == 0.0f && r == 1.0f, "borné à +50");
        dsp::pan_gains(-500, l, r);
        CHECK(l == 1.0f && r == 0.0f, "borné à -50");
    }

    // ----- Sample mono : au centre comme avant, puis placé -----
    {
        Out o = first(mono_impulse, 2, 1, note(2));
        CHECK(near(o.l[0], out_of(0.5f)) && o.l[0] == o.r[0], "mono au centre : mêmes valeurs à gauche et à droite, niveau d'avant");
        o = first(mono_impulse, 2, 1, note(2, 0, -50));
        CHECK(near(o.l[0], out_of(0.5f)) && o.r[0] == 0.0f, "mono, pan -50 : tout à gauche, plein niveau");
        o = first(mono_impulse, 2, 1, note(2, 0, 50));
        CHECK(o.l[0] == 0.0f && near(o.r[0], out_of(0.5f)), "mono, pan +50 : tout à droite");
        o = first(mono_impulse, 2, 1, note(2, 0, 25));
        CHECK(near(o.l[0], out_of(0.25f)) && near(o.r[0], out_of(0.5f)), "mono, pan +25 : gauche à moitié");
        o = first(mono_impulse, 2, 1, note(2, 0, -25));
        CHECK(near(o.l[0], out_of(0.5f)) && near(o.r[0], out_of(0.25f)), "mono, pan -25 : droite à moitié");
        o = first(mono_impulse, 2, 1, note(2, 0, 99));
        CHECK(o.l[0] == 0.0f && near(o.r[0], out_of(0.5f)), "pan borné à +50 par le moteur");
        o = first(mono_impulse, 2, 1, note(2, 0, -128));
        CHECK(o.r[0] == 0.0f && near(o.l[0], out_of(0.5f)), "pan borné à -50 par le moteur");
        o = first(mono_impulse, 2, 1, note(2, 0, 10, 64));
        CHECK(near(o.l[0], out_of(0.5f * 64.0f / 127.0f * 0.8f)) && near(o.r[0], out_of(0.5f * 64.0f / 127.0f)), "pan et vélocité ensemble");
    }

    // ----- Sample stéréo : l'image est conservée -----
    {
        Out o = first(stereo_impulse, 2, 2, note(2));
        CHECK(near(o.l[0], out_of(0.5f)) && near(o.r[0], out_of(0.25f)), "stéréo au centre : chaque canal garde son niveau");
        o = first(stereo_impulse, 2, 2, note(2, 0, 50));
        CHECK(o.l[0] == 0.0f && near(o.r[0], out_of(0.25f)), "stéréo, pan +50 : la gauche est coupée, la droite reste");
        o = first(stereo_impulse, 2, 2, note(2, 0, -50));
        CHECK(near(o.l[0], out_of(0.5f)) && o.r[0] == 0.0f, "stéréo, pan -50 : la droite est coupée");
        o = first(stereo_impulse, 2, 2, note(2, 0, 25));
        CHECK(near(o.l[0], out_of(0.25f)) && near(o.r[0], out_of(0.25f)), "stéréo, pan +25 : balance");
        // Interpolation entre deux images : le pitch de la piste agit sur les deux canaux.
        Engine e;
        e.push(set_sample(2, stereo_impulse, 2, 2));
        Command p = cmd(Command::SetTrackPitch, 2, 0, 0.5f);
        e.push(p);
        e.push(note(2));
        Out h = render(e, 4);
        CHECK(near(h.l[0], out_of(0.5f)) && near(h.r[0], out_of(0.25f)), "pitch 0,5 : première image");
        CHECK(near(h.l[1], out_of(0.25f)) && near(h.r[1], out_of(0.125f)), "pitch 0,5 : image intermédiaire interpolée sur les deux canaux");
        CHECK(h.l[2] == 0.0f && h.r[2] == 0.0f, "puis le silence");
    }
    {
        // Rampe stéréo à pitch +12 (une image sur deux) : G = k x 1000, D = -k x 500.
        static int16_t ramp[12];
        for (int k = 0; k < 6; ++k) {
            ramp[2 * k]     = static_cast<int16_t>(k * 1000);
            ramp[2 * k + 1] = static_cast<int16_t>(-k * 500);
        }
        Out o = first(ramp, 6, 2, note(2, 12), 4);
        CHECK(near(o.l[1], out_of(2000.0f / 32768.0f)) && near(o.r[1], out_of(-1000.0f / 32768.0f)) && near(o.l[2], out_of(4000.0f / 32768.0f)) &&
                  near(o.r[2], out_of(-2000.0f / 32768.0f)),
              "stéréo +12 demi-tons : une image sur deux, canaux non mélangés");
        // Interpolation du canal droit : la deuxième image porte ses propres valeurs gauche et droite.
        static const int16_t two[] = {0, 0, 16000, -8000};
        o = first(two, 2, 2, note(2, 0, 0), 1);
        Engine h;
        h.push(set_sample(2, two, 2, 2));
        h.push(cmd(Command::SetTrackPitch, 2, 0, 0.5f));
        h.push(note(2));
        Out hh = render(h, 3);
        CHECK(near(hh.l[1], out_of(8000.0f / 32768.0f)) && near(hh.r[1], out_of(-4000.0f / 32768.0f)), "interpolation : chaque canal vers sa propre image suivante");
        // Un nombre de canaux invalide est lu comme du mono.
        static const int16_t six[] = {16384, 0, 0, 8192, 0, 0};
        Engine m3;
        m3.push(set_sample(2, six, 2, 3));
        m3.push(cmd(Command::SetTrackPitch, 2, 0, 0.5f));
        m3.push(note(2));
        Out om = render(m3, 3);
        CHECK(near(om.l[1], out_of(0.25f)) && om.l[1] == om.r[1], "canaux = 3 : lu comme du mono (images d'un échantillon)");
        o = first(mono_impulse, 2, 3, note(2));
        CHECK(near(o.l[0], out_of(0.5f)) && o.l[0] == o.r[0], "canaux invalides : lu comme mono");
        o = first(mono_impulse, 2, 0, note(2));
        CHECK(near(o.l[0], out_of(0.5f)), "canaux = 0 : mono");
    }

    // ----- Lecteur : API mono et accumulation -----
    {
        SamplePlayer p;
        p.set_sample(stereo_impulse, 2, 2);
        p.trigger(1.0f);
        float out[2] = {0.1f, 0.1f};
        p.render(out, 2);
        CHECK(near(out[0], 0.1f + 0.375f, 1e-6f), "render mono d'un sample stéréo : moyenne des canaux, ajoutée");

        SamplePlayer q;
        q.set_sample(stereo_impulse, 2, 2);
        q.trigger_panned(1.0f, 1.0f, 0);
        float l[2] = {0.1f, 0.1f}, r[2] = {0.2f, 0.2f};
        q.render_stereo(l, r, 2);
        CHECK(near(l[0], 0.1f + 0.5f, 1e-6f) && near(r[0], 0.2f + 0.25f, 1e-6f), "render stéréo : accumulation, pas d'écrasement");
        CHECK(!q.active(), "fin du sample");
        float l2[2] = {0, 0}, r2[2] = {0, 0};
        q.render_stereo(l2, r2, 2);
        CHECK(l2[0] == 0.0f && r2[1] == 0.0f, "inactif : rien");
    }

    // ----- Voie sans pan propre (kick synthétique) -----
    {
        Engine e;
        e.push(note(0, 0, 50));
        Out o = render(e, 2000);
        float sum_l = 0.0f, sum_r = 0.0f;
        for (std::size_t i = 0; i < o.l.size(); ++i) {
            sum_l += std::fabs(o.l[i]);
            sum_r += std::fabs(o.r[i]);
        }
        CHECK(sum_l == 0.0f && sum_r > 1.0f, "kick synthétique, pan +50 : tout à droite");
        Engine w;
        w.push(note(0, 0, -50));
        Out ow = render(w, 2000);
        sum_l = 0.0f;
        sum_r = 0.0f;
        for (std::size_t i = 0; i < ow.l.size(); ++i) {
            sum_l += std::fabs(ow.l[i]);
            sum_r += std::fabs(ow.r[i]);
        }
        CHECK(sum_r == 0.0f && sum_l > 1.0f, "kick synthétique, pan -50 : tout à gauche");
        Engine c;
        c.push(note(0));
        o = render(c, 500);
        bool same = true;
        for (std::size_t i = 0; i < o.l.size(); ++i) same = same && o.l[i] == o.r[i];
        CHECK(same, "kick au centre : gauche = droite");
    }

    // ----- Pan des pas -----
    {
        auto run = [](auto&& setup, int frames) {
            Engine e;
            e.push(set_sample(2, mono_impulse, 2));
            setup(e);
            e.push(cmd(Command::Play));
            return render(e, frames);
        };
        Out o = run([](Engine& e) {
            e.push(cmd(Command::SetStep, 2, 0, 1.0f));
            e.push(cmd(Command::SetStepPan, 2, 0, -30.0f));
        }, 10);
        CHECK(near(o.l[0], out_of(0.5f)) && near(o.r[0], out_of(0.5f * 0.4f)), "pas 0, pan -30 : droite à 40 %");

        // Bornes et arrondi de la commande.
        o = run([](Engine& e) {
            e.push(cmd(Command::SetStep, 2, 0, 1.0f));
            e.push(cmd(Command::SetStepPan, 2, 0, 400.0f));
        }, 10);
        CHECK(o.l[0] == 0.0f && near(o.r[0], out_of(0.5f)), "SetStepPan borné à +50");
        o = run([](Engine& e) {
            e.push(cmd(Command::SetStep, 2, 0, 1.0f));
            e.push(cmd(Command::SetStepPan, 2, 0, 24.6f));
        }, 10);
        CHECK(near(o.l[0], out_of(0.5f * 0.5f)), "SetStepPan : valeur arrondie (24,6 -> 25)");
        // Piste ou pas hors limites : ignorés sans effet.
        o = run([](Engine& e) {
            e.push(cmd(Command::SetStep, 2, 0, 1.0f));
            e.push(cmd(Command::SetStepPan, 200, 0, 50.0f));
            e.push(cmd(Command::SetStepPan, 2, 99, 50.0f));
        }, 10);
        CHECK(near(o.l[0], out_of(0.5f)) && near(o.r[0], out_of(0.5f)), "commandes de pan hors limites ignorées");

        // Le pan se conserve quand le pas est éteint puis rallumé.
        o = run([](Engine& e) {
            e.push(cmd(Command::SetStepPan, 2, 0, 50.0f));
            e.push(cmd(Command::SetStep, 2, 0, 1.0f));
        }, 10);
        CHECK(o.l[0] == 0.0f && near(o.r[0], out_of(0.5f)), "pan réglé avant d'allumer le pas");

        // ClearTrack et ClearAll remettent le pan à 0.
        {
            Engine e;
            e.push(set_sample(2, mono_impulse, 2));
            e.push(cmd(Command::SetStepPan, 2, 0, 50.0f));
            e.push(cmd(Command::ClearTrack, 2));
            e.push(set_sample(2, mono_impulse, 2));
            e.push(cmd(Command::SetStep, 2, 0, 1.0f));
            e.push(cmd(Command::Play));
            Out c = render(e, 10);
            CHECK(near(c.l[0], out_of(0.5f)) && near(c.r[0], out_of(0.5f)), "ClearTrack : pan remis à 0");
            Engine f;
            f.push(set_sample(2, mono_impulse, 2));
            f.push(cmd(Command::SetStepPan, 2, 0, -50.0f));
            f.push(cmd(Command::ClearAll));
            f.push(set_sample(2, mono_impulse, 2));
            f.push(cmd(Command::SetStep, 2, 0, 1.0f));
            f.push(cmd(Command::Play));
            c = render(f, 10);
            CHECK(near(c.r[0], out_of(0.5f)), "ClearAll : pan remis à 0");
        }

        // Note d'essai d'un pas : avec son pan.
        {
            Engine e;
            e.push(set_sample(2, mono_impulse, 2));
            e.push(cmd(Command::SetStepPan, 2, 5, 50.0f));
            e.push(cmd(Command::AuditionStep, 2, 5));
            Out c = render(e, 10);
            CHECK(c.l[0] == 0.0f && near(c.r[0], out_of(0.5f)), "AuditionStep : le pan du pas");
        }
    }

    // ----- Pan : notes supplémentaires, notes décalées, notes en attente -----
    {
        Engine e;
        e.push(set_sample(2, mono_impulse, 2));
        e.push(cmd(Command::SetStep, 2, 4, 1.0f));
        e.push(cmd(Command::SetStepOffset, 2, 4, 32.0f));
        e.push(cmd(Command::SetStepPan, 2, 4, -50.0f));
        Command x = cmd(Command::SetExtraHit, 2, 4, 127.0f);
        x.slot    = 1;
        x.offset  = 64 - 1;
        x.pan     = 50;
        e.push(x);
        e.push(cmd(Command::Play));
        Out o = render(e, 4 * 5513 + 5000);
        const int main_idx  = static_cast<int>(std::ceil(4 * L + 32.0 / 128.0 * L));
        const int extra_idx = static_cast<int>(std::ceil(4 * L + 63.0 / 128.0 * L));
        CHECK(near(o.l[static_cast<std::size_t>(main_idx)], out_of(0.5f)) && o.r[static_cast<std::size_t>(main_idx)] == 0.0f,
              "note principale décalée (en attente) : pan -50 conservé");
        CHECK(o.l[static_cast<std::size_t>(extra_idx)] == 0.0f && near(o.r[static_cast<std::size_t>(extra_idx)], out_of(0.5f)),
              "note supplémentaire : son propre pan +50");

        // Note en avance (programmée au pas précédent) et pas 0 au premier tour.
        Engine f;
        f.push(set_sample(2, mono_impulse, 2));
        f.push(cmd(Command::SetStep, 2, 4, 1.0f));
        f.push(cmd(Command::SetStepOffset, 2, 4, -32.0f));
        f.push(cmd(Command::SetStepPan, 2, 4, 50.0f));
        f.push(cmd(Command::Play));
        o = render(f, 4 * 5513);
        const int early = static_cast<int>(std::ceil(4 * L - 32.0 / 128.0 * L));
        CHECK(o.l[static_cast<std::size_t>(early)] == 0.0f && near(o.r[static_cast<std::size_t>(early)], out_of(0.5f)), "note en avance : pan conservé");

        Engine g;
        g.push(set_sample(2, mono_impulse, 2));
        g.push(cmd(Command::SetStep, 2, 0, 1.0f));
        g.push(cmd(Command::SetStepOffset, 2, 0, -20.0f));
        g.push(cmd(Command::SetStepPan, 2, 0, -50.0f));
        g.push(cmd(Command::Play));
        o = render(g, 10);
        CHECK(near(o.l[0], out_of(0.5f)) && o.r[0] == 0.0f, "pas 0 en avance au premier tour : pan conservé");
    }

    // ----- Pan fixé au déclenchement -----
    {
        static int16_t held[200];
        for (int16_t& v : held) v = 16384;
        Engine e;
        e.push(set_sample(2, held, 200));
        e.push(cmd(Command::SetStep, 2, 0, 1.0f));
        e.push(cmd(Command::SetStepPan, 2, 0, 50.0f));
        e.push(cmd(Command::Play));
        Out a = render(e, 50);
        e.push(cmd(Command::SetStepPan, 2, 0, -50.0f));
        Out b = render(e, 50);
        CHECK(a.l[10] == 0.0f && near(a.r[10], out_of(0.5f)) && b.l[10] == 0.0f && near(b.r[10], out_of(0.5f)),
              "changer le pan du pas pendant la note ne déplace pas la note en cours");
    }

    // ----- Plusieurs pistes, chacune son pan ; gain de piste -----
    {
        Engine e;
        e.push(set_sample(2, mono_impulse, 2));
        e.push(set_sample(3, mono_impulse, 2));
        Command g = cmd(Command::SetTrackGain, 3, 0, 0.5f);
        e.push(g);
        e.push(note(2, 0, -50));
        e.push(note(3, 0, 50));
        Out o = render(e, 4);
        CHECK(near(o.l[0], out_of(0.5f)) && near(o.r[0], out_of(0.25f)), "deux pistes : une à gauche, une à droite (gain de piste)");
    }

    // ----- Aperçu : le vrai stéréo, au centre -----
    {
        Engine e;
        Command c = cmd(Command::PreviewSample);
        c.data     = stereo_impulse;
        c.length   = 2;
        c.channels = 2;
        e.push(c);
        Out o = render(e, 4);
        CHECK(near(o.l[0], out_of(0.5f)) && near(o.r[0], out_of(0.25f)), "aperçu stéréo : image conservée");
    }

    // ----- Prise : le pan de la frappe est rangé -----
    {
        Engine e;
        e.push(set_sample(2, mono_impulse, 2));
        e.push(cmd(Command::StartRecord, 2));
        render(e, 88200 + 17916);
        e.push(note(2, 3, -40));
        render(e, 10);
        StepEvent ev;
        CHECK(e.pop_step_event(ev) && ev.step == 3 && ev.main.pan == -40 && ev.main.pitch == 3, "événement de prise : le pan de la frappe");
        e.push(note(2, 0, 20));
        render(e, 10);
        CHECK(e.pop_step_event(ev) && ev.extra_count == 1 && ev.extra[0].pan == 20 && ev.main.pan == -40, "note supplémentaire de prise : son pan");
        // Pan hors bornes : rangé borné.
        e.push(note(2, 0, 99));
        render(e, 10);
        CHECK(e.pop_step_event(ev) && ev.extra[1].pan == 50, "pan de la frappe borné à 50 dans la prise");
        e.push(cmd(Command::StopRecord));
        // Cinq frappes dans le même bloc (même position) : à égalité, la cinquième remplace la note principale.
        Engine q;
        q.push(set_sample(2, mono_impulse, 2));
        q.push(cmd(Command::StartRecord, 2));
        render(q, 88200 + 40000);
        const int pans[] = {10, 20, 30, 40, 33};
        for (int pan : pans) q.push(note(2, 0, pan));
        render(q, 10);
        StepEvent last;
        int       n = 0;
        while (q.pop_step_event(ev)) {
            last = ev;
            ++n;
        }
        CHECK(n == 5 && last.extra_count == 3 && last.main.pan == 33 && last.extra[0].pan == 20 && last.extra[1].pan == 30 && last.extra[2].pan == 40,
              "cinquième frappe à égalité : remplace la note principale, avec son pan");
        // Les frappes sans pan (clavier) : pan 0.
        Engine f;
        f.push(set_sample(2, mono_impulse, 2));
        f.push(cmd(Command::StartRecord, 2));
        render(f, 88200 + 17916);
        f.push(note(2));
        render(f, 10);
        CHECK(f.pop_step_event(ev) && ev.main.pan == 0, "frappe du clavier : pan 0");
    }
    {
        Engine e;
        e.push(set_sample(2, mono_impulse, 2));
        e.push(cmd(Command::StartRecord, 2));
        render(e, 88200 + 17916);
        e.push(note(2, 0, 50));
        render(e, 1);
        Out o = render(e, 88200 + 17916 - 1 + 5000);
        // Tour suivant : la note enregistrée (pas 3, +32) sonne, tout à droite.
        const int at = 88200 - 17916 - 1 + static_cast<int>(std::ceil(3 * L + 32.0 / 128.0 * L)) + 0;
        float     best_r = 0.0f, best_l = 0.0f;
        for (std::size_t i = static_cast<std::size_t>(at) - 3; i < static_cast<std::size_t>(at) + 3 && i < o.l.size(); ++i) {
            best_l = std::fmax(best_l, o.l[i]);
            best_r = std::fmax(best_r, o.r[i]);
        }
        CHECK(best_l == 0.0f && best_r > 0.3f, "rejouée au tour suivant avec son pan");
    }

    // ----- Clic du pré-compte : au centre -----
    {
        Engine e;
        e.push(cmd(Command::StartRecord, 2));
        Out  o    = render(e, 2000);
        bool same = true, loud = false;
        for (std::size_t i = 0; i < o.l.size(); ++i) {
            same = same && o.l[i] == o.r[i];
            loud = loud || std::fabs(o.l[i]) > 0.1f;
        }
        CHECK(same && loud, "clic du pré-compte : gauche = droite");
    }

    // ----- Banque et bibliothèque : le nombre de canaux suit le sample -----
    {
        namespace fs = std::filesystem;
        const std::string dir = "test_stereo_dir";
        fs::remove_all(dir);
        fs::create_directory(dir);
        auto write = [&](const char* name, int channels, const std::vector<int16_t>& v) {
            std::vector<uint8_t> b;
            auto p16 = [&](uint16_t x) { b.push_back(static_cast<uint8_t>(x)); b.push_back(static_cast<uint8_t>(x >> 8)); };
            auto p32 = [&](uint32_t x) { p16(static_cast<uint16_t>(x)); p16(static_cast<uint16_t>(x >> 16)); };
            auto tg  = [&](const char* t) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(t[i])); };
            tg("RIFF"); p32(36 + static_cast<uint32_t>(v.size() * 2)); tg("WAVE"); tg("fmt "); p32(16); p16(1);
            p16(static_cast<uint16_t>(channels)); p32(44100); p32(44100u * static_cast<uint32_t>(channels) * 2);
            p16(static_cast<uint16_t>(channels * 2)); p16(16); tg("data"); p32(static_cast<uint32_t>(v.size() * 2));
            for (int16_t x : v) p16(static_cast<uint16_t>(x));
            std::FILE* f = std::fopen((dir + "/" + name).c_str(), "wb");
            std::fwrite(b.data(), 1, b.size(), f);
            std::fclose(f);
        };
        write("m.wav", 1, {1, 2, 3, 4});
        write("s.wav", 2, {10, -10, 20, -20, 30, -30});

        SampleBank           bank;
        DesktopSampleLibrary lib(dir, bank);
        CHECK(lib.count() == 2 && lib.find("s.wav") == 1, "la bibliothèque propose aussi les samples stéréo");
        const int16_t *m = nullptr, *s = nullptr;
        uint32_t       lm = 0, ls = 0;
        CHECK(lib.load(0, m, lm) && lm == 4 && lib.channels(m) == 1, "mono : 4 images, 1 canal");
        CHECK(lib.load(1, s, ls) && ls == 3 && lib.channels(s) == 2 && s[0] == 10 && s[1] == -10 && s[5] == -30,
              "stéréo : 3 images (et non 6), 2 canaux, entrelacé conservé");
        CHECK(lib.channels(nullptr) == 1 && lib.channels(m + 1) == 1, "pointeur inconnu : mono");
        lib.discard(s);
        CHECK(lib.channels(s) == 1 && bank.count() == 1, "après libération : pointeur inconnu");
        lib.discard(m);
        fs::remove_all(dir);
    }

    // ----- UI : les canaux accompagnent les commandes de sample -----
    {
        struct Sink : CommandSink {
            std::vector<Command> sent;
            bool push(const Command& c) override {
                sent.push_back(c);
                return true;
            }
        } sink;
        struct Lib : SampleLibrary {
            int16_t data[2][8] = {};
            int     count() const override { return 2; }
            const char* name(int i) const override { return i == 0 ? "mono.wav" : "stereo.wav"; }
            int  find(const char* f) const override { return std::strcmp(f, "mono.wav") == 0 ? 0 : std::strcmp(f, "stereo.wav") == 0 ? 1 : -1; }
            bool load(int i, const int16_t*& d, uint32_t& n) override {
                d = data[i];
                n = 4;
                return true;
            }
            int  channels(const int16_t* d) const override { return d == data[1] ? 2 : 1; }
            void discard(const int16_t*) override {}
        } lib;
        UiController ui(sink, &lib);
        // Ajout d'une piste avec le sample stéréo (deuxième ligne de la liste).
        ui.handle(InputEvent::wheel(1));  // « Ajouter piste »
        ui.handle(InputEvent::ok());
        ui.handle(InputEvent::wheel(1));
        ui.handle(InputEvent::ok());
        bool found = false;
        for (const Command& c : sink.sent)
            if (c.type == Command::SetTrackSample) found = c.channels == 2 && c.length == 4;
        CHECK(found, "ajout d'une piste : SetTrackSample porte 2 canaux et la longueur en images");
        // Écoute.
        sink.sent.clear();
        EngineState st;
        ui.handle(InputEvent::back());
        ui.handle(InputEvent::back());
        ui.handle(InputEvent::wheel(1));
        ui.handle(InputEvent::ok());
        ui.update(st, 0);
        ui.handle(InputEvent::wheel(1));
        ui.update(st, 1000);
        bool preview = false;
        for (const Command& c : sink.sent)
            if (c.type == Command::PreviewSample) preview = c.channels == 2;
        CHECK(preview, "écoute : PreviewSample porte 2 canaux");
    }

    // ===== S2b : balance de la piste, combinaison avec celle de la note, pan replié de la piste =====

    // ----- Combinaison : la note est une position entre la piste et les extrémités -----
    {
        struct Case { int track, note; float expected; };
        const Case cases[] = {{0, 0, 0},   {0, -50, -50}, {0, 50, 50},  {0, 20, 20},   {30, 0, 30},  {30, -50, -50}, {30, 50, 50}, {30, -25, -10},
                              {30, 25, 40}, {-30, 0, -30}, {-30, 50, 50}, {-30, -50, -50}, {-30, 25, 10}, {-30, -25, -40}, {50, -50, -50}, {50, 25, 50},
                              {50, -25, 0}, {-50, 50, 50}, {-50, -25, -50}, {99, 0, 50}, {0, 99, 50}, {-99, -99, -50}};
        bool ok = true;
        for (const Case& c : cases) ok = ok && near(dsp::combine_balance(c.track, c.note), c.expected, 1e-4f);
        CHECK(ok, "combinaison piste + note : exemples, extrémités atteintes depuis n'importe quelle piste, valeurs bornées");
        CHECK(near(dsp::combine_balance(30, -50), -50.0f, 1e-6f) && near(dsp::combine_balance(30, 0), 30.0f, 1e-6f), "note 0 = la piste, note -50 = tout à gauche");
    }

    // ----- Balance de la piste dans le moteur -----
    {
        auto with_balance = [](int balance, int note_pan, Out* out) {
            Engine e;
            e.push(set_sample(2, mono_impulse, 2));
            e.push(cmd(Command::SetTrackBalance, 2, 0, static_cast<float>(balance)));
            e.push(note(2, 0, note_pan));
            *out = render(e, 4);
        };
        Out o;
        with_balance(30, 0, &o);
        CHECK(near(o.l[0], out_of(0.5f * 0.4f)) && near(o.r[0], out_of(0.5f)), "piste R30, note 0 : R30 (gauche à 40 %)");
        with_balance(30, -50, &o);
        CHECK(near(o.l[0], out_of(0.5f)) && o.r[0] == 0.0f, "piste R30, note L50 : pur gauche, aucune composante droite");
        with_balance(30, 50, &o);
        CHECK(o.l[0] == 0.0f && near(o.r[0], out_of(0.5f)), "piste R30, note R50 : pur droite");
        with_balance(30, -25, &o);
        CHECK(near(o.l[0], out_of(0.5f)) && near(o.r[0], out_of(0.5f * 0.8f)), "piste R30, note L25 : L10 (droite à 80 %)");
        with_balance(-40, 25, &o);
        const float expected_pan = dsp::combine_balance(-40, 25);  // -40 + 25 x 90 / 50 = 5
        CHECK(near(expected_pan, 5.0f, 1e-5f) && near(o.l[0], out_of(0.5f * 0.9f)) && near(o.r[0], out_of(0.5f)), "piste L40, note R25 : R5");

        // Bornes et arrondi de la commande ; commandes invalides.
        with_balance(400, 0, &o);
        CHECK(o.l[0] == 0.0f && near(o.r[0], out_of(0.5f)), "SetTrackBalance borné à +50");
        with_balance(-400, 0, &o);
        CHECK(near(o.l[0], out_of(0.5f)) && o.r[0] == 0.0f, "SetTrackBalance borné à -50");
        with_balance(24, 0, &o);  // 24 : gauche à 52 %
        CHECK(near(o.l[0], out_of(0.5f * 0.52f)), "SetTrackBalance : valeur entière");
        {
            Engine e;
            e.push(set_sample(2, mono_impulse, 2));
            e.push(cmd(Command::SetTrackBalance, 2, 0, 24.6f));
            e.push(cmd(Command::SetTrackBalance, 200, 0, 50.0f));  // piste inexistante : ignorée
            e.push(note(2));
            o = render(e, 4);
            CHECK(near(o.l[0], out_of(0.5f * 0.5f)), "SetTrackBalance arrondi (24,6 -> 25), piste invalide ignorée");
        }

        // Chaque piste a sa balance.
        {
            Engine e;
            e.push(set_sample(2, mono_impulse, 2));
            e.push(set_sample(3, mono_impulse, 2));
            e.push(cmd(Command::SetTrackBalance, 2, 0, -50.0f));
            e.push(cmd(Command::SetTrackBalance, 3, 0, 50.0f));
            e.push(note(2));
            e.push(note(3));
            o = render(e, 4);
            CHECK(near(o.l[0], out_of(0.5f)) && near(o.r[0], out_of(0.5f)), "deux pistes, chacune de son côté : un côté chacune");
        }

        // ClearTrack et ClearAll remettent la balance et la largeur de la piste à 0.
        {
            Engine e;
            e.push(set_sample(2, stereo_impulse, 2, 2));
            e.push(cmd(Command::SetTrackBalance, 2, 0, -50.0f));
            e.push(cmd(Command::SetTrackWidth, 2, 0, -100.0f));
            e.push(cmd(Command::ClearTrack, 2));
            e.push(set_sample(2, stereo_impulse, 2, 2));
            e.push(note(2));
            o = render(e, 4);
            CHECK(near(o.l[0], out_of(0.5f)) && near(o.r[0], out_of(0.25f)), "ClearTrack : balance et largeur de la piste remises à 0");
            Engine f;
            f.push(set_sample(2, stereo_impulse, 2, 2));
            f.push(cmd(Command::SetTrackBalance, 2, 0, 50.0f));
            f.push(cmd(Command::SetTrackWidth, 2, 0, -100.0f));
            f.push(cmd(Command::ClearAll));
            f.push(set_sample(2, stereo_impulse, 2, 2));
            f.push(note(2));
            o = render(f, 4);
            CHECK(near(o.l[0], out_of(0.5f)) && near(o.r[0], out_of(0.25f)), "ClearAll : balance et largeur de la piste remises à 0");
        }

        // Pas, note d'essai, note décalée, note en avance : tous passent par la même combinaison.
        {
            Engine e;
            e.push(set_sample(2, mono_impulse, 2));
            e.push(cmd(Command::SetTrackBalance, 2, 0, 30.0f));
            e.push(cmd(Command::SetStepPan, 2, 5, -50.0f));
            e.push(cmd(Command::AuditionStep, 2, 5));
            o = render(e, 4);
            CHECK(near(o.l[0], out_of(0.5f)) && o.r[0] == 0.0f, "note d'essai d'un pas : combinaison avec la piste");

            Engine f;
            f.push(set_sample(2, mono_impulse, 2));
            f.push(cmd(Command::SetTrackBalance, 2, 0, -50.0f));
            f.push(cmd(Command::SetStep, 2, 4, 1.0f));
            f.push(cmd(Command::SetStepOffset, 2, 4, 32.0f));
            f.push(cmd(Command::SetStep, 2, 6, 1.0f));
            f.push(cmd(Command::SetStepOffset, 2, 6, -32.0f));
            f.push(cmd(Command::Play));
            o = render(f, 7 * 5513);
            const std::size_t late  = static_cast<std::size_t>(std::ceil(4 * L + 32.0 / 128.0 * L));
            const std::size_t early = static_cast<std::size_t>(std::ceil(6 * L - 32.0 / 128.0 * L));
            CHECK(near(o.l[late], out_of(0.5f)) && o.r[late] == 0.0f, "note décalée (en attente) : balance de la piste");
            CHECK(near(o.l[early], out_of(0.5f)) && o.r[early] == 0.0f, "note en avance : balance de la piste");
        }

        // La balance de la piste agit au déclenchement : une note en cours ne bouge pas, la suivante oui.
        {
            static int16_t held[300];
            for (int16_t& v : held) v = 16384;
            Engine e;
            e.push(set_sample(2, held, 300));
            e.push(cmd(Command::SetTrackRelease, 2, 0, 0.0f));  // coupe nette : on ne teste ici que la balance
            e.push(note(2));
            Out a = render(e, 50);
            e.push(cmd(Command::SetTrackBalance, 2, 0, -50.0f));
            Out b = render(e, 50);
            CHECK(near(a.l[10], out_of(0.5f)) && near(a.r[10], out_of(0.5f)) && near(b.l[10], out_of(0.5f)) && near(b.r[10], out_of(0.5f)),
                  "changer la balance de la piste pendant une note ne la déplace pas");
            e.push(note(2));
            Out c = render(e, 50);
            CHECK(near(c.l[10], out_of(0.5f)) && c.r[10] == 0.0f, "la note suivante prend la nouvelle balance");
        }

        // Prise : la frappe est rangée avec sa propre balance (0), pas combinée ; elle sonne combinée.
        {
            Engine e;
            e.push(set_sample(2, mono_impulse, 2));
            e.push(cmd(Command::SetTrackBalance, 2, 0, 50.0f));
            e.push(cmd(Command::StartRecord, 2));
            render(e, 88200 + 17916);
            e.push(note(2));
            o = render(e, 4);
            StepEvent ev;
            CHECK(e.pop_step_event(ev) && ev.main.pan == 0, "frappe de prise : balance de la note 0 (suit la piste)");
            CHECK(o.l[0] == 0.0f && near(o.r[0], out_of(0.5f)), "…et elle sonne avec la balance de la piste");
        }
    }

    // ----- Largeur stéréo de la piste : -100 = mono, 0 = inchangé, +100 = côtés doublés -----
    {
        auto wide = [](const int16_t* data, int channels, float width, int note_pan = 0) {
            Engine e;
            e.push(set_sample(2, data, 2, channels));
            e.push(cmd(Command::SetTrackWidth, 2, 0, width));
            e.push(note(2, 0, note_pan));
            return render(e, 4);
        };
        // Sample stéréo : G = 0,5, D = 0,25 -> milieu M = 0,375, côtés S = 0,125 ; G' = M + w S, D' = M - w S.
        Out o = wide(stereo_impulse, 2, 0);
        CHECK(near(o.l[0], out_of(0.5f)) && near(o.r[0], out_of(0.25f)), "largeur 0 : sample inchangé");
        o = wide(stereo_impulse, 2, -100);
        CHECK(near(o.l[0], out_of(0.375f)) && near(o.r[0], out_of(0.375f)) && o.l[0] == o.r[0], "100M : mono complet, les deux canaux valent la moyenne");
        o = wide(stereo_impulse, 2, -50);
        CHECK(near(o.l[0], out_of(0.4375f)) && near(o.r[0], out_of(0.3125f)), "50M : moitié des côtés (w = 0,5)");
        o = wide(stereo_impulse, 2, 50);
        CHECK(near(o.l[0], out_of(0.5625f)) && near(o.r[0], out_of(0.1875f)), "50W : côtés x1,5");
        o = wide(stereo_impulse, 2, 100);
        CHECK(near(o.l[0], out_of(0.625f)) && near(o.r[0], out_of(0.125f)), "100W : côtés doublés");
        o = wide(stereo_impulse, 2, 24.6f);
        CHECK(near(o.l[0], out_of(0.375f + 1.25f * 0.125f)) && near(o.r[0], out_of(0.375f - 1.25f * 0.125f)), "valeur arrondie (24,6 -> 25 : w = 1,25)");
        o = wide(stereo_impulse, 2, 999);
        CHECK(near(o.l[0], out_of(0.625f)) && near(o.r[0], out_of(0.125f)), "SetTrackWidth borné à +100");
        o = wide(stereo_impulse, 2, -999);
        CHECK(near(o.l[0], out_of(0.375f)) && near(o.r[0], out_of(0.375f)), "SetTrackWidth borné à -100");

        // Sans effet sur un sample mono (pas de côtés) ; L = R dans un fichier stéréo non plus.
        for (float w : {-100.0f, -40.0f, 60.0f, 100.0f}) {
            o = wide(mono_impulse, 1, w);
            CHECK(near(o.l[0], out_of(0.5f)) && o.l[0] == o.r[0], "sample mono : largeur sans effet");
        }
        static const int16_t same_lr[] = {12000, 12000, 0, 0};
        o = wide(same_lr, 2, 100);
        CHECK(near(o.l[0], out_of(12000.0f / 32768.0f)) && near(o.r[0], out_of(12000.0f / 32768.0f)), "stéréo à canaux identiques : sans effet");

        // La largeur agit avant la balance de la note.
        o = wide(stereo_impulse, 2, -100, 25);  // mono 0,375 puis balance R25 : gauche à moitié
        CHECK(near(o.l[0], out_of(0.375f * 0.5f)) && near(o.r[0], out_of(0.375f)), "mono puis balance de la note R25");
        o = wide(stereo_impulse, 2, 100, -50);  // 0,625 / 0,125 puis tout à gauche
        CHECK(near(o.l[0], out_of(0.625f)) && o.r[0] == 0.0f, "large puis balance L50 : la droite est coupée");

        // Un sample stéréo très latéral et large : le niveau dépasse 1 avant le limiteur de sortie, qui l'absorbe.
        static const int16_t hard_left[] = {32767, 0, 0, 0};
        o = wide(hard_left, 2, 100);
        CHECK(near(o.l[0], out_of(1.5f * 32767.0f / 32768.0f)) && o.r[0] < 0.0f, "côté doublé d'un son tout à gauche : G = 1,5, D négatif, limité par la sortie");

        // En direct : une note qui sonne encore suit la largeur.
        {
            static int16_t held[600];
            for (int i = 0; i < 300; ++i) {
                held[2 * i]     = 16384;
                held[2 * i + 1] = 8192;
            }
            Engine e;
            e.push(set_sample(2, held, 300, 2));
            e.push(note(2));
            Out a = render(e, 50);
            e.push(cmd(Command::SetTrackWidth, 2, 0, -100.0f));
            Out b = render(e, 50);
            CHECK(near(a.l[10], out_of(0.5f)) && near(a.r[10], out_of(0.25f)) && near(b.l[10], out_of(0.375f)) && near(b.r[10], out_of(0.375f)),
                  "largeur modifiée pendant une note : suivi en direct");
            e.push(cmd(Command::SetTrackWidth, 2, 0, 0.0f));
            Out c = render(e, 50);
            CHECK(near(c.l[10], out_of(0.5f)) && near(c.r[10], out_of(0.25f)), "retour à 0 en direct");
        }

        // Chaque piste sa largeur ; le kick synthétique, l'aperçu et une piste invalide l'ignorent.
        {
            Engine e;
            e.push(set_sample(2, stereo_impulse, 2, 2));
            e.push(set_sample(3, stereo_impulse, 2, 2));
            e.push(cmd(Command::SetTrackWidth, 3, 0, -100.0f));
            e.push(cmd(Command::SetTrackWidth, 200, 0, -100.0f));  // piste inexistante : ignorée
            e.push(note(2));
            Out a = render(e, 4);
            CHECK(near(a.l[0], out_of(0.5f)) && near(a.r[0], out_of(0.25f)), "une autre piste en mono : sans effet sur celle-ci");
            Engine f;
            f.push(cmd(Command::SetTrackWidth, 0, 0, -100.0f));
            f.push(note(0));
            Engine g;
            g.push(note(0));
            Out kf = render(f, 300), kg = render(g, 300);
            CHECK(kf.l == kg.l && kf.r == kg.r, "kick synthétique : la largeur de la piste est sans effet");
            Engine h;
            h.push(cmd(Command::SetTrackWidth, 0, 0, -100.0f));
            Command pv = cmd(Command::PreviewSample);
            pv.data     = stereo_impulse;
            pv.length   = 2;
            pv.channels = 2;
            h.push(pv);
            Out ph = render(h, 4);
            CHECK(near(ph.l[0], out_of(0.5f)) && near(ph.r[0], out_of(0.25f)), "aperçu : sans largeur de piste");
        }
    }

    if (failures == 0) std::printf("OK : test_stereo\n");
    return failures == 0 ? 0 : 1;
}
