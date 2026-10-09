// Drum machine PC sans interface : joue en boucle la loop définie dans default_pattern.h.
// (Avec l'interface simulée : ./build/emulator)
#include <cstdio>

#include "../../core/engine.h"
#include "audio_miniaudio.h"
#include "default_loop.h"
#include "default_pattern.h"
#include "sample_bank.h"

static void render_cb(float* left, float* right, int frames, void* user) {
    static_cast<Engine*>(user)->render(left, right, frames);
}

int main() {
    Engine       engine;
    SampleBank   bank;
    DesktopAudio audio;  // déclaré en dernier : détruit en premier, avant le moteur et les buffers

    // Le séquenceur est à l'arrêt tant qu'on n'envoie pas Play : l'audio tourne en silence
    // pendant qu'on charge, exactement comme quand l'UI ajoutera des pistes en direct.
    if (!audio.start(render_cb, &engine)) return 1;
    if (!load_default_loop(engine, bank, nullptr)) return 1;
    send_command(engine, {Command::Play, 0, 0, 0.0f});

    std::printf("%.0f BPM - lecture en cours. Entrée pour quitter.\n", DEFAULT_BPM);
    std::getchar();

    audio.stop();  // plus aucun callback avant de détruire le moteur et les buffers
    return 0;
}
