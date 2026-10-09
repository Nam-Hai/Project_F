#include "engine.h"

#include "mixer.h"

Engine::Engine()
    : kick_(static_cast<float>(cfg::SAMPLE_RATE)),
      seq_(static_cast<float>(cfg::SAMPLE_RATE)) {
    for (float& g : gains_) g = 1.0f;
    voices_[0] = &kick_;
    state_.bpm.store(seq_.bpm());
}

void Engine::set_voice(int track, Voice* voice) {
    if (track < 0 || track >= cfg::MAX_TRACKS) return;
    voices_[track] = voice;
}

void Engine::trigger(int track, float velocity) {
    if (track < 0 || track >= cfg::MAX_TRACKS || !voices_[track]) return;
    voices_[track]->trigger(velocity);
}

void Engine::apply(const Command& c) {
    const bool track_ok = c.track < cfg::MAX_TRACKS;
    const bool step_ok  = c.step < cfg::MAX_STEPS;

    switch (c.type) {
        case Command::SetBpm:
            seq_.set_bpm(c.value);
            state_.bpm.store(seq_.bpm());
            break;
        case Command::SetStep:
            if (track_ok && step_ok) seq_.pattern().on[c.track][c.step] = c.value > 0.5f;
            break;
        case Command::ToggleStep:
            if (track_ok && step_ok) seq_.pattern().on[c.track][c.step] ^= 1;
            break;
        case Command::SetTrackGain:
            if (track_ok) gains_[c.track] = c.value;
            break;
        case Command::Play:
            seq_.play();
            state_.playing.store(true);
            break;
        case Command::Stop:
            seq_.stop();
            state_.playing.store(false);
            break;
        case Command::Trigger:
            trigger(c.track);
            break;
    }
}

void Engine::fire_step() {
    const int step = seq_.fire();
    for (int t = 0; t < cfg::MAX_TRACKS; ++t) {
        if (seq_.pattern().on[t][step]) trigger(t);
    }
    state_.step.store(step);
}

void Engine::render(float* left, float* right, int frames) {
    Command cmd;
    while (commands_.pop(cmd)) apply(cmd);

    // On découpe le rendu aux frontières de pas : le trig tombe à l'échantillon exact.
    for (int done = 0; done < frames;) {
        while (seq_.due()) fire_step();

        int n = frames - done;
        const int32_t until = seq_.frames_until_due();
        if (until < n) n = until;

        render_audio(left + done, right + done, n);
        seq_.advance(n);
        done += n;
    }
}

void Engine::render_audio(float* left, float* right, int frames) {
    for (int done = 0; done < frames;) {
        const int n = (frames - done < cfg::BLOCK) ? frames - done : cfg::BLOCK;
        render_block(left + done, right + done, n);
        done += n;
    }
}

void Engine::render_block(float* left, float* right, int frames) {
    for (int i = 0; i < frames; ++i) left[i] = 0.0f;

    for (int t = 0; t < cfg::MAX_TRACKS; ++t) {
        if (!voices_[t]) continue;
        for (int i = 0; i < frames; ++i) scratch_[i] = 0.0f;
        voices_[t]->render(scratch_, frames);
        mixer::accumulate(left, scratch_, frames, gains_[t]);
    }

    mixer::master(left, frames, cfg::MASTER_GAIN);

    for (int i = 0; i < frames; ++i) right[i] = left[i];
}
