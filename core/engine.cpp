#include "engine.h"

#include "mixer.h"

Engine::Engine()
    : kick_(static_cast<float>(cfg::SAMPLE_RATE)),
      seq_(static_cast<float>(cfg::SAMPLE_RATE)) {
    for (float& g : gains_) g = 1.0f;
    for (auto& n : state_.triggers) n.store(0);
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
    state_.triggers[track].fetch_add(1, std::memory_order_relaxed);
}

// Le moteur n'utilisera plus le buffer de cette piste : on le rend à l'UI.
void Engine::release_pointer(const int16_t*& data) {
    if (!data) return;
    released_.push(data);  // file de 64 : l'UI la vide régulièrement
    data = nullptr;
}

void Engine::release_sample(int track) {
    release_pointer(track_sample_[track]);
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
        case Command::SetTrackPitch:
            if (track_ok) players_[c.track].set_pitch(c.value);
            break;
        case Command::SetTrackSample:
            if (track_ok && c.data && c.length > 0) {
                // L'ancien buffer n'est plus lu après set_sample ci-dessous (sauf si c'est le même).
                if (track_sample_[c.track] != c.data) release_sample(c.track);
                players_[c.track].set_sample(c.data, c.length);
                track_sample_[c.track] = c.data;
                voices_[c.track]       = &players_[c.track];
            }
            break;
        case Command::ClearTrack:
            if (track_ok) {
                voices_[c.track] = nullptr;
                players_[c.track].set_sample(nullptr, 0);
                players_[c.track].set_pitch(1.0f);
                release_sample(c.track);
                gains_[c.track] = 1.0f;
                for (int s = 0; s < cfg::MAX_STEPS; ++s) seq_.pattern().on[c.track][s] = 0;
            }
            break;
        case Command::PreviewSample:
            if (c.data && c.length > 0) {
                if (preview_sample_ != c.data) release_pointer(preview_sample_);
                preview_.set_sample(c.data, c.length);
                preview_.trigger(1.0f);
                preview_sample_ = c.data;
            }
            break;
        case Command::StopPreview:
            preview_.set_sample(nullptr, 0);  // plus aucune lecture du buffer
            release_pointer(preview_sample_);
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

    if (preview_.active()) {
        for (int i = 0; i < frames; ++i) scratch_[i] = 0.0f;
        preview_.render(scratch_, frames);
        mixer::accumulate(left, scratch_, frames, 1.0f);
    }

    mixer::master(left, frames, cfg::MASTER_GAIN);

    for (int i = 0; i < frames; ++i) right[i] = left[i];
}
