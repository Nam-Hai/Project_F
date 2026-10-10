// Banc d'essai du moteur dans son pire cas : 16 pistes stéréo de 5 s, tous les pas allumés, 200 BPM, Release de 2 s
// (queues pleines), largeur et enveloppe actives, décalages. Affiche le temps de rendu par bloc de 128 échantillons.
// Sert de référence pour le budget CPU (plan_teensy.md, T1) : le budget d'un bloc est de 2 903 us à 44,1 kHz.
//
//   cmake --build build --target bench_engine && ./build/bench_engine
#include <chrono>
#include <cstdio>
#include <vector>

#include "../core/engine.h"

int main() {
    static int16_t buf[2 * 220500];
    for (int i = 0; i < 220500; ++i) {
        buf[2 * i]     = static_cast<int16_t>(8000 * ((i * 7) % 200 - 100) / 100);
        buf[2 * i + 1] = static_cast<int16_t>(buf[2 * i] / 2);
    }

    Engine             e;
    std::vector<float> l(128), r(128);
    auto push = [&](const Command& c) {  // la file est petite : on rend un bloc quand elle est pleine
        while (!e.push(c)) e.render(l.data(), r.data(), 128);
    };
    auto cmd = [&](Command::Type type, int track, float value, int step = 0) {
        Command c;
        c.type  = type;
        c.track = static_cast<uint8_t>(track);
        c.step  = static_cast<uint8_t>(step);
        c.value = value;
        push(c);
    };

    for (int t = 0; t < 16; ++t) {
        Command c;
        c.type     = Command::SetTrackSample;
        c.track    = static_cast<uint8_t>(t);
        c.data     = buf;
        c.length   = 220500;
        c.channels = 2;
        push(c);
        cmd(Command::SetTrackRelease, t, 2000);
        cmd(Command::SetTrackWidth, t, 50);
        cmd(Command::SetTrackAttack, t, 5);
        cmd(Command::SetTrackDecay, t, 50);
        Command p;
        p.type   = Command::SetTrackPattern;
        p.track  = static_cast<uint8_t>(t);
        p.length = 0xFFFF;
        push(p);
        if (t % 3 == 0)
            for (int s = 0; s < 16; ++s) cmd(Command::SetStepOffset, t, 20, s);
    }
    cmd(Command::SetBpm, 0, 200);
    cmd(Command::Play, 0, 0);

    const int blocks = 44100 * 20 / 128;  // 20 s d'audio
    for (int i = 0; i < 40; ++i) e.render(l.data(), r.data(), 128);  // chauffe

    double sum = 0.0, worst = 0.0;
    for (int b = 0; b < blocks; ++b) {
        const auto t0 = std::chrono::steady_clock::now();
        e.render(l.data(), r.data(), 128);
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        sum += us;
        if (us > worst) worst = us;
    }
    std::printf("bloc de 128 échantillons : moyenne %.1f us, pire %.1f us (budget 2903 us) : %.1f %% / %.1f %%\n", sum / blocks, worst,
                100.0 * sum / blocks / 2903.0, 100.0 * worst / 2903.0);
    return 0;
}
