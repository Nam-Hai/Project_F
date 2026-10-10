#include "engine.h"

#include <cmath>

#include "dsp_utils.h"
#include "mixer.h"

Engine::Engine()
    : click_(static_cast<float>(cfg::SAMPLE_RATE)),
      kick_(static_cast<float>(cfg::SAMPLE_RATE)),
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

void Engine::trigger(int track, float velocity, float pitch_ratio, int pan) {
    if (track < 0 || track >= cfg::MAX_TRACKS || !voices_[track]) return;
    // La balance de la note est une position entre celle de la piste et les extrémités.
    voices_[track]->trigger_panned(velocity, pitch_ratio, dsp::combine_balance(track_balance_[track], pan));
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
    players_[track].set_sample(nullptr, 0, 1);
    players_[track].set_pitch(1.0f);
    release_sample(track);
    gains_[track] = 1.0f;
    players_[track].set_stereo_width(0);
    env_[track] = Envelope{};
    players_[track].set_envelope(env_[track].attack, env_[track].decay, env_[track].sustain, env_[track].release);
    track_balance_[track] = 0;
    seq_.pattern().reset_track(track);  // pas, vélocités et pitchs par défaut
}

// Change un des quatre réglages de l'enveloppe d'une piste (arrondi, borné à 0..max) et les transmet au lecteur.
void Engine::set_envelope(int track, int Envelope::*field, float value, int max) {
    int v = static_cast<int>(std::lround(value));
    if (v < 0) v = 0;
    if (v > max) v = max;
    env_[track].*field = v;
    players_[track].set_envelope(env_[track].attack, env_[track].decay, env_[track].sustain, env_[track].release);
}

static int clamp_pan(int pan) {
    return pan < cfg::PAN_MIN ? cfg::PAN_MIN : pan > cfg::PAN_MAX ? cfg::PAN_MAX : pan;
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
                players_[c.track].set_sample(c.data, c.length, c.channels);
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
        case Command::SetTrackWidth: {
            int v = static_cast<int>(std::lround(c.value));
            if (v < cfg::WIDTH_MIN) v = cfg::WIDTH_MIN;
            if (v > cfg::WIDTH_MAX) v = cfg::WIDTH_MAX;
            if (track_ok) players_[c.track].set_stereo_width(v);
            break;
        }
        case Command::SetTrackAttack:
            if (track_ok) set_envelope(c.track, &Envelope::attack, c.value, cfg::ENV_MS_MAX);
            break;
        case Command::SetTrackDecay:
            if (track_ok) set_envelope(c.track, &Envelope::decay, c.value, cfg::ENV_MS_MAX);
            break;
        case Command::SetTrackSustain:
            if (track_ok) set_envelope(c.track, &Envelope::sustain, c.value, 100);
            break;
        case Command::SetTrackRelease:
            if (track_ok) set_envelope(c.track, &Envelope::release, c.value, cfg::ENV_MS_MAX);
            break;
        case Command::SetTrackBalance:
            if (track_ok) track_balance_[c.track] = static_cast<int8_t>(clamp_pan(static_cast<int>(std::lround(c.value))));
            break;
        case Command::SetStepPan:
            if (track_ok && step_ok) seq_.pattern().pan[c.track][c.step] = static_cast<int8_t>(clamp_pan(static_cast<int>(std::lround(c.value))));
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
                h.pan      = static_cast<int8_t>(clamp_pan(c.pan));
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
                const int pan = clamp_pan(c.pan);
                trigger(c.track, static_cast<float>(v) / static_cast<float>(cfg::VELOCITY_MAX), dsp::semitone_ratio(p), pan);
                if (phase_ != Phase::Idle && c.track == record_track_) record_note(v, p, pan);
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
                preview_.set_sample(c.data, c.length, c.channels);
                preview_.trigger(1.0f);
                preview_sample_ = c.data;
            }
            break;
        case Command::StopPreview:
            preview_.set_sample(nullptr, 0, 1);  // plus aucune lecture du buffer
            release_pointer(preview_sample_);
            break;
        case Command::Play:
            end_record();
            seq_.play();
            pending_count_ = 0;
            just_started_  = true;
            state_.playing.store(true);
            break;
        case Command::Stop:
            end_record();
            seq_.stop();
            pending_count_ = 0;  // les notes déjà programmées ne partent plus
            state_.playing.store(false);
            break;
        case Command::StartRecord:
            if (track_ok) start_record(c.track);
            break;
        case Command::StopRecord:
            if (phase_ == Phase::Precount) {  // le pré-compte avait lancé la lecture : on l'annule
                end_record();
                seq_.stop();
                pending_count_ = 0;
                state_.playing.store(false);
            } else {
                end_record();
            }
            break;
        case Command::SetLatency: {
            double ms = c.value;
            if (!(ms >= 0.0)) ms = 0.0;
            if (ms > MAX_LATENCY_MS) ms = MAX_LATENCY_MS;
            latency_samples_ = ms * cfg::SAMPLE_RATE / 1000.0;
            break;
        }
        case Command::Trigger:
            trigger(c.track);
            break;
    }
}

// Déclenche la note d'un pas avec sa vélocité et son pitch.
void Engine::play_step(int track, int step) {
    const Pattern& p = seq_.pattern();
    trigger(track, static_cast<float>(p.velocity[track][step]) / static_cast<float>(cfg::VELOCITY_MAX),
            dsp::semitone_ratio(p.pitch[track][step]), p.pan[track][step]);
}

// Programme une note à `countdown` échantillons (mesuré depuis l'échantillon courant). Si le tableau est
// plein (ce qui ne devrait pas arriver), la note est abandonnée : aucune allocation dans le callback audio.
void Engine::schedule(double countdown, int track, int step, int velocity, int pitch, int pan) {
    if (pending_count_ >= MAX_PENDING) return;
    Pending& p = pending_[pending_count_++];
    p.countdown = countdown;
    p.track     = static_cast<uint8_t>(track);
    p.step      = static_cast<uint8_t>(step);
    p.velocity  = static_cast<uint8_t>(velocity);
    p.pitch     = static_cast<int8_t>(pitch);
    p.pan       = static_cast<int8_t>(pan);
}

// Joue les notes en attente dont l'heure est venue.
void Engine::fire_pending() {
    int kept = 0;
    for (int i = 0; i < pending_count_; ++i) {
        const Pending p = pending_[i];
        if (p.countdown <= 0.0) {
            trigger(p.track, static_cast<float>(p.velocity) / static_cast<float>(cfg::VELOCITY_MAX), dsp::semitone_ratio(p.pitch), p.pan);
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
    cur_step_             = step;
    state_.step.store(step);

    bool begin_take = false;
    if (phase_ == Phase::Precount) {
        if (precount_left_ > 0) {  // pré-compte : seulement les clics, un par temps
            if (step % 4 == 0) click_.trigger(step == 0);
            --precount_left_;
            state_.precount_step.store(step);
            return;
        }
        // Les 16 pas du pré-compte sont joués : ce pas 0 est le premier de la prise.
        phase_      = Phase::Recording;
        just_started_ = true;
        take_steps_ = 0;
        for (bool& t : touched_) t = false;
        state_.record_phase.store(static_cast<int>(Phase::Recording));
        state_.precount_step.store(-1);
        begin_take = true;
    }
    if (phase_ == Phase::Recording) {
        ++take_steps_;
        touched_[(step + cfg::MAX_STEPS / 2) % cfg::MAX_STEPS] = false;  // le pas opposé : sa fenêtre est loin
    }

    for (int t = 0; t < cfg::MAX_TRACKS; ++t) {
        const double keep = static_cast<double>(cfg::QUANTIZE_MAX - pat.quantize[t]) / cfg::QUANTIZE_MAX;

        // Les notes du pas qui commence.
        if (pat.on[t][step]) {
            const int hits = 1 + pat.extra_count[t][step];
            for (int h = 0; h < hits; ++h) {
                const int off = h == 0 ? pat.offset[t][step] : pat.extra[t][step][h - 1].offset;
                const int vel = h == 0 ? pat.velocity[t][step] : pat.extra[t][step][h - 1].velocity;
                const int pit = h == 0 ? pat.pitch[t][step] : pat.extra[t][step][h - 1].pitch;
                const int pan = h == 0 ? pat.pan[t][step] : pat.extra[t][step][h - 1].pan;

                const double shift = static_cast<double>(off) * keep;  // en 1/128 de pas, signe conservé
                if (shift == 0.0 || (shift < 0.0 && just_started_ && step == 0)) {
                    trigger(t, static_cast<float>(vel) / static_cast<float>(cfg::VELOCITY_MAX), dsp::semitone_ratio(pit), pan);
                } else if (shift > 0.0) {
                    schedule(lateness + shift / cfg::OFFSET_UNITS * length, t, step, vel, pit, pan);
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
                const int pan = h == 0 ? pat.pan[t][next] : pat.extra[t][next][h - 1].pan;
                schedule(lateness + length + shift / cfg::OFFSET_UNITS * length, t, next, vel, pit, pan);
            }
        }
    }
    just_started_ = false;

    if (begin_take) {  // les frappes d'avant le premier pas, enfin rangées
        for (int i = 0; i < early_count_; ++i) record_hit(0, early_[i]);
        early_count_ = 0;
    }
}

void Engine::start_record(int track) {
    record_track_ = track;
    phase_        = Phase::Precount;
    precount_left_ = cfg::MAX_STEPS;
    take_steps_   = 0;
    early_count_  = 0;
    for (bool& t : touched_) t = false;
    seq_.pattern().clear_steps(track);  // une prise repart d'une piste vide (la quantification de la piste est conservée)
    seq_.play();  // depuis le début : le pré-compte occupe un tour
    pending_count_ = 0;
    just_started_  = false;
    state_.playing.store(true);
    state_.record_phase.store(static_cast<int>(Phase::Precount));
    state_.precount_step.store(-1);
}

void Engine::end_record() {
    phase_ = Phase::Idle;
    early_count_ = 0;
    state_.record_phase.store(static_cast<int>(Phase::Idle));
    state_.precount_step.store(-1);
}

// Une frappe pendant le pré-compte ou la prise : on la date par rapport à la grille. Elle a déjà sonné (le
// moteur la joue tout de suite), donc le pattern la reçoit pour les tours suivants.
void Engine::record_note(int velocity, int pitch, int pan) {
    if (!seq_.playing()) return;
    while (seq_.due()) fire_step();  // les pas dus à cet instant sont d'abord tirés : la position est sans ambiguïté
    if (phase_ == Phase::Idle) return;

    // Position de la frappe en pas depuis le début du pas courant, latence retirée.
    const double length = seq_.step_length();
    const double pos    = (length - seq_.countdown()) / length - latency_samples_ / length;
    const double near   = std::floor(pos + 0.5);              // pas le plus proche, relatif au pas courant
    int          off    = static_cast<int>(std::lround((pos - near) * cfg::OFFSET_UNITS));
    if (off < cfg::OFFSET_MIN) off = cfg::OFFSET_MIN;
    if (off > cfg::OFFSET_MAX) off = cfg::OFFSET_MAX;
    const long r = static_cast<long>(near);

    Hit hit;
    hit.offset   = static_cast<int8_t>(off);
    hit.velocity = static_cast<uint8_t>(velocity);
    hit.pitch    = static_cast<int8_t>(pitch);
    hit.pan      = static_cast<int8_t>(pan);

    if (phase_ == Phase::Precount) {
        // Seule la fin du dernier pas du pré-compte compte : une frappe juste avant le premier pas.
        if (precount_left_ != 0 || r != 1) return;
        if (early_count_ < MAX_EARLY) early_[early_count_++] = hit;
        else state_.record_lost.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const long index = take_steps_ - 1 + r;  // rang du pas visé depuis le début de la prise
    if (index < 0) return;                   // avant la prise
    record_hit(static_cast<int>((cur_step_ + r + 2L * cfg::MAX_STEPS) % cfg::MAX_STEPS), hit);
}

// Range une frappe dans le pas visé. La première du tour remplace les notes du pas ; les suivantes
// s'ajoutent (roulement) ; au-delà de 4 notes, la plus proche en temps est remplacée.
void Engine::record_hit(int step, const Hit& hit) {
    Pattern& pat = seq_.pattern();
    const int t  = record_track_;

    if (!touched_[step]) {
        touched_[step]       = true;
        pat.on[t][step]       = 1;
        pat.offset[t][step]   = hit.offset;
        pat.velocity[t][step] = hit.velocity;
        pat.pitch[t][step]    = hit.pitch;
        pat.pan[t][step]      = hit.pan;
        pat.extra_count[t][step] = 0;
        for (Hit& h : pat.extra[t][step]) h = Hit{};
        // Les anciennes notes de ce pas encore en attente ne doivent plus sonner (la nouvelle a déjà sonné).
        int kept = 0;
        for (int i = 0; i < pending_count_; ++i)
            if (!(pending_[i].track == t && pending_[i].step == step)) pending_[kept++] = pending_[i];
        pending_count_ = kept;
    } else if (pat.extra_count[t][step] < cfg::MAX_EXTRA_HITS) {
        pat.extra[t][step][pat.extra_count[t][step]++] = hit;
    } else {
        // Quatre notes déjà : on remplace celle dont le décalage est le plus proche.
        int best = 0, best_d = 1 << 30;
        for (int n = 0; n < 1 + cfg::MAX_EXTRA_HITS; ++n) {
            const int o = n == 0 ? pat.offset[t][step] : pat.extra[t][step][n - 1].offset;
            const int d = o > hit.offset ? o - hit.offset : hit.offset - o;
            if (d < best_d) {
                best_d = d;
                best   = n;
            }
        }
        if (best == 0) {
            pat.offset[t][step]   = hit.offset;
            pat.velocity[t][step] = hit.velocity;
            pat.pitch[t][step]    = hit.pitch;
            pat.pan[t][step]      = hit.pan;
        } else {
            pat.extra[t][step][best - 1] = hit;
        }
    }
    send_step_event(step);
}

void Engine::send_step_event(int step) {
    const Pattern& pat = seq_.pattern();
    const int      t   = record_track_;

    StepEvent ev;
    ev.track       = static_cast<uint8_t>(t);
    ev.step        = static_cast<uint8_t>(step);
    ev.extra_count = pat.extra_count[t][step];
    ev.main        = Hit{pat.offset[t][step], pat.velocity[t][step], pat.pitch[t][step], pat.pan[t][step]};
    for (int n = 0; n < cfg::MAX_EXTRA_HITS; ++n) ev.extra[n] = pat.extra[t][step][n];
    if (!step_events_.push(ev)) state_.record_lost.fetch_add(1, std::memory_order_relaxed);
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
    for (int i = 0; i < frames; ++i) {
        left[i]  = 0.0f;
        right[i] = 0.0f;
    }

    for (int t = 0; t < cfg::MAX_TRACKS; ++t) {
        if (!voices_[t]) continue;
        for (int i = 0; i < frames; ++i) {
            scratch_[i]       = 0.0f;
            scratch_right_[i] = 0.0f;
        }
        voices_[t]->render_stereo(scratch_, scratch_right_, frames);
        mixer::accumulate(left, scratch_, frames, gains_[t]);
        mixer::accumulate(right, scratch_right_, frames, gains_[t]);
    }

    if (preview_.active()) {
        for (int i = 0; i < frames; ++i) {
            scratch_[i]       = 0.0f;
            scratch_right_[i] = 0.0f;
        }
        preview_.render_stereo(scratch_, scratch_right_, frames);
        mixer::accumulate(left, scratch_, frames, 1.0f);
        mixer::accumulate(right, scratch_right_, frames, 1.0f);
    }

    if (click_.active()) {  // le clic du pré-compte est au centre
        for (int i = 0; i < frames; ++i) scratch_[i] = 0.0f;
        click_.render(scratch_, frames);
        mixer::accumulate(left, scratch_, frames, 1.0f);
        mixer::accumulate(right, scratch_, frames, 1.0f);
    }

    mixer::master(left, frames, cfg::MASTER_GAIN);
    mixer::master(right, frames, cfg::MASTER_GAIN);
}
