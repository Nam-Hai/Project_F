#include "default_loop.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "../../config.h"
#include "default_pattern.h"

void send_command(CommandSink& sink, const Command& cmd) {
    while (!sink.push(cmd)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

bool load_default_loop(CommandSink& sink, SampleBank& bank, UiController* ui) {
    constexpr int track_count = static_cast<int>(sizeof(DEFAULT_TRACKS) / sizeof(DEFAULT_TRACKS[0]));
    static_assert(track_count <= cfg::MAX_TRACKS, "trop de pistes dans DEFAULT_TRACKS");

    send_command(sink, {Command::SetBpm, 0, 0, DEFAULT_BPM});

    for (int t = 0; t < track_count; ++t) {
        const TrackSetup& setup = DEFAULT_TRACKS[t];
        const uint8_t     track = static_cast<uint8_t>(t);

        const std::string path   = std::string(SAMPLES_DIR) + "/" + setup.sample;
        const int16_t*    data   = nullptr;
        uint32_t          length = 0;
        std::string       error;
        if (!bank.load(path.c_str(), data, length, error)) {
            std::fprintf(stderr, "Piste %d (%s) : %s\n", t, setup.sample, error.c_str());
            return false;
        }

        Command sample_cmd;
        sample_cmd.type   = Command::SetTrackSample;
        sample_cmd.track  = track;
        sample_cmd.data   = data;
        sample_cmd.length = length;
        sample_cmd.channels = static_cast<uint8_t>(bank.channels(data));
        send_command(sink, sample_cmd);

        send_command(sink, {Command::SetTrackPitch, track, 0, setup.pitch});
        send_command(sink, {Command::SetTrackGain, track, 0, setup.gain});

        const int len = static_cast<int>(std::strlen(setup.pattern));
        if (len > cfg::MAX_STEPS)
            std::fprintf(stderr, "Piste %d : pattern de %d pas, seuls les %d premiers sont utilisés\n",
                         t, len, cfg::MAX_STEPS);

        uint16_t bits = 0;
        for (int s = 0; s < len && s < cfg::MAX_STEPS; ++s) {
            if (setup.pattern[s] == 'x' || setup.pattern[s] == 'X') {
                send_command(sink, {Command::SetStep, track, static_cast<uint8_t>(s), 1.0f});
                bits = static_cast<uint16_t>(bits | (1u << s));
            }
        }

        if (ui) ui->add_track(setup.sample, track, bits);
        std::printf("  piste %d  %-14s %s\n", t, setup.sample, setup.pattern);
    }
    return true;
}
