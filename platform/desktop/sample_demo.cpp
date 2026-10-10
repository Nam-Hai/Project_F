// Rendu hors ligne d'un sample à plusieurs pitchs (1.0, 0.5, 2.0), pour l'écouter dans un WAV.
// Usage : sample_demo samples/mon_kick.wav [sortie.wav]
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../../config.h"
#include "../../core/engine.h"
#include "../../core/sample_player.h"
#include "sample_loader.h"
#include "wav_writer.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage : %s samples/fichier.wav [sortie.wav]\n", argv[0]);
        return 1;
    }
    const char* out_path = (argc > 2) ? argv[2] : "sample_test.wav";

    std::vector<int16_t> data;
    std::string          error;
    if (!load_wav_mono(argv[1], data, error)) {
        std::fprintf(stderr, "%s : %s\n", argv[1], error.c_str());
        return 1;
    }
    std::printf("%s chargé : %zu échantillons (%.2f s)\n", argv[1], data.size(),
                static_cast<double>(data.size()) / cfg::SAMPLE_RATE);

    SamplePlayer player;
    player.set_sample(data.data(), static_cast<uint32_t>(data.size()));

    Engine engine;
    engine.set_voice(1, &player);

    const float pitches[]     = {1.0f, 0.5f, 2.0f};
    const int   slot_frames   = cfg::SAMPLE_RATE * 2;  // 2 s par pitch
    const int   total_frames  = slot_frames * 3;

    std::vector<float> left(total_frames), right(total_frames);
    for (int p = 0; p < 3; ++p) {
        player.set_pitch(pitches[p]);
        engine.trigger(1);
        engine.render(left.data() + p * slot_frames, right.data() + p * slot_frames, slot_frames);
    }

    float peak = 0.0f;
    for (float s : left) peak = std::fmax(peak, std::fabs(s));

    if (!write_wav_stereo16(out_path, left.data(), right.data(), total_frames, cfg::SAMPLE_RATE)) {
        std::fprintf(stderr, "Impossible d'écrire %s\n", out_path);
        return 1;
    }
    std::printf("%s écrit (pitch 1.0, 0.5, 2.0 ; pic = %.3f)\n", out_path, peak);
    return 0;
}
