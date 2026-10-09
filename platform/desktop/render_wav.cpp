// Étapes 2-3 : rendu hors ligne du kick dans un WAV (aucune carte son nécessaire).
// Usage : render_wav [sortie.wav]
#include <cmath>
#include <cstdio>
#include <vector>

#include "../../config.h"
#include "../../core/engine.h"
#include "wav_writer.h"

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "kick_test.wav";

    const int seconds        = 4;
    const int total_frames   = seconds * cfg::SAMPLE_RATE;
    const int trigger_period = cfg::SAMPLE_RATE / 2;  // un kick toutes les 0,5 s

    std::vector<float> left(total_frames), right(total_frames);

    Engine engine;
    int    pos = 0;
    while (pos < total_frames) {
        if (pos % trigger_period == 0) engine.trigger(0);

        // On s'arrête juste avant le prochain trig pour qu'il tombe à l'échantillon près.
        const int to_next = trigger_period - (pos % trigger_period);
        const int n       = (total_frames - pos < to_next) ? total_frames - pos : to_next;
        engine.render(left.data() + pos, right.data() + pos, n);
        pos += n;
    }

    float peak = 0.0f;
    for (float s : left) peak = std::fmax(peak, std::fabs(s));

    if (!write_wav_stereo16(path, left.data(), right.data(), total_frames, cfg::SAMPLE_RATE)) {
        std::fprintf(stderr, "Impossible d'écrire %s\n", path);
        return 1;
    }
    std::printf("%s écrit (%d s, pic = %.3f)\n", path, seconds, peak);
    return 0;
}
