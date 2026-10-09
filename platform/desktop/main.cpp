// Drum machine PC : joue en boucle la loop définie dans default_pattern.h.
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../../config.h"
#include "../../core/engine.h"
#include "../../core/sample_player.h"
#include "../../core/synth_kick.h"
#include "audio_miniaudio.h"
#include "default_pattern.h"
#include "sample_loader.h"

static void render_cb(float* left, float* right, int frames, void* user) {
    static_cast<Engine*>(user)->render(left, right, frames);
}

int main() {
    constexpr int track_count = static_cast<int>(sizeof(DEFAULT_TRACKS) / sizeof(DEFAULT_TRACKS[0]));
    static_assert(track_count <= cfg::MAX_TRACKS, "trop de pistes dans DEFAULT_TRACKS");

    Engine engine;

    // Les voix et les données des samples vivent aussi longtemps que le moteur.
    std::vector<std::unique_ptr<Voice>>    voices;
    std::vector<std::vector<int16_t>>      sample_data(track_count);

    for (int t = 0; t < track_count; ++t) {
        const TrackSetup& setup = DEFAULT_TRACKS[t];

        if (std::strcmp(setup.sample, "synth:kick") == 0) {
            voices.push_back(std::make_unique<SynthKick>(static_cast<float>(cfg::SAMPLE_RATE)));
        } else {
            const std::string path = std::string(SAMPLES_DIR) + "/" + setup.sample;
            std::string       error;
            if (!load_wav_mono16(path.c_str(), sample_data[t], error)) {
                std::fprintf(stderr, "Piste %d (%s) : %s\n", t, setup.sample, error.c_str());
                return 1;
            }
            auto player = std::make_unique<SamplePlayer>();
            player->set_sample(sample_data[t].data(), static_cast<uint32_t>(sample_data[t].size()));
            player->set_pitch(setup.pitch);
            voices.push_back(std::move(player));
        }
        engine.set_voice(t, voices.back().get());
    }

    // Le pattern initial passe par la file de commandes, comme le fera l'UI.
    engine.push({Command::SetBpm, 0, 0, DEFAULT_BPM});

    for (int t = 0; t < track_count; ++t) {
        const TrackSetup& setup = DEFAULT_TRACKS[t];
        engine.push({Command::SetTrackGain, static_cast<uint8_t>(t), 0, setup.gain});

        const int len = static_cast<int>(std::strlen(setup.pattern));
        if (len > cfg::MAX_STEPS)
            std::fprintf(stderr, "Piste %d : pattern de %d pas, seuls les %d premiers sont utilisés\n",
                         t, len, cfg::MAX_STEPS);

        for (int s = 0; s < len && s < cfg::MAX_STEPS; ++s) {
            if (setup.pattern[s] == 'x' || setup.pattern[s] == 'X')
                engine.push({Command::SetStep, static_cast<uint8_t>(t), static_cast<uint8_t>(s), 1.0f});
        }
        std::printf("  piste %d  %-14s %s\n", t, setup.sample, setup.pattern);
    }
    engine.push({Command::Play, 0, 0, 0.0f});

    DesktopAudio audio;
    if (!audio.start(render_cb, &engine)) return 1;

    std::printf("%.0f BPM - lecture en cours. Entrée pour quitter.\n", DEFAULT_BPM);
    std::getchar();
    return 0;
}
