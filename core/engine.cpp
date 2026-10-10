#include "engine.h"

#include <cmath>

#include "dsp_utils.h"
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

void Engine::trigger(int track, float velocity, float pitch_ratio) {
    if (track < 0 || track >= cfg::MAX_TRACKS || !voices_[track]) return;
    voices_[track]->trigger_pitched(velocity, pitch_ratio);
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

// Supprime une piste : plus de voix, sample rendu à l'UI, pattern / gain / pitch remis à zéro.
void Engine::clear_track(int track) {
    voices_[track] = nullptr;
    players_[track].set_sample(nullptr, 0);
    players_[track].set_pitch(1.0f);
    release_sample(track);
    gains_[track] = 1.0f;
    seq_.pattern().reset_track(track);  // pas, vélocités et pitchs par défaut
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
        case Command::SetTrackPattern:
            if (track_ok) {
                for (int s = 0; s < cfg::MAX_STEPS; ++s)
                    seq_.pattern().on[c.track][s] = static_cast<uint8_t>((c.length >> s) & 1u);
            }
            break;
        case Command::SetStepVelocity:
            if (track_ok && step_ok) {
                int v = static_cast<int>(std::lround(c.value));
                if (v < cfg::VELOCITY_MIN) v = cfg::VELOCITY_MIN;
                if (v > cfg::VELOCITY_MAX) v = cfg::VELOCITY_MAX;
                seq_.pattern().velocity[c.track][c.step] = static_cast<uint8_t>(v);
            }
            break;
        case Command::SetStepPitch:
            if (track_ok && step_ok) {
                int v = static_cast<int>(std::lround(c.value));
                if (v < cfg::STEP_PITCH_MIN) v = cfg::STEP_PITCH_MIN;
                if (v > cfg::STEP_PITCH_MAX) v = cfg::STEP_PITCH_MAX;
                seq_.pattern().pitch[c.track][c.step] = static_cast<int8_t>(v);
            }
            break;
        case Command::SetStepOffset:
            if (track_ok && step_ok) {
                int v = static_cast<int>(std::lround(c.value));
                if (v < cfg::OFFSET_MIN) v = cfg::OFFSET_MIN;
                if (v > cfg::OFFSET_MAX) v = cfg::OFFSET_MAX;
                seq_.pattern().offset[c.track][c.step] = static_cast<int8_t>(v);
            }
            break;
        case Command::SetExtraHit:
            if (track_ok && step_ok && c.slot >= 1 && c.slot <= cfg::MAX_EXTRA_HITS) {
                int v = static_cast<int>(std::lround(c.value));
                if (v < cfg::VELOCITY_MIN) v = cfg::VELOCITY_MIN;
                if (v > cfg::VELOCITY_MAX) v = cfg::VELOCITY_MAX;
                int p = c.pitch;
                if (p < cfg::STEP_PITCH_MIN) p = cfg::STEP_PITCH_MIN;
                if (p > cfg::STEP_PITCH_MAX) p = cfg::STEP_PITCH_MAX;
                int o = c.offset;
                if (o < cfg::OFFSET_MIN) o = cfg::OFFSET_MIN;
                if (o > cfg::OFFSET_MAX) o = cfg::OFFSET_MAX;
                Hit& h = seq_.pattern().extra[c.track][c.step][c.slot - 1];
                h.offset   = static_cast<int8_t>(o);
                h.velocity = static_cast<uint8_t>(v);
                h.pitch    = static_cast<int8_t>(p);
                if (seq_.pattern().extra_count[c.track][c.step] < c.slot) seq_.pattern().extra_count[c.track][c.step] = c.slot;
            }
            break;
        case Command::ClearExtraHits:
            if (track_ok && step_ok) {
                seq_.pattern().extra_count[c.track][c.step] = 0;
                for (Hit& h : seq_.pattern().extra[c.track][c.step]) h = Hit{};
            }
            break;
        case Command::SetTrackQuantize:
            if (track_ok) {
                int v = static_cast<int>(std::lround(c.value));
                if (v < 0) v = 0;
                if (v > cfg::QUANTIZE_MAX) v = cfg::QUANTIZE_MAX;
                seq_.pattern().quantize[c.track] = static_cast<uint8_t>(v);
            }
            break;
        case Command::AuditionStep:
            if (track_ok && step_ok) play_step(c.track, c.step);
            break;
        case Command::Note:
            if (track_ok) {
                // Joue la piste tout de suite, à la vélocité et au pitch demandés : indépendant du pattern et
                // du séquenceur (clavier de piano).
                int v = static_cast<int>(std::lround(c.value));
                if (v < cfg::VELOCITY_MIN) v = cfg::VELOCITY_MIN;
                if (v > cfg::VELOCITY_MAX) v = cfg::VELOCITY_MAX;
                int p = c.pitch;
                if (p < cfg::STEP_PITCH_MIN) p = cfg::STEP_PITCH_MIN;
                if (p > cfg::STEP_PITCH_MAX) p = cfg::STEP_PITCH_MAX;
                trigger(c.track, static_cast<float>(v) / static_cast<float>(cfg::VELOCITY_MAX), dsp::semitone_ratio(p));
            }
            break;
        case Command::ClearTrack:
            if (track_ok) clear_track(c.track);
            break;
        case Command::ClearAll:
            for (int t = 0; t < cfg::MAX_TRACKS; ++t) clear_track(t);
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
            pending_count_ = 0;
            just_started_  = true;
            state_.playing.store(true);
            break;
        case Command::Stop:
            seq_.stop();
            pending_count_ = 0;  // les notes déjà programmées ne partent plus
            state_.playing.store(false);
            break;
        case Command::Trigger:
            trigger(c.track);
            break;
    }
}

// Déclenche la note d'un pas avec sa vélocité et son pitch.
void Engine::play_step(int track, int step) {
    const Pattern& p = seq_.pattern();
    trigger(track, static_cast<float>(p.velocity[track][step]) / static_cast<float>(cfg::VELOCITY_MAX),
            dsp::semitone_ratio(p.pitch[track][step]));
}

// Programme une note à `countdown` échantillons (mesuré depuis l'échantillon courant). Si le tableau est
// plein (ce qui ne devrait pas arriver), la note est abandonnée : aucune allocation dans le callback audio.
void Engine::schedule(double countdown, int track, int velocity, int pitch) {
    if (pending_count_ >= MAX_PENDING) return;
    Pending& p = pending_[pending_count_++];
    p.countdown = countdown;
    p.track     = static_cast<uint8_t>(track);
    p.velocity  = static_cast<uint8_t>(velocity);
    p.pitch     = static_cast<int8_t>(pitch);
}

// Joue les notes en attente dont l'heure est venue.
void Engine::fire_pending() {
    int kept = 0;
    for (int i = 0; i < pending_count_; ++i) {
        const Pending p = pending_[i];
        if (p.countdown <= 0.0) {
            trigger(p.track, static_cast<float>(p.velocity) / static_cast<float>(cfg::VELOCITY_MAX), dsp::semitone_ratio(p.pitch));
        } else {
            pending_[kept++] = p;
        }
    }
    pending_count_ = kept;
}

// Frontière d'un pas. Chaque note jouée d'un pas a un décalage (en 1/128 de pas, réduit par la force de
// quantification de la piste) :
//  - décalage nul : elle part tout de suite ;
//  - décalage positif : elle est programmée pour plus tard dans ce pas ;
//  - décalage négatif : elle sonne AVANT la frontière de son pas, donc elle a été programmée à la frontière
//    du pas précédent (le pattern est circulaire : le pas 0 est précédé du pas 15). Au tout premier tour il
//    n'y a pas de pas précédent : ces notes du pas 0 partent tout de suite.
void Engine::fire_step() {
    const double lateness = seq_.countdown();   // <= 0 : l'instant exact de la frontière est un peu avant
    const int    step     = seq_.fire();
    const double length   = seq_.step_length();  // durée du pas qui commence
    const int    next     = (step + 1) % cfg::MAX_STEPS;
    const Pattern& pat    = seq_.pattern();

    for (int t = 0; t < cfg::MAX_TRACKS; ++t) {
        const double keep = static_cast<double>(cfg::QUANTIZE_MAX - pat.quantize[t]) / cfg::QUANTIZE_MAX;

        // Les notes du pas qui commence.
        if (pat.on[t][step]) {
            const int hits = 1 + pat.extra_count[t][step];
            for (int h = 0; h < hits; ++h) {
                const int off = h == 0 ? pat.offset[t][step] : pat.extra[t][step][h - 1].offset;
                const int vel = h == 0 ? pat.velocity[t][step] : pat.extra[t][step][h - 1].velocity;
                const int pit = h == 0 ? pat.pitch[t][step] : pat.extra[t][step][h - 1].pitch;

                const double shift = static_cast<double>(off) * keep;  // en 1/128 de pas, signe conservé
                if (shift == 0.0 || (shift < 0.0 && just_started_ && step == 0)) {
                    trigger(t, static_cast<float>(vel) / static_cast<float>(cfg::VELOCITY_MAX), dsp::semitone_ratio(pit));
                } else if (shift > 0.0) {
                    schedule(lateness + shift / cfg::OFFSET_UNITS * length, t, vel, pit);
                }
                // décalage négatif : déjà programmé à la frontière précédente
            }
        }

        // Les notes en avance du pas suivant : elles sonnent avant sa frontière.
        if (pat.on[t][next]) {
            const int hits = 1 + pat.extra_count[t][next];
            for (int h = 0; h < hits; ++h) {
                const int off = h == 0 ? pat.offset[t][next] : pat.extra[t][next][h - 1].offset;
                const double shift = static_cast<double>(off) * keep;
                if (shift >= 0.0) continue;
                const int vel = h == 0 ? pat.velocity[t][next] : pat.extra[t][next][h - 1].velocity;
                const int pit = h == 0 ? pat.pitch[t][next] : pat.extra[t][next][h - 1].pitch;
                schedule(lateness + length + shift / cfg::OFFSET_UNITS * length, t, vel, pit);
            }
        }
    }
    just_started_ = false;
    state_.step.store(step);
}

void Engine::render(float* left, float* right, int frames) {
    Command cmd;
    while (commands_.pop(cmd)) apply(cmd);

    // On découpe le rendu aux frontières de pas et aux échéances des notes en attente : chaque note part
    // à l'échantillon exact, même quand elle est décalée par rapport à son pas.
    for (int done = 0; done < frames;) {
        while (seq_.due()) fire_step();
        fire_pending();

        int n = frames - done;
        const int32_t until = seq_.frames_until_due();
        if (until < n) n = until;
        for (int i = 0; i < pending_count_; ++i) {
            const int32_t when = static_cast<int32_t>(std::ceil(pending_[i].countdown));  // >= 1 : les échues sont parties
            if (when < n) n = when;
        }

        render_audio(left + done, right + done, n);
        seq_.advance(n);
        for (int i = 0; i < pending_count_; ++i) pending_[i].countdown -= n;
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
