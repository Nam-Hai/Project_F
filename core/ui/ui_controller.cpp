#include "ui_controller.h"

#include "keyboard_layout.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static_assert(cfg::MAX_STEPS <= 16, "le pattern d'une piste tient dans un uint16_t");

namespace {

// Bounds an int for display: lets the compiler prove that snprintf fits the small text buffers.
int clamp_int(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

bool ends_with_wav(const char* s, int len) {
    if (len < 4) return false;
    const char ext[] = ".wav";
    for (int i = 0; i < 4; ++i) {
        char c = s[len - 4 + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != ext[i]) return false;
    }
    return true;
}

// Nom d'affichage : sans « .wav », tronqué à UiController::NAME_LEN caractères.
void display_name(char* dst, const char* src) {
    int len = static_cast<int>(std::strlen(src));
    if (ends_with_wav(src, len)) len -= 4;
    if (len > UiController::NAME_LEN) len = UiController::NAME_LEN;
    std::memcpy(dst, src, static_cast<std::size_t>(len));
    dst[len] = '\0';
}

// Garde le curseur visible dans une liste de LIST_ROWS lignes (-1 = ligne de titre : hors liste).
void reveal(int cursor, int& scroll) {
    if (cursor < 0) {
        scroll = 0;
        return;
    }
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + UiController::LIST_ROWS) scroll = cursor - UiController::LIST_ROWS + 1;
}

void move_in_list(int& cursor, int& scroll, int items, int delta, int min_cursor = 0) {
    cursor += delta;
    if (cursor < min_cursor) cursor = min_cursor;
    if (cursor > items - 1) cursor = items - 1;
    reveal(cursor, scroll);
}

// Balance ou pan en texte : « C » au centre, « L35 » à gauche, « R20 » à droite.
void pan_text(char* dst, std::size_t cap, int v) {
    if (v == 0) std::snprintf(dst, cap, "C");
    else std::snprintf(dst, cap, "%c%d", v < 0 ? 'L' : 'R', v < 0 ? -v : v);
}

}  // namespace

// ============================================================================================
// Modèle
// ============================================================================================

int UiController::add_track(const char* file, uint8_t engine_track, uint16_t pattern_bits, float gain,
                            float pitch, const uint8_t* step_velocity, const int8_t* step_pitch) {
    if (track_count_ >= cfg::MAX_TRACKS) return -1;

    Track& t = tracks_[track_count_];
    t        = Track{};
    std::snprintf(t.file, sizeof t.file, "%s", file);
    display_name(t.name, file);
    t.engine_track = engine_track;
    t.pattern      = pattern_bits;
    t.gain         = gain;
    t.pitch        = pitch;
    for (int s = 0; s < cfg::MAX_STEPS; ++s) {
        if (step_velocity) {
            const int v = step_velocity[s];
            t.step_velocity[s] = static_cast<uint8_t>(v < cfg::VELOCITY_MIN ? cfg::VELOCITY_MIN : v > cfg::VELOCITY_MAX ? cfg::VELOCITY_MAX : v);
        }
        if (step_pitch) {
            const int v = step_pitch[s];
            t.step_pitch[s] = static_cast<int8_t>(v < cfg::STEP_PITCH_MIN ? cfg::STEP_PITCH_MIN : v > cfg::STEP_PITCH_MAX ? cfg::STEP_PITCH_MAX : v);
        }
    }
    return track_count_++;
}

void UiController::reset_profile() {
    std::strcpy(profile_.name, "Default");
    profile_.sample_count = 0;
    profile_.loop_count   = 0;
}

bool UiController::load_profile() {
    if (!store_) {
        reset_profile();
        return true;
    }
    if (store_->load(profile_) == profile::LoadResult::Failed) {
        reset_profile();  // l'ancien fichier est conservé en .bak lors de la prochaine sauvegarde
        notify(" Profil illisible");
        return false;
    }
    return true;
}

void UiController::notify(const char* text) {
    std::snprintf(notice_, sizeof notice_, "%s", text);
    error_until_ = now_ms_ + ERROR_MS;
}

// ============================================================================================
// Entrées
// ============================================================================================

void UiController::handle(const InputEvent& e) {
    if (loading_ || restoring_) return;    // un chargement de loop est en cours : il se termine en un instant
    error_until_ = now_ms_;  // toute action efface le message

    switch (e.type) {
        case InputEvent::Wheel:  on_wheel(e.delta); break;
        case InputEvent::Ok:     on_ok(); break;
        case InputEvent::Back:   on_back(); break;
        case InputEvent::Menu:   on_menu(); break;
        case InputEvent::Mode:   on_mode(); break;
        case InputEvent::Play:   on_play(); break;
        case InputEvent::Step:   on_step(e.index); break;
        case InputEvent::Black:  on_black(e.index); break;
        case InputEvent::BlackUp: on_black_up(e.index); break;
        case InputEvent::PotBpm: on_pot_bpm(e.value); break;
    }
    if (view_ != View::Pattern) {  // les réglages tenus n'existent que dans la vue Pattern
        setting_   = TrackSetting::None;
        held_mask_ = 0;
    }
}

void UiController::on_wheel(int delta) {
    switch (view_) {
        case View::Tracks:
            // -1 = la ligne de titre ; puis les pistes ; puis « Ajouter piste ».
            move_in_list(cursor_, scroll_, track_count_ + 1, delta, -1);
            break;
        case View::Add:
            if (library_) {
                const int before = add_cursor_;
                move_in_list(add_cursor_, add_scroll_, library_->count(), delta);
                if (add_cursor_ != before) schedule_preview();
            }
            break;
        case View::Menu: {
            int unused = 0;
            move_in_list(menu_cursor_, unused, menu_item_count(), delta);
            confirm_ = false;
            break;
        }
        case View::Loops:
            move_in_list(loops_cursor_, loops_scroll_, profile_.loop_count + 1, delta);
            confirm_ = false;
            break;
        case View::Pattern:
            if (setting_ != TrackSetting::None) adjust_track_setting(delta);  // un réglage de la piste est tenu
            else adjust_step_value(delta);
            break;
        case View::Keyboard:
            break;
    }
}

bool UiController::can_add() const {
    return library_ && library_->count() > 0 && track_count_ < cfg::MAX_TRACKS && free_engine_track() >= 0;
}

// Plus petit index de piste moteur qui n'est pas déjà utilisé par une piste de l'UI.
int UiController::free_engine_track() const {
    bool used[cfg::MAX_TRACKS] = {};
    for (int i = 0; i < track_count_; ++i) used[tracks_[i].engine_track] = true;
    for (int t = 0; t < cfg::MAX_TRACKS; ++t)
        if (!used[t]) return t;
    return -1;
}

void UiController::open_pattern(int track) {
    selected_    = track;
    step_cursor_ = -1;
    edit_mode_   = EditMode::Edit;
    audition_pending_ = false;
    confirm_     = false;
    view_        = View::Pattern;
}

void UiController::open_add(bool replace) {
    add_replace_     = replace;
    add_cursor_      = 0;
    add_scroll_      = 0;
    preview_pending_ = -1;  // pas d'écoute à l'ouverture : seulement quand on bouge la roue
    view_            = View::Add;
}

void UiController::leave_add() {
    stop_preview();
    // Remplacement : retour là d'où on a ouvert le menu. Ajout : retour à la liste.
    view_ = add_replace_ ? menu_origin_ : View::Tracks;
}

// Charge le sample choisi (hors audio) et le donne à la piste moteur. false = rien n'a changé.
bool UiController::send_sample(int engine_track) {
    stop_preview();

    const int16_t* data   = nullptr;
    uint32_t       length = 0;
    if (!library_->load(add_cursor_, data, length)) {
        notify(" Echec du chargement");
        return false;
    }

    Command c;
    c.type   = Command::SetTrackSample;
    c.track  = static_cast<uint8_t>(engine_track);
    c.data     = data;
    c.length   = length;
    c.channels = static_cast<uint8_t>(library_->channels(data));
    if (!sink_.push(c)) {  // file pleine : on ne garde rien, le buffer est rendu
        library_->discard(data);
        notify(" Echec du chargement");
        return false;
    }
    return true;
}

// Crée une piste avec le sample choisi, puis ouvre son pattern (vide) pour le remplir.
void UiController::confirm_add() {
    const int engine_track = free_engine_track();
    if (engine_track < 0 || track_count_ >= cfg::MAX_TRACKS) return;
    if (!send_sample(engine_track)) return;

    const int index = add_track(library_->name(add_cursor_), static_cast<uint8_t>(engine_track));
    cursor_         = index;
    reveal(cursor_, scroll_);
    mark_dirty();
    open_pattern(index);
}

// Remplace le sample de la piste sélectionnée ; son pattern est conservé.
void UiController::confirm_replace() {
    Track& t = tracks_[selected_];
    if (!send_sample(t.engine_track)) return;

    std::snprintf(t.file, sizeof t.file, "%s", library_->name(add_cursor_));
    display_name(t.name, t.file);
    mark_dirty();
    view_ = menu_origin_;
}

void UiController::delete_selected() {
    Command c;
    c.type  = Command::ClearTrack;
    c.track = tracks_[selected_].engine_track;
    if (!sink_.push(c)) {
        confirm_ = false;
        notify(" Echec de l'action");
        return;
    }

    for (int i = selected_; i < track_count_ - 1; ++i) tracks_[i] = tracks_[i + 1];
    tracks_[--track_count_] = Track{};

    // Retour à la liste, sur la piste qui a pris la place (ou « Ajouter piste » si c'était la dernière).
    const int items = track_count_ + 1;
    cursor_         = selected_ < items ? selected_ : items - 1;
    const int max_scroll = items > LIST_ROWS ? items - LIST_ROWS : 0;
    if (scroll_ > max_scroll) scroll_ = max_scroll;
    reveal(cursor_, scroll_);

    confirm_ = false;
    mark_dirty();
    view_ = View::Tracks;
}

void UiController::on_ok() {
    switch (view_) {
        case View::Tracks:
            if (cursor_ < 0) open_loops();  // la ligne de titre : accès direct à la liste des loops
            else if (cursor_ < track_count_) open_pattern(cursor_);
            else if (can_add()) open_add(false);
            break;
        case View::Pattern:
            break;
        case View::Add:
            if (add_replace_) confirm_replace();
            else confirm_add();
            break;
        case View::Menu:
            on_menu_ok();
            break;
        case View::Loops:
            on_loops_ok();
            break;
        case View::Keyboard:
            break;
    }
}

void UiController::on_back() {
    switch (view_) {
        case View::Pattern:
            view_   = View::Tracks;
            cursor_ = selected_;
            reveal(cursor_, scroll_);
            break;
        case View::Add:
            leave_add();
            break;
        case View::Menu:
            close_menu();
            break;
        case View::Loops:
            confirm_ = false;
            view_    = View::Tracks;
            break;
        case View::Keyboard:
            if (record_phase_ != 0) {  // quitter la vue arrête la prise (elle est conservée)
                Command c;
                c.type = Command::StopRecord;
                sink_.push(c);
            }
            view_ = keyboard_origin_;
            break;
        case View::Tracks:
            break;
    }
}

void UiController::on_menu() {
    switch (view_) {
        case View::Tracks:
            if (cursor_ < 0) {
                open_menu(MenuKind::Loop);
            } else if (cursor_ < track_count_) {
                selected_ = cursor_;
                open_menu(MenuKind::Track);
            }  // « Ajouter piste » n'a pas de menu
            break;
        case View::Pattern:
            open_menu(MenuKind::Track);
            break;
        case View::Menu:
            close_menu();  // Menu rappuyé : on referme
            break;
        case View::Loops:
            if (loops_cursor_ < profile_.loop_count) {
                loop_item_ = loops_cursor_;
                open_menu(MenuKind::LoopItem);
            }  // « Nouvelle loop » n'a pas de menu
            break;
        case View::Add:
            break;  // pas de menu dans cette vue pour l'instant
        case View::Keyboard:
            open_menu(MenuKind::Keyboard);
            break;
    }
}

// Mode : Edit -> Velocity -> Pitch -> Timing -> Balance -> Edit, dans la vue Pattern seulement.
void UiController::on_mode() {
    if (view_ != View::Pattern) return;
    switch (edit_mode_) {
        case EditMode::Edit:     edit_mode_ = EditMode::Velocity; break;
        case EditMode::Velocity: edit_mode_ = EditMode::Pitch; break;
        case EditMode::Pitch:    edit_mode_ = EditMode::Timing; break;
        case EditMode::Timing:   edit_mode_ = EditMode::Balance; break;
        case EditMode::Balance:  edit_mode_ = EditMode::Edit; break;
    }
    audition_pending_ = false;
}

// La roue règle la vélocité, le pitch ou le décalage du pas sélectionné (s'il est joué), puis joue la note.
void UiController::adjust_step_value(int delta) {
    if (edit_mode_ == EditMode::Edit || step_cursor_ < 0) return;

    Track& t = tracks_[selected_];
    if (!((t.pattern >> step_cursor_) & 1u)) return;
    if (!shift_step_notes(t, step_cursor_, delta)) return;

    mark_dirty();
    if (edit_mode_ == EditMode::Timing) return;  // le décalage ne change pas le son de la note : pas d'écoute
    audition_pending_ = true;
    audition_selected_step();
}

// Ajoute `delta` crans à la valeur du mode courant pour toutes les notes du pas (la principale et les
// notes supplémentaires). Le même écart est appliqué à toutes, borné pour qu'aucune ne sorte de ses
// limites : un roulement garde ainsi ses écarts. false si rien n'a changé.
bool UiController::shift_step_notes(Track& t, int s, int delta) {
    int lo = 0, hi = 0, per = 1;
    switch (edit_mode_) {
        case EditMode::Velocity: lo = cfg::VELOCITY_MIN;   hi = cfg::VELOCITY_MAX;   per = VELOCITY_PER_NOTCH; break;
        case EditMode::Pitch:    lo = cfg::STEP_PITCH_MIN; hi = cfg::STEP_PITCH_MAX; per = 1; break;
        case EditMode::Timing:   lo = cfg::OFFSET_MIN;     hi = cfg::OFFSET_MAX;     per = OFFSET_PER_NOTCH; break;
        case EditMode::Balance:  lo = cfg::PAN_MIN;        hi = cfg::PAN_MAX;        per = BALANCE_PER_NOTCH; break;
        case EditMode::Edit:     return false;
    }
    auto value = [&](int n) -> int {  // n = 0 : note principale ; n >= 1 : note supplémentaire n
        if (n == 0) {
            return edit_mode_ == EditMode::Velocity ? t.step_velocity[s]
                 : edit_mode_ == EditMode::Pitch    ? t.step_pitch[s]
                 : edit_mode_ == EditMode::Balance  ? t.step_pan[s]
                                                    : t.step_offset[s];
        }
        const Hit& h = t.extra[s][n - 1];
        return edit_mode_ == EditMode::Velocity ? h.velocity : edit_mode_ == EditMode::Pitch ? h.pitch : edit_mode_ == EditMode::Balance ? h.pan : h.offset;
    };

    const int notes = 1 + t.extra_count[s];
    int       d     = delta * per;
    for (int n = 0; n < notes; ++n) {
        const int v = value(n);
        if (d < lo - v) d = lo - v;
        if (d > hi - v) d = hi - v;
    }
    if (d == 0) return false;

    Command c;
    c.track = t.engine_track;
    c.step  = static_cast<uint8_t>(s);
    c.type  = edit_mode_ == EditMode::Velocity ? Command::SetStepVelocity
            : edit_mode_ == EditMode::Pitch    ? Command::SetStepPitch
            : edit_mode_ == EditMode::Balance  ? Command::SetStepPan
                                               : Command::SetStepOffset;
    c.value = static_cast<float>(value(0) + d);
    if (!sink_.push(c)) return false;
    if (edit_mode_ == EditMode::Velocity)   t.step_velocity[s] = static_cast<uint8_t>(t.step_velocity[s] + d);
    else if (edit_mode_ == EditMode::Pitch) t.step_pitch[s]    = static_cast<int8_t>(t.step_pitch[s] + d);
    else if (edit_mode_ == EditMode::Balance) t.step_pan[s]    = static_cast<int8_t>(t.step_pan[s] + d);
    else                                    t.step_offset[s]   = static_cast<int8_t>(t.step_offset[s] + d);

    for (int n = 1; n < notes; ++n) {
        Hit h = t.extra[s][n - 1];
        if (edit_mode_ == EditMode::Velocity)   h.velocity = static_cast<uint8_t>(h.velocity + d);
        else if (edit_mode_ == EditMode::Pitch) h.pitch    = static_cast<int8_t>(h.pitch + d);
        else if (edit_mode_ == EditMode::Balance) h.pan    = static_cast<int8_t>(h.pan + d);
        else                                    h.offset   = static_cast<int8_t>(h.offset + d);

        Command e;
        e.type   = Command::SetExtraHit;
        e.track  = t.engine_track;
        e.step   = static_cast<uint8_t>(s);
        e.slot   = static_cast<uint8_t>(n);
        e.offset = h.offset;
        e.value  = static_cast<float>(h.velocity);
        e.pitch  = h.pitch;
        e.pan    = h.pan;
        if (!sink_.push(e)) break;  // file pleine : le modèle suit ce que le moteur a reçu
        t.extra[s][n - 1] = h;
    }
    return true;
}

// Touche noire de réglage relâchée : on revient à l'état d'avant (si une autre est encore tenue, c'est elle qui règle).
void UiController::on_black_up(int index) {
    if (index < 0 || index > 7) return;
    held_mask_ = static_cast<uint8_t>(held_mask_ & ~(1u << index));

    if (setting_ != setting_for_key(index)) return;
    setting_ = TrackSetting::None;
    for (int k = 7; k >= 0; --k) {
        if ((held_mask_ >> k) & 1u) {
            setting_ = setting_for_key(k);
            break;
        }
    }
}

// Réglage de piste porté par une touche noire de la vue Pattern : 0 Gain, 1 Quantifier, 2 Wide, 3 Balance, 4 à 7
// Attack, Decay, Sustain, Release.
UiController::TrackSetting UiController::setting_for_key(int index) {
    switch (index) {
        case 0:  return TrackSetting::Gain;
        case 1:  return TrackSetting::Quantize;
        case 2:  return TrackSetting::Width;
        case 3:  return TrackSetting::Balance;
        case 4:  return TrackSetting::Attack;
        case 5:  return TrackSetting::Decay;
        case 6:  return TrackSetting::Sustain;
        case 7:  return TrackSetting::Release;
        default: return TrackSetting::None;
    }
}

// Track gain (black key 1 held): 0.5 dB per notch, from -40 dB (below that: muted) to +6 dB. Computed in integer tenths of
// a dB so the grid has no floating-point drift. A value that is off the 0.5 dB grid (0.8 = -1.9 dB, for example) first moves
// to the nearest grid point in the direction of the notch.
void UiController::adjust_gain(int delta) {
    Track& t = tracks_[selected_];

    constexpr int    MIN_T = -400, MAX_T = 60, STEP_T = 5;  // tenths of dB
    constexpr double EPS   = 1e-3;                          // in grid steps: absorbs the float rounding of the stored gain
    // Current position in tenths of dB; one step below MIN_T stands for mute (gain 0).
    double pos = t.gain > 0.0f ? 200.0 * std::log10(static_cast<double>(t.gain)) : static_cast<double>(MIN_T - STEP_T);
    if (pos < MIN_T) pos = MIN_T - STEP_T;
    double next = pos;
    for (int k = delta > 0 ? delta : -delta; k > 0; --k) {
        next = (delta > 0 ? std::floor(next / STEP_T + EPS) + 1.0 : std::ceil(next / STEP_T - EPS) - 1.0) * STEP_T;
    }
    if (next > MAX_T) next = MAX_T;
    if (next < MIN_T) next = delta > 0 ? MIN_T : MIN_T - STEP_T;  // below the floor: going up lands on it, going down is mute
    const float gain = next < MIN_T ? 0.0f : static_cast<float>(std::pow(10.0, next / 200.0));
    if (gain == t.gain) return;

    Command c;
    c.type  = Command::SetTrackGain;
    c.track = t.engine_track;
    c.value = gain;
    if (!sink_.push(c)) return;
    t.gain = gain;
    mark_dirty();
}

// Valeurs proposées à la roue pour Attack, Decay et Release (ms) : fines près de 0, plus larges ensuite.
static const int ENV_STEPS_MS[] = {0, 1, 2, 3, 5, 7, 10, 15, 20, 30, 40, 50, 70, 100, 150, 200, 300, 500, 700, 1000, 1500, 2000};

// La roue sur Attack, Decay, Release : un cran = la valeur suivante (ou précédente) de la liste ; une valeur qui n'y
// est pas (venue d'un fichier) passe d'abord à la plus proche dans le sens du mouvement. Sustain : 5 % par cran.
void UiController::adjust_envelope(int delta) {
    Track& t = tracks_[selected_];
    Command c;
    c.track = t.engine_track;
    int value;

    if (setting_ == TrackSetting::Sustain) {
        value = t.sustain + delta * TRACK_PAN_PER_NOTCH;
        if (value < 0) value = 0;
        if (value > 100) value = 100;
        if (value == t.sustain) return;
        c.type = Command::SetTrackSustain;
    } else {
        const int current = setting_ == TrackSetting::Attack ? t.attack : setting_ == TrackSetting::Decay ? t.decay : t.release;
        constexpr int N   = static_cast<int>(sizeof ENV_STEPS_MS / sizeof ENV_STEPS_MS[0]);
        value             = current;
        for (int k = delta > 0 ? delta : -delta; k > 0; --k) {
            int next = value;
            if (delta > 0) {
                next = ENV_STEPS_MS[N - 1];
                for (int i = N - 1; i >= 0; --i)
                    if (ENV_STEPS_MS[i] > value) next = ENV_STEPS_MS[i];
                if (value >= ENV_STEPS_MS[N - 1]) next = value;
            } else {
                next = ENV_STEPS_MS[0];
                for (int i = 0; i < N; ++i)
                    if (ENV_STEPS_MS[i] < value) next = ENV_STEPS_MS[i];
                if (value <= ENV_STEPS_MS[0]) next = value;
            }
            value = next;
        }
        if (value == current) return;
        c.type = setting_ == TrackSetting::Attack ? Command::SetTrackAttack : setting_ == TrackSetting::Decay ? Command::SetTrackDecay : Command::SetTrackRelease;
    }
    c.value = static_cast<float>(value);
    if (!sink_.push(c)) return;
    switch (setting_) {
        case TrackSetting::Attack:  t.attack  = static_cast<uint16_t>(value); break;
        case TrackSetting::Decay:   t.decay   = static_cast<uint16_t>(value); break;
        case TrackSetting::Sustain: t.sustain = static_cast<uint8_t>(value); break;
        default:                    t.release = static_cast<uint16_t>(value); break;
    }
    mark_dirty();
}

// Réglage de la piste tenu : la roue change la valeur par pas de 5 (la force de quantification en %, le pan
// et la balance de la piste en points), avec effet immédiat.
void UiController::adjust_track_setting(int delta) {
    if (setting_ >= TrackSetting::Attack) {
        adjust_envelope(delta);
        return;
    }
    if (setting_ == TrackSetting::Gain) {
        adjust_gain(delta);
        return;
    }
    Track&  t = tracks_[selected_];
    Command c;
    c.track = t.engine_track;

    if (setting_ == TrackSetting::Quantize) {
        int q = t.quantize + delta * QUANTIZE_PER_NOTCH;
        if (q < 0) q = 0;
        if (q > cfg::QUANTIZE_MAX) q = cfg::QUANTIZE_MAX;
        if (q == t.quantize) return;
        c.type  = Command::SetTrackQuantize;
        c.value = static_cast<float>(q);
        if (!sink_.push(c)) return;
        t.quantize = static_cast<uint8_t>(q);
    } else if (setting_ == TrackSetting::Width || setting_ == TrackSetting::Balance) {
        const bool width = setting_ == TrackSetting::Width;
        int8_t&    field = width ? t.width : t.balance;
        int        v     = field + delta * TRACK_PAN_PER_NOTCH;
        const int  lo    = width ? cfg::WIDTH_MIN : cfg::PAN_MIN;
        const int  hi    = width ? cfg::WIDTH_MAX : cfg::PAN_MAX;
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        if (v == field) return;
        c.type  = width ? Command::SetTrackWidth : Command::SetTrackBalance;
        c.value = static_cast<float>(v);
        if (!sink_.push(c)) return;
        field = static_cast<int8_t>(v);
    } else {
        return;
    }
    mark_dirty();
}

// Joue la note du pas sélectionné, une fois toutes les AUDITION_MIN_MS au plus. Si la cadence la
// retient, elle reste en attente et part dans update() : on entend toujours la valeur finale.
void UiController::audition_selected_step() {
    if (!audition_pending_) return;
    if (audition_sent_ && static_cast<uint32_t>(now_ms_ - last_audition_ms_) < AUDITION_MIN_MS) return;

    Command c;
    c.type  = Command::AuditionStep;
    c.track = tracks_[selected_].engine_track;
    c.step  = static_cast<uint8_t>(step_cursor_);
    if (!sink_.push(c)) return;  // file pleine : on réessaiera au prochain update()
    last_audition_ms_ = now_ms_;
    audition_sent_    = true;
    audition_pending_ = false;
}

// Touche noire. Dans la vue Clavier, elle joue sa note. Ailleurs, seule la première a un effet : elle
// ouvre la vue Clavier (depuis la vue Pattern, ou la vue Pistes avec le curseur sur une piste) et joue
// sa note. Les autres noires, et la première dans les autres vues, sont ignorées.
void UiController::on_black(int index) {
    if (index < 0 || index >= keyboard::BLACK_KEYS) return;

    if (view_ == View::Keyboard) {
        play_key(keyboard::BLACK_PITCH[index]);
        return;
    }
    if (view_ == View::Pattern && index >= 0 && index <= 7) {  // tenir : règle un paramètre de la piste (voir setting_for_key)
        held_mask_ = static_cast<uint8_t>(held_mask_ | (1u << index));
        setting_   = setting_for_key(index);
        audition_pending_ = false;
        return;
    }
    if (index != REC_BLACK_KEY) return;

    if (view_ == View::Pattern) {
        enter_keyboard(selected_);
    } else if (view_ == View::Tracks && cursor_ >= 0 && cursor_ < track_count_) {
        enter_keyboard(cursor_);
    } else {
        return;
    }
    play_key(keyboard::BLACK_PITCH[REC_BLACK_KEY]);
}

// Touche blanche, dans la vue Clavier : les 14 premières jouent, les deux dernières sont inactives.
void UiController::on_white_key(int index) {
    if (index < 0 || index >= keyboard::WHITE_KEYS) return;
    play_key(keyboard::WHITE_PITCH[index]);
}

void UiController::enter_keyboard(int track) {
    keyboard_origin_  = view_;
    selected_         = track;
    audition_pending_ = false;
    view_             = View::Keyboard;
}

// Joue la piste du clavier tout de suite, au pitch de la touche.
void UiController::play_key(int pitch) {
    Command c;
    c.type  = Command::Note;
    c.track = tracks_[selected_].engine_track;
    c.value = static_cast<float>(KEYBOARD_VELOCITY);
    c.pitch = static_cast<int8_t>(pitch);
    sink_.push(c);
}

void UiController::on_play() {
    if (view_ == View::Keyboard) {  // dans la vue Clavier, Play lance ou arrête la prise
        toggle_record();
        return;
    }
    Command c;
    c.type = playing_ ? Command::Stop : Command::Play;
    if (sink_.push(c)) playing_ = !playing_;  // confirmé ensuite par update()
}

void UiController::on_step(int index) {
    if (view_ == View::Keyboard) {
        on_white_key(index);
        return;
    }
    if (view_ != View::Pattern || index < 0 || index >= cfg::MAX_STEPS) return;

    Track& t = tracks_[selected_];

    // Velocity et Pitch : un pas joué se sélectionne ; les pas éteints sont ignorés (pour en
    // allumer un, on repasse en Edit).
    if (edit_mode_ != EditMode::Edit) {
        if ((t.pattern >> index) & 1u) step_cursor_ = index;
        return;
    }

    const bool on = !((t.pattern >> index) & 1u);

    Command c;
    c.type  = Command::SetStep;
    c.track = t.engine_track;
    c.step  = static_cast<uint8_t>(index);
    c.value = on ? 1.0f : 0.0f;
    if (!sink_.push(c)) return;  // file pleine : le modèle reste cohérent avec le moteur

    t.pattern = static_cast<uint16_t>(on ? (t.pattern | (1u << index)) : (t.pattern & ~(1u << index)));
    step_cursor_ = index;
    mark_dirty();
}

// Play dans la vue Clavier : lance le pré-compte puis la prise sur la piste du clavier, ou termine la prise
// (conservée). La piste est copiée au lancement : « Annuler la prise » la remet dans cet état.
void UiController::toggle_record() {
    Command c;
    if (record_phase_ == 0) {
        c.type  = Command::StartRecord;
        c.track = tracks_[selected_].engine_track;
        if (!sink_.push(c)) return;
        snapshot_      = tracks_[selected_];
        have_snapshot_ = true;
        record_phase_  = 1;  // confirmé ensuite par update()

        // Le moteur vide les pas de la piste au lancement de la prise : le modèle fait de même (la copie ci-dessus
        // permet de tout restaurer avec « Annuler la prise »). La quantification, la largeur et la balance restent.
        Track& t  = tracks_[selected_];
        t.pattern = 0;
        for (int st = 0; st < cfg::MAX_STEPS; ++st) {
            t.step_velocity[st] = static_cast<uint8_t>(cfg::VELOCITY_MAX);
            t.step_pitch[st]    = 0;
            t.step_pan[st]      = 0;
            t.step_offset[st]   = 0;
            t.extra_count[st]   = 0;
            for (Hit& h : t.extra[st]) h = Hit{};
        }
        mark_dirty();
    } else {
        c.type = Command::StopRecord;
        sink_.push(c);
    }
}

// Le moteur a rangé une frappe dans un pas : on écrase le modèle avec l'état complet du pas.
void UiController::apply_step_event(const StepEvent& ev) {
    if (restoring_ || ev.step >= cfg::MAX_STEPS || ev.extra_count > cfg::MAX_EXTRA_HITS) return;
    for (int i = 0; i < track_count_; ++i) {
        Track& t = tracks_[i];
        if (t.engine_track != ev.track) continue;
        const int s       = ev.step;
        t.pattern         = static_cast<uint16_t>(t.pattern | (1u << s));
        t.step_offset[s]  = ev.main.offset;
        t.step_velocity[s] = ev.main.velocity;
        t.step_pitch[s]   = ev.main.pitch;
        t.step_pan[s]     = ev.main.pan;
        t.extra_count[s]  = ev.extra_count;
        for (int e = 0; e < cfg::MAX_EXTRA_HITS; ++e) t.extra[s][e] = ev.extra[e];
        mark_dirty();
        return;
    }
}

// Remet la piste du clavier dans l'état d'avant la prise : le modèle tout de suite, le moteur par paquets
// (pump_restore).
void UiController::undo_take() {
    if (!have_snapshot_ || selected_ < 0 || selected_ >= track_count_) return;

    if (record_phase_ != 0) {
        Command c;
        c.type = Command::StopRecord;
        sink_.push(c);
    }
    Track& t           = tracks_[selected_];
    t.pattern          = snapshot_.pattern;
    t.quantize         = snapshot_.quantize;
    for (int s = 0; s < cfg::MAX_STEPS; ++s) {
        t.step_velocity[s] = snapshot_.step_velocity[s];
        t.step_pitch[s]    = snapshot_.step_pitch[s];
        t.step_offset[s]   = snapshot_.step_offset[s];
        t.step_pan[s]      = snapshot_.step_pan[s];
        t.extra_count[s]   = snapshot_.extra_count[s];
        for (int e = 0; e < cfg::MAX_EXTRA_HITS; ++e) t.extra[s][e] = snapshot_.extra[s][e];
    }
    snapshot_.engine_track = t.engine_track;

    int extras = 0;
    for (int s = 0; s < cfg::MAX_STEPS; ++s) extras += snapshot_.extra_count[s];
    restore_total_ = 5 * cfg::MAX_STEPS + 2 + extras;
    restore_next_  = 0;
    restoring_     = true;
    pump_restore();
    mark_dirty();
}

// Commande numéro `index` de la remise en état : d'abord les notes supplémentaires effacées, puis chaque
// vélocité, pitch, décalage et balance de chaque pas, le pattern, la quantification, enfin les notes supplémentaires de la copie (82 commandes au minimum).
bool UiController::restore_command(int index, Command& c) const {
    c = Command{};
    c.track = snapshot_.engine_track;
    const int n = cfg::MAX_STEPS;
    if (index < 0 || index >= restore_total_) return false;

    if (index < n) {
        c.type = Command::ClearExtraHits;
        c.step = static_cast<uint8_t>(index);
    } else if (index < 2 * n) {
        c.type  = Command::SetStepVelocity;
        c.step  = static_cast<uint8_t>(index - n);
        c.value = static_cast<float>(snapshot_.step_velocity[index - n]);
    } else if (index < 3 * n) {
        c.type  = Command::SetStepPitch;
        c.step  = static_cast<uint8_t>(index - 2 * n);
        c.value = static_cast<float>(snapshot_.step_pitch[index - 2 * n]);
    } else if (index < 4 * n) {
        c.type  = Command::SetStepOffset;
        c.step  = static_cast<uint8_t>(index - 3 * n);
        c.value = static_cast<float>(snapshot_.step_offset[index - 3 * n]);
    } else if (index < 5 * n) {
        c.type  = Command::SetStepPan;
        c.step  = static_cast<uint8_t>(index - 4 * n);
        c.value = static_cast<float>(snapshot_.step_pan[index - 4 * n]);
    } else if (index == 5 * n) {
        c.type   = Command::SetTrackPattern;
        c.length = snapshot_.pattern;
    } else if (index == 5 * n + 1) {
        c.type  = Command::SetTrackQuantize;
        c.value = static_cast<float>(snapshot_.quantize);
    } else {
        int k = index - (5 * n + 2);
        for (int s = 0; s < n; ++s) {
            if (k >= snapshot_.extra_count[s]) {
                k -= snapshot_.extra_count[s];
                continue;
            }
            const Hit& h = snapshot_.extra[s][k];
            c.type       = Command::SetExtraHit;
            c.step       = static_cast<uint8_t>(s);
            c.slot       = static_cast<uint8_t>(k + 1);
            c.offset     = h.offset;
            c.value      = static_cast<float>(h.velocity);
            c.pitch      = h.pitch;
            c.pan        = h.pan;
            return true;
        }
        return false;
    }
    return true;
}

void UiController::pump_restore() {
    while (restoring_) {
        Command c;
        if (!restore_command(restore_next_, c)) {
            restoring_ = false;
            break;
        }
        if (!sink_.push(c)) break;
        ++restore_next_;
    }
}

void UiController::on_pot_bpm(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    const int bpm = BPM_MIN + static_cast<int>(std::lround(v * (BPM_MAX - BPM_MIN)));
    if (bpm == bpm_) return;  // le potard bouge à peine : pas de commande inutile

    Command c;
    c.type  = Command::SetBpm;
    c.value = static_cast<float>(bpm);
    if (sink_.push(c)) {
        bpm_ = bpm;
        mark_dirty();
    }
}

// ============================================================================================
// Menus
// ============================================================================================

void UiController::open_menu(MenuKind kind) {
    menu_kind_   = kind;
    menu_origin_ = view_;
    menu_cursor_ = 0;
    confirm_     = false;
    view_        = View::Menu;
}

void UiController::close_menu() {
    confirm_ = false;
    view_    = menu_origin_;
}

int UiController::menu_item_count() const {
    return menu_kind_ == MenuKind::LoopItem || menu_kind_ == MenuKind::Keyboard ? 2 : 3;
}

void UiController::on_menu_ok() {
    switch (menu_kind_) {
        case MenuKind::Track:
            if (menu_cursor_ == 0) {
                if (library_ && library_->count() > 0) open_add(true);
            } else if (menu_cursor_ == 1) {
                if (confirm_) delete_selected();
                else confirm_ = true;  // première validation : on demande confirmation
            } else {
                close_menu();  // Annuler
            }
            break;

        case MenuKind::Loop:
            if (menu_cursor_ == 0) {
                open_loops();
            } else if (menu_cursor_ == 1) {
                if (current_loop_ >= 0 && !confirm_) {  // écraser une loop existante : confirmation
                    confirm_ = true;
                    break;
                }
                save_loop();
                close_menu();
            } else {
                close_menu();
            }
            break;

        case MenuKind::Keyboard:
            if (menu_cursor_ == 0) undo_take();
            close_menu();
            break;

        case MenuKind::LoopItem:
            if (menu_cursor_ == 0) {
                if (!confirm_) {
                    confirm_ = true;
                } else {
                    delete_loop(loop_item_);
                    close_menu();
                }
            } else {
                close_menu();
            }
            break;
    }
}

// ============================================================================================
// Loops : liste, chargement, sauvegarde, suppression
// ============================================================================================

void UiController::open_loops() {
    loops_cursor_ = current_loop_ >= 0 && current_loop_ < profile_.loop_count ? current_loop_ : 0;
    loops_scroll_ = 0;
    reveal(loops_cursor_, loops_scroll_);
    confirm_ = false;
    view_    = View::Loops;
}

// Valider dans la liste : charge la loop, ou vide la machine pour « + Nouvelle loop ». Si la loop
// courante a des modifications non sauvegardées, une seconde validation est demandée.
void UiController::on_loops_ok() {
    if (dirty_ && !confirm_) {
        confirm_ = true;
        return;
    }
    confirm_ = false;

    if (loops_cursor_ < profile_.loop_count) load_loop(loops_cursor_);
    else new_loop();
}

// Prépare le chargement : on retrouve et on charge les samples tout de suite (les pistes dont le
// sample manque sont sautées), on met le modèle de l'UI à jour, puis les commandes partent par
// petits paquets dans pump_load() pour ne jamais bloquer ni déborder la file.
void UiController::load_loop(int slot) {
    if (!library_ || slot < 0 || slot >= profile_.loop_count) {
        notify(" Echec de l'action");
        return;
    }
    const profile::LoopData& loop = profile_.loops[slot];

    plan_count_ = 0;
    int missing = 0;
    for (int t = 0; t < loop.track_count; ++t) {
        const int sample = loop.tracks[t].sample;
        const int index  = sample < profile_.sample_count ? library_->find(profile_.samples[sample]) : -1;
        PlannedTrack& p  = plan_[plan_count_];
        if (index < 0 || !library_->load(index, p.data, p.length)) {
            ++missing;
            continue;
        }
        p.loop_track = t;
        p.channels   = library_->channels(p.data);
        ++plan_count_;
    }

    // Numéro de la première commande de chaque piste : ClearAll et SetBpm d'abord, puis pour chaque
    // piste 4 commandes de base et une par réglage qui n'est pas la valeur par défaut (voir count_extras).
    int next_cmd = 2;
    for (int k = 0; k < plan_count_; ++k) {
        PlannedTrack& p = plan_[k];
        p.first_cmd     = next_cmd;
        p.extra         = count_extras(loop.tracks[p.loop_track]);
        next_cmd += 4 + p.extra;
    }

    // Modèle de l'UI : les pistes retrouvées, numérotées dans l'ordre sur les pistes du moteur.
    track_count_ = 0;
    for (Track& t : tracks_) t = Track{};
    for (int k = 0; k < plan_count_; ++k) {
        const profile::TrackData& td = loop.tracks[plan_[k].loop_track];
        const int index = add_track(profile_.samples[td.sample], static_cast<uint8_t>(k), td.pattern, td.gain, td.pitch,
                                    td.step_velocity, td.step_pitch);
        Track& t        = tracks_[index];
        t.quantize      = td.quantize;
        t.width        = td.width;
        t.balance      = td.balance;
        t.attack       = td.attack;
        t.decay        = td.decay;
        t.sustain      = td.sustain;
        t.release      = td.release;
        for (int st = 0; st < cfg::MAX_STEPS; ++st) {
            t.step_pan[st]    = td.step_pan[st];
            t.step_offset[st] = td.step_offset[st];
            t.extra_count[st] = td.extra_count[st];
            for (int e = 0; e < cfg::MAX_EXTRA_HITS; ++e) t.extra[st][e] = td.extra[st][e];
        }
    }

    bpm_          = static_cast<int>(std::lround(loop.bpm));
    current_loop_ = slot;
    dirty_        = false;
    confirm_      = false;
    view_         = View::Tracks;
    cursor_       = 0;
    scroll_       = 0;

    load_slot_  = slot;
    load_next_  = 0;
    load_total_ = next_cmd;
    loading_    = true;
    pump_load();

    if (missing == 1) notify(" 1 sample manquant");
    else if (missing > 1) {
        char text[22];
        std::snprintf(text, sizeof text, " %d samples manquants", clamp_int(missing, 0, 99));
        notify(text);
    }
}

// Nombre de commandes de réglage d'une piste en plus des 4 de base : une par vélocité, pitch ou décalage
// qui s'écarte de la valeur par défaut, une par note supplémentaire, une pour la quantification si elle
// n'est pas nulle.
int UiController::count_extras(const profile::TrackData& td) {
    int n = (td.quantize != 0) + (td.width != 0) + (td.balance != 0) + (td.attack != 0) + (td.decay != 0) + (td.sustain != 100) +
            (td.release != cfg::ENV_DEFAULT_RELEASE_MS);
    for (int s = 0; s < cfg::MAX_STEPS; ++s) {
        n += td.step_velocity[s] != cfg::VELOCITY_MAX;
        n += td.step_pitch[s] != 0;
        n += td.step_pan[s] != 0;
        n += td.step_offset[s] != 0;
        n += td.extra_count[s];
    }
    return n;
}

// La `index`-ième commande de réglage d'une piste : les vélocités, puis les pitchs, les balances des pas, les
// décalages, les notes supplémentaires (pas par pas), la quantification, la largeur et la balance de la piste.
bool UiController::extra_command(const profile::TrackData& td, int index, int track, Command& c) {
    c.track = static_cast<uint8_t>(track);

    for (int s = 0; s < cfg::MAX_STEPS; ++s) {
        if (td.step_velocity[s] == cfg::VELOCITY_MAX) continue;
        if (index-- == 0) {
            c.type  = Command::SetStepVelocity;
            c.step  = static_cast<uint8_t>(s);
            c.value = static_cast<float>(td.step_velocity[s]);
            return true;
        }
    }
    for (int s = 0; s < cfg::MAX_STEPS; ++s) {
        if (td.step_pitch[s] == 0) continue;
        if (index-- == 0) {
            c.type  = Command::SetStepPitch;
            c.step  = static_cast<uint8_t>(s);
            c.value = static_cast<float>(td.step_pitch[s]);
            return true;
        }
    }
    for (int s = 0; s < cfg::MAX_STEPS; ++s) {
        if (td.step_pan[s] == 0) continue;
        if (index-- == 0) {
            c.type  = Command::SetStepPan;
            c.step  = static_cast<uint8_t>(s);
            c.value = static_cast<float>(td.step_pan[s]);
            return true;
        }
    }
    for (int s = 0; s < cfg::MAX_STEPS; ++s) {
        if (td.step_offset[s] == 0) continue;
        if (index-- == 0) {
            c.type  = Command::SetStepOffset;
            c.step  = static_cast<uint8_t>(s);
            c.value = static_cast<float>(td.step_offset[s]);
            return true;
        }
    }
    for (int s = 0; s < cfg::MAX_STEPS; ++s) {
        for (int e = 0; e < td.extra_count[s]; ++e) {
            if (index-- != 0) continue;
            const Hit& h = td.extra[s][e];
            c.type       = Command::SetExtraHit;
            c.step       = static_cast<uint8_t>(s);
            c.slot       = static_cast<uint8_t>(e + 1);
            c.offset     = h.offset;
            c.value      = static_cast<float>(h.velocity);
            c.pitch      = h.pitch;
            c.pan        = h.pan;
            return true;
        }
    }
    if (td.quantize != 0 && index-- == 0) {
        c.type  = Command::SetTrackQuantize;
        c.value = static_cast<float>(td.quantize);
        return true;
    }
    if (td.width != 0 && index-- == 0) {
        c.type  = Command::SetTrackWidth;
        c.value = static_cast<float>(td.width);
        return true;
    }
    if (td.balance != 0 && index-- == 0) {
        c.type  = Command::SetTrackBalance;
        c.value = static_cast<float>(td.balance);
        return true;
    }
    if (td.attack != 0 && index-- == 0) {
        c.type  = Command::SetTrackAttack;
        c.value = static_cast<float>(td.attack);
        return true;
    }
    if (td.decay != 0 && index-- == 0) {
        c.type  = Command::SetTrackDecay;
        c.value = static_cast<float>(td.decay);
        return true;
    }
    if (td.sustain != 100 && index-- == 0) {
        c.type  = Command::SetTrackSustain;
        c.value = static_cast<float>(td.sustain);
        return true;
    }
    if (td.release != cfg::ENV_DEFAULT_RELEASE_MS && index-- == 0) {
        c.type  = Command::SetTrackRelease;
        c.value = static_cast<float>(td.release);
        return true;
    }
    return false;
}

// Commande numéro `index` du chargement : ClearAll, SetBpm, puis pour chaque piste 4 commandes de
// base (sample, gain, pitch, pattern), puis une commande par réglage de pas (voir extra_command).
bool UiController::next_load_command(int index, Command& c) const {
    if (index < 0 || index >= load_total_) return false;

    const profile::LoopData& loop = profile_.loops[load_slot_];
    c                             = Command{};
    if (index == 0) {
        c.type = Command::ClearAll;
        return true;
    }
    if (index == 1) {
        c.type  = Command::SetBpm;
        c.value = loop.bpm;
        return true;
    }

    int k = 0;
    while (k + 1 < plan_count_ && index >= plan_[k + 1].first_cmd) ++k;
    const PlannedTrack&       p      = plan_[k];
    const profile::TrackData& td     = loop.tracks[p.loop_track];
    const int                 offset = index - p.first_cmd;
    c.track                          = static_cast<uint8_t>(k);

    switch (offset) {
        case 0:
            c.type     = Command::SetTrackSample;
            c.data     = p.data;
            c.length   = p.length;
            c.channels = static_cast<uint8_t>(p.channels);
            return true;
        case 1:
            c.type  = Command::SetTrackGain;
            c.value = td.gain;
            return true;
        case 2:
            c.type  = Command::SetTrackPitch;
            c.value = td.pitch;
            return true;
        case 3:
            c.type   = Command::SetTrackPattern;
            c.length = td.pattern;
            return true;
        default:
            return extra_command(td, offset - 4, k, c);
    }
}

// Envoie autant de commandes que la file en accepte ; la suite part au prochain appel.
void UiController::pump_load() {
    while (loading_) {
        Command c;
        if (!next_load_command(load_next_, c)) {
            loading_ = false;
            break;
        }
        if (!sink_.push(c)) break;
        ++load_next_;
    }
}

void UiController::new_loop() {
    Command c;
    c.type = Command::ClearAll;
    if (!sink_.push(c)) {
        notify(" Echec de l'action");
        return;
    }
    track_count_ = 0;
    for (Track& t : tracks_) t = Track{};
    current_loop_ = -1;
    dirty_        = false;
    cursor_       = 0;
    scroll_       = 0;
    view_         = View::Tracks;
}

// La loop courante sous forme de LoopData ; ses samples entrent dans la table du profil.
bool UiController::build_loop(profile::LoopData& out) {
    out             = profile::LoopData{};
    out.bpm         = static_cast<float>(bpm_);
    out.track_count = static_cast<uint8_t>(track_count_);
    for (int i = 0; i < track_count_; ++i) {
        const Track& t     = tracks_[i];
        const int    index = profile_.add_sample(t.file);
        if (index < 0) return false;
        profile::TrackData& td = out.tracks[i];
        td.sample  = static_cast<uint8_t>(index);
        td.gain    = t.gain;
        td.pitch   = t.pitch;
        td.pattern = t.pattern;
        for (int s = 0; s < cfg::MAX_STEPS; ++s) {
            td.step_velocity[s] = t.step_velocity[s];
            td.step_pitch[s]    = t.step_pitch[s];
            td.step_pan[s]      = t.step_pan[s];
            td.step_offset[s]   = t.step_offset[s];
            td.extra_count[s]   = t.extra_count[s];
            for (int e = 0; e < cfg::MAX_EXTRA_HITS; ++e) td.extra[s][e] = t.extra[s][e];
        }
        td.quantize = t.quantize;
        td.width    = t.width;
        td.balance  = t.balance;
        td.attack   = t.attack;
        td.decay    = t.decay;
        td.sustain  = t.sustain;
        td.release  = t.release;
    }
    return true;
}

void UiController::save_loop() {
    if (!store_) {
        notify(" Pas de stockage");
        return;
    }
    // Une loop déjà sauvegardée écrase son emplacement ; une loop neuve prend le suivant.
    const int slot = current_loop_ >= 0 ? current_loop_ : profile_.loop_count;
    if (slot >= profile::MAX_LOOPS) {
        notify(" Profil plein");
        return;
    }

    // Le profil en mémoire peut avoir une longueur d'avance sur le disque : si l'écriture échoue, la loop
    // reste à son emplacement (`dirty_` reste vrai) et un nouvel essai réécrit simplement tout le profil.
    profile_.compact_samples();  // avant d'ajouter : une table « pleine » l'est vraiment
    profile::LoopData loop;
    if (!build_loop(loop)) {
        notify(" Trop de samples");
        return;
    }
    profile_.loops[slot] = loop;
    if (slot == profile_.loop_count) ++profile_.loop_count;
    current_loop_ = slot;
    dirty_        = true;
    profile_.compact_samples();

    if (!store_->save(profile_)) {
        notify(" Echec sauvegarde");
        return;
    }
    dirty_ = false;
    notify(" Loop sauvegardee");
}

void UiController::delete_loop(int slot) {
    if (!store_ || slot < 0 || slot >= profile_.loop_count) return;

    profile_.remove_loop(slot);
    profile_.compact_samples();
    const bool saved = store_->save(profile_);

    // La loop courante garde son contenu dans la machine, mais n'a plus d'emplacement.
    if (current_loop_ == slot) {
        current_loop_ = -1;
        dirty_        = true;
    } else if (current_loop_ > slot) {
        --current_loop_;
    }
    const int items = profile_.loop_count + 1;
    if (loops_cursor_ > items - 1) loops_cursor_ = items - 1;
    reveal(loops_cursor_, loops_scroll_);
    notify(saved ? " Loop supprimee" : " Echec sauvegarde");
}

// ============================================================================================
// Écoute
// ============================================================================================

// Écoute : on attend que la roue s'arrête, pour ne pas jouer tous les samples qu'on survole.
void UiController::schedule_preview() {
    preview_pending_ = add_cursor_;
    preview_due_     = now_ms_ + PREVIEW_DELAY_MS;
}

void UiController::play_preview(int index) {
    const int16_t* data   = nullptr;
    uint32_t       length = 0;
    if (!library_->load(index, data, length)) return;  // l'écoute est facultative : pas d'erreur affichée

    Command c;
    c.type     = Command::PreviewSample;
    c.data     = data;
    c.length   = length;
    c.channels = static_cast<uint8_t>(library_->channels(data));
    if (!sink_.push(c)) {
        library_->discard(data);
        return;
    }
    preview_active_ = true;
}

void UiController::stop_preview() {
    preview_pending_ = -1;
    if (!preview_active_) return;

    Command c;
    c.type = Command::StopPreview;
    sink_.push(c);  // si la file est pleine, l'écoute se termine seule et sera remplacée à la suivante
    preview_active_ = false;
}

// ============================================================================================
// Mise à jour et dessin
// ============================================================================================

void UiController::update(const EngineState& s, uint32_t now_ms) {
    now_ms_    = now_ms;
    playing_   = s.playing.load();
    bpm_       = static_cast<int>(std::lround(s.bpm.load()));
    play_step_ = playing_ ? s.step.load() : -1;

    record_phase_  = s.record_phase.load();
    precount_step_ = s.precount_step.load();
    pump_load();
    pump_restore();
    if (events_) {
        StepEvent ev;
        while (events_->pop_step_event(ev)) apply_step_event(ev);
    }
    const uint32_t lost = s.record_lost.load();
    if (lost != lost_seen_) {
        lost_seen_ = lost;
        notify(" Frappes perdues");
    }

    if (audition_pending_) {
        if (view_ == View::Pattern && edit_mode_ != EditMode::Edit && step_cursor_ >= 0) audition_selected_step();
        else audition_pending_ = false;
    }

    for (int i = 0; i < track_count_; ++i) {
        Track&         t   = tracks_[i];
        const uint32_t cnt = s.triggers[t.engine_track].load();
        if (t.seen && cnt != t.last_triggers) t.lit_until = now_ms + LIT_MS;
        t.last_triggers = cnt;
        t.seen          = true;
    }

    if (view_ == View::Add && library_ && preview_pending_ >= 0 &&
        static_cast<int32_t>(now_ms - preview_due_) >= 0) {
        play_preview(preview_pending_);
        preview_pending_ = -1;
    }
}

void UiController::render(Canvas& c) const {
    c.clear();
    switch (view_) {
        case View::Tracks:
            render_header(c, "Pistes", true);
            render_tracks(c);
            if (cursor_ < 0) c.invert_rect(0, 0, Canvas::W, Canvas::CHAR_H);  // titre sélectionné
            break;
        case View::Pattern: {
            // Le mode en haut à droite : une lettre, E, V ou P. Elle se place à gauche du BPM : le titre
            // est donc coupé à 13 caractères dans cette vue.
            // Un réglage de la piste tenu (touche noire) remplace la lettre par son nom, plus long : titre coupé à 8.
            static const char* const LETTERS[] = {"E", "V", "P", "T", "B"};
            static const char* const SETTINGS[] = {"", "Quant", "Wide", "Bal", "Gain", "Atk", "Dec", "Sus", "Rel"};
            const bool held = setting_ != TrackSetting::None;
            render_header(c, tracks_[selected_].name, false, held ? SETTINGS[static_cast<int>(setting_)] : LETTERS[static_cast<int>(edit_mode_)], held ? 8 : 13);
            if (setting_ >= TrackSetting::Attack) render_envelope(c);  // la forme de l'enveloppe remplace la timeline
            else render_pattern(c);
            break;
        }
        case View::Add:
            render_header(c, "Samples", false);  // même vue pour l'utilisateur : ajout ou remplacement
            render_add(c);
            break;
        case View::Menu: {
            char title[17];
            if (menu_kind_ == MenuKind::Track) std::snprintf(title, sizeof title, "%s", tracks_[selected_].name);
            else if (menu_kind_ == MenuKind::Loop) std::snprintf(title, sizeof title, "Loop");
            else if (menu_kind_ == MenuKind::LoopItem) std::snprintf(title, sizeof title, "Loop %d", loop_item_ + 1);
            else std::snprintf(title, sizeof title, "%s", tracks_[selected_].name);
            render_header(c, title, false);
            render_menu(c);
            break;
        }
        case View::Loops:
            render_header(c, "Loops", false);
            render_loops(c);
            break;
        case View::Keyboard: {
            // Mode Clavier / Rec : le nom du mode (ou le décompte, ou REC) à droite, et la timeline du pattern.
            char text[8];
            if (record_phase_ == 1) {
                // Décompte des temps restants : 4 pendant le premier temps du pré-compte, puis 3, 2, 1.
                std::snprintf(text, sizeof text, "PRE %d", clamp_int(precount_step_ < 0 ? 4 : 4 - precount_step_ / 4, 0, 9));
            } else {
                std::snprintf(text, sizeof text, "%s", record_phase_ == 2 ? "REC" : "KEY");
            }
            render_header(c, tracks_[selected_].name, false, text, 8);
            render_pattern(c, true);
            break;
        }
    }
    if (error_shown()) render_notice(c);
}

// En-tête : titre à gauche (16 caractères au plus), à droite l'état lecture / pause et le BPM ;
// dans la vue Pistes, le numéro de la loop et son indicateur de modification s'y ajoutent.
void UiController::render_header(Canvas& c, const char* title, bool show_loop, const char* mode_text, int max_title) const {
    char shown[17];
    if (max_title > 16) max_title = 16;
    std::snprintf(shown, sizeof shown, "%.*s", max_title, title);
    c.text(0, 0, shown);

    const int icon_x = Canvas::W - 6;
    if (playing_) {
        for (int col = 0; col < 4; ++col) c.vline(icon_x + col, col, 7 - 2 * col);  // triangle
    } else {
        c.rect(icon_x, 0, 2, 7, true);       // pause : deux barres
        c.rect(icon_x + 3, 0, 2, 7, true);
    }

    char bpm[8];
    std::snprintf(bpm, sizeof bpm, "%d", bpm_);
    const int bpm_x = icon_x - 4 - Canvas::text_width(bpm);
    c.text(bpm_x, 0, bpm);

    if (mode_text) c.text(bpm_x - 6 - Canvas::CHAR_W * static_cast<int>(std::strlen(mode_text)), 0, mode_text);

    if (show_loop) {
        char loop[8];
        if (current_loop_ < 0) std::snprintf(loop, sizeof loop, "L*");
        else std::snprintf(loop, sizeof loop, "L%d%s", clamp_int(current_loop_ + 1, 0, 99), dirty_ ? "*" : "");
        c.text(bpm_x - 6 - Canvas::text_width(loop), 0, loop);
    }
}

void UiController::render_tracks(Canvas& c) const {
    const int items = track_count_ + 1;

    for (int row = 0; row < LIST_ROWS; ++row) {
        const int i = scroll_ + row;
        if (i >= items) break;

        const int  y   = (row + 1) * Canvas::CHAR_H;
        const bool sel = (i == cursor_);
        if (sel) c.rect(0, y, Canvas::W, Canvas::CHAR_H, true);  // barre de sélection pleine largeur

        char label[NAME_LEN + 4];
        if (i < track_count_) std::snprintf(label, sizeof label, " %s", tracks_[i].name);
        else                  std::snprintf(label, sizeof label, " + Ajouter piste");
        c.text(0, y, label, sel);

        // Indicateur de jeu : un petit carré à droite quand la piste vient de jouer.
        if (i < track_count_ && is_lit(tracks_[i])) c.rect(Canvas::W - 6, y + 2, 4, 4, true, !sel);
    }
}

// Vue Pattern : 16 pas de 8 px (7 de boîte + 1 d'espace) = 128 px, alignés sur les 16 boutons.
//
// Edit     : boîte pleine = joué, contour = silence.
// Velocity : un pas joué est un contour dont l'intérieur se remplit du bas vers le haut selon la
//            vélocité (1 à 5 lignes) ; vélocité maximale = boîte pleine, comme en Edit.
// Pitch    : un pas joué est un contour avec une ligne centrale (pitch 0) ; un pitch positif remplit
//            vers le haut (jusqu'à 2 lignes), un pitch négatif vers le bas.
// L'écran est en 1 bit : pas de transparence, donc des niveaux de remplissage. Le texte donne la
// valeur exacte.
void UiController::render_pattern(Canvas& c, bool timeline) const {
    constexpr int Y = 24;  // haut des boîtes de pas

    // Timeline (mode Clavier / Rec) : les boîtes comme en Edit, sans sélection de pas, sans jauge.
    const EditMode mode        = timeline ? EditMode::Edit : edit_mode_;
    const int      cursor      = timeline ? -1 : step_cursor_;
    const TrackSetting setting = timeline ? TrackSetting::None : setting_;

    const Track& t = tracks_[selected_];

    for (int i = 0; i < cfg::MAX_STEPS; ++i) {
        const int  x  = i * 8;
        const bool on = (t.pattern >> i) & 1u;


        if (mode == EditMode::Edit || mode == EditMode::Timing || !on) {
            c.rect(x, Y, 7, 7, on && (mode == EditMode::Edit || mode == EditMode::Timing));  // plein = joué, contour = silence
        } else if (mode == EditMode::Balance) {
            // Un trait central ; à gauche ou à droite, une colonne de plus se remplit dès 10 et deux dès 30.
            c.rect(x, Y, 7, 7, false);
            const int p    = t.step_pan[i];
            const int cols = (p < 0 ? -p : p) >= 30 ? 2 : (p < 0 ? -p : p) >= 10 ? 1 : 0;
            c.vline(x + 3, Y + 1, 5);
            if (cols > 0 && p < 0) c.rect(x + 3 - cols, Y + 1, cols, 5, true);
            if (cols > 0 && p > 0) c.rect(x + 4, Y + 1, cols, 5, true);
        } else if (mode == EditMode::Velocity) {
            c.rect(x, Y, 7, 7, false);
            const int rows = 1 + ((t.step_velocity[i] - cfg::VELOCITY_MIN) * 4 + 63) / 126;  // 1..5
            c.rect(x + 1, Y + 6 - rows, 5, rows, true);
        } else {
            c.rect(x, Y, 7, 7, false);
            const int p    = t.step_pitch[i];
            const int rows = ((p < 0 ? -p : p) * 2 + 6) / 12;  // 0..2
            if (p >= 0) c.rect(x + 1, Y + 3 - rows, 5, rows + 1, true);  // la ligne centrale, plus vers le haut
            else        c.rect(x + 1, Y + 3, 5, rows + 1, true);         // la ligne centrale, plus vers le bas
        }
        // Un pas à plusieurs notes (roulement) : un petit point par note sous la boîte.
        if (on) {
            for (int n = 0; n < t.extra_count[i] + 1 && t.extra_count[i] > 0; ++n) c.set_pixel(x + 2 * n, Y + 8);
        }

        if (i % 4 == 0) c.vline(x + 3, Y - 6, 3);            // repère de temps tous les 4 pas
        if (i == cursor) c.hline(x, Y - 2, 7);         // pas sélectionné
        if (i == play_step_) c.rect(x, Y + 9, 7, 2, true);   // pas en cours de lecture
    }

    // Bas de l'écran. Edit : rien. Les autres modes : une jauge horizontale pour le pas sélectionné
    // (s'il est joué), avec sa valeur exacte à droite.
    constexpr int GAUGE_Y = 6 * Canvas::CHAR_H;   // même rangée que le texte d'Edit
    constexpr int GAUGE_W = 100;                  // largeur de la jauge, contour compris
    const bool    selected_on = cursor >= 0 && ((t.pattern >> cursor) & 1u);

    if (setting != TrackSetting::None) {
        // Réglage de la piste tenu : sa jauge remplace celle du pas. Quantifier : de la gauche vers la droite
        // (rien à 0 %) ; Pan et Balance : du centre, comme la balance d'une note.
        char value[8];
        c.rect(0, GAUGE_Y, GAUGE_W, 7, false);
        if (setting == TrackSetting::Quantize) {
            const int q    = t.quantize;
            const int fill = q * (GAUGE_W - 3) / cfg::QUANTIZE_MAX;
            if (fill > 0) c.rect(1, GAUGE_Y + 1, fill, 5, true);
            std::snprintf(value, sizeof value, "%d%%", q);
        } else if (setting == TrackSetting::Gain) {
            // De la gauche vers la droite : muet à vide, -40 dB tout à gauche, +6 dB tout à droite ; un trait marque 0 dB.
            constexpr int MIN_DB = -40, MAX_DB = 6;
            const double  db     = t.gain > 0.0f ? 20.0 * std::log10(static_cast<double>(t.gain)) : static_cast<double>(MIN_DB - 1);
            const double  pos    = t.gain > 0.0f ? (db < MIN_DB ? 0.0 : db > MAX_DB ? 1.0 : (db - MIN_DB) / (MAX_DB - MIN_DB)) : 0.0;
            const int     fill   = static_cast<int>(std::lround(pos * (GAUGE_W - 3)));
            if (fill > 0) c.rect(1, GAUGE_Y + 1, fill, 5, true);
            c.vline(1 + static_cast<int>(std::lround(static_cast<double>(0 - MIN_DB) / (MAX_DB - MIN_DB) * (GAUGE_W - 3))), GAUGE_Y - 1, 9);
            if (t.gain <= 0.0f) std::snprintf(value, sizeof value, "-inf");
            else {
                // Integer formatting only (no float printf on the Teensy); bounded so that "-99.9dB" is the longest text.
                const int tenths = clamp_int(static_cast<int>(std::lround(db * 10.0)), -999, 999);
                const int mag    = tenths < 0 ? -tenths : tenths;
                std::snprintf(value, sizeof value, "%c%d.%ddB", tenths < 0 ? '-' : '+', mag / 10, mag % 10);
            }
        } else if (setting == TrackSetting::Width) {
            // Du centre (0 = inchangé) : vers la gauche pour le mono (100M = mono complet), vers la droite pour
            // élargir (100W = côtés doublés). Presque un demi-pixel par point : 48 pixels pour 100.
            const int v      = t.width;
            const int center = GAUGE_W / 2;
            const int cols   = ((v < 0 ? -v : v) * 48 + 50) / 100;
            if (v > 0) c.rect(center + 1, GAUGE_Y + 1, cols, 5, true);
            else if (v < 0) c.rect(center - cols, GAUGE_Y + 1, cols, 5, true);
            c.vline(center, GAUGE_Y - 1, 9);
            if (v == 0) std::snprintf(value, sizeof value, "0");
            else        std::snprintf(value, sizeof value, "%d%c", v < 0 ? -v : v, v < 0 ? 'M' : 'W');
        } else {
            const int v      = t.balance;
            const int center = GAUGE_W / 2;
            const int cols   = ((v < 0 ? -v : v) * 48 + 25) / 50;
            if (v > 0) c.rect(center + 1, GAUGE_Y + 1, cols, 5, true);
            else if (v < 0) c.rect(center - cols, GAUGE_Y + 1, cols, 5, true);
            c.vline(center, GAUGE_Y - 1, 9);
            pan_text(value, sizeof value, v);
        }
        c.text(GAUGE_W + 4, GAUGE_Y, value);
    } else if (mode != EditMode::Edit && selected_on) {
        char value[8];
        c.rect(0, GAUGE_Y, GAUGE_W, 7, false);  // contour de la jauge

        if (mode == EditMode::Velocity) {
            // De la gauche vers la droite : 1 pixel pour la vélocité 1, toute la jauge pour 127.
            const int v    = t.step_velocity[cursor];
            const int fill = 1 + (v - cfg::VELOCITY_MIN) * (GAUGE_W - 3) / (cfg::VELOCITY_MAX - cfg::VELOCITY_MIN);
            c.rect(1, GAUGE_Y + 1, fill, 5, true);
            std::snprintf(value, sizeof value, "%d", v);
        } else if (mode == EditMode::Timing) {
            // Du centre : vers la droite pour une note en retard, vers la gauche pour une note en avance.
            // 3/4 de pixel par graduation : 48 pixels pour 64 (50 % d'un pas).
            const int o      = t.step_offset[cursor];
            const int center = GAUGE_W / 2;
            const int cols   = ((o < 0 ? -o : o) * 3 + 2) / 4;
            if (o > 0) c.rect(center + 1, GAUGE_Y + 1, cols, 5, true);
            else if (o < 0) c.rect(center - cols, GAUGE_Y + 1, cols, 5, true);
            c.vline(center, GAUGE_Y - 1, 9);
            const int pct = static_cast<int>(std::lround(o * 100.0 / cfg::OFFSET_UNITS));
            if (pct == 0) std::snprintf(value, sizeof value, "0");
            else          std::snprintf(value, sizeof value, "%+d%%", pct);
        } else if (mode == EditMode::Balance) {
            // Du centre : vers la droite pour une note à droite, vers la gauche pour une note à gauche.
            // Presque 1 pixel par point : 48 pixels pour 50.
            const int p      = t.step_pan[cursor];
            const int center = GAUGE_W / 2;
            const int cols   = ((p < 0 ? -p : p) * 48 + 25) / 50;
            if (p > 0) c.rect(center + 1, GAUGE_Y + 1, cols, 5, true);
            else if (p < 0) c.rect(center - cols, GAUGE_Y + 1, cols, 5, true);
            c.vline(center, GAUGE_Y - 1, 9);
            pan_text(value, sizeof value, p);
        } else {
            // Du centre : vers la droite pour un pitch positif, vers la gauche pour un pitch négatif.
            // Un trait dépassant la jauge marque le 0.
            const int p      = t.step_pitch[cursor];
            const int center = GAUGE_W / 2;
            const int cols   = (p < 0 ? -p : p) * 4;  // 4 pixels par demi-ton : 48 pour +-12
            if (p > 0) c.rect(center + 1, GAUGE_Y + 1, cols, 5, true);
            else if (p < 0) c.rect(center - cols, GAUGE_Y + 1, cols, 5, true);
            c.vline(center, GAUGE_Y - 1, 9);
            if (p == 0) std::snprintf(value, sizeof value, "0");
            else        std::snprintf(value, sizeof value, "%+d", p);
        }
        c.text(GAUGE_W + 4, GAUGE_Y, value);
    }
}

// Temps d'un segment de l'enveloppe en largeur d'écran : logarithmique (0 ms = un pixel, 2000 ms = 27 pixels), pour
// que les temps courts comme les longs restent lisibles.
static int env_width(int ms) {
    if (ms <= 0) return 1;
    return 2 + static_cast<int>(std::lround(std::log2(1.0 + ms) * 2.3));
}

// Durée en texte court avec son unité : « 12ms » sous 100 ms, des dixièmes et centièmes de seconde sans zéro
// devant au-dessus (« .3s », « .15s »), des secondes à partir de 1000 ms (« 1s », « 1.5s »). Au plus 4 caractères,
// pour que la rangée des quatre valeurs tienne sur l'écran.
static void time_text(char* dst, std::size_t cap, int ms) {
    if (ms < 100) {
        std::snprintf(dst, cap, "%dms", ms);
    } else if (ms < 1000) {
        const int hundredths = (ms + 5) / 10;  // 100..999 ms -> 10..100 centièmes
        if (hundredths >= 100) std::snprintf(dst, cap, "1s");
        else if (hundredths % 10 == 0) std::snprintf(dst, cap, ".%ds", hundredths / 10);
        else std::snprintf(dst, cap, ".%02ds", hundredths);
    } else {
        const int tenths = (ms + 50) / 100;  // 1000..2000 ms -> 10..20 dixièmes
        if (tenths % 10 == 0) std::snprintf(dst, cap, "%ds", tenths / 10);
        else std::snprintf(dst, cap, "%d.%ds", tenths / 10, tenths % 10);
    }
}

// Réglage de l'enveloppe tenu (touches noires 5 à 8) : la forme classique d'un ADSR (montée, descente, palier,
// descente finale), avec le segment réglé en gras, et les quatre valeurs en bas, celle réglée en négatif.
void UiController::render_envelope(Canvas& c) const {
    const Track& t = tracks_[selected_];

    constexpr int TOP = 16, BASE = 46, SUSTAIN_W = 20;
    const int     wa = env_width(t.attack), wd = env_width(t.decay), wr = env_width(t.release);
    const int     total = wa + wd + SUSTAIN_W + wr;
    const int     x0    = (Canvas::W - total) / 2;
    const int     ys    = BASE - (BASE - TOP) * t.sustain / 100;

    const int xs[5] = {x0, x0 + wa, x0 + wa + wd, x0 + wa + wd + SUSTAIN_W, x0 + total};
    const int ye[5] = {BASE, TOP, ys, ys, BASE};
    for (int x = x0; x <= x0 + total; x += 4) c.set_pixel(x, BASE + 1);  // l'axe du temps, en pointillés

    for (int seg = 0; seg < 4; ++seg) {
        c.line(xs[seg], ye[seg], xs[seg + 1], ye[seg + 1]);
        const bool active = (seg == 0 && setting_ == TrackSetting::Attack) || (seg == 1 && setting_ == TrackSetting::Decay) ||
                            (seg == 2 && setting_ == TrackSetting::Sustain) || (seg == 3 && setting_ == TrackSetting::Release);
        if (active) c.line(xs[seg] + 1, ye[seg], xs[seg + 1] + 1, ye[seg + 1]);  // en gras
    }

    // Les valeurs avec leur unité : A, D, R en durées (ms ou s), S en %.
    char item[4][12];
    char value[8];
    time_text(value, sizeof value, t.attack);
    std::snprintf(item[0], sizeof item[0], "A%s", value);
    time_text(value, sizeof value, t.decay);
    std::snprintf(item[1], sizeof item[1], "D%s", value);
    std::snprintf(item[2], sizeof item[2], "S%d%%", t.sustain);
    time_text(value, sizeof value, t.release);
    std::snprintf(item[3], sizeof item[3], "R%s", value);
    int x = 0;
    for (int k = 0; k < 4; ++k) {
        const bool active = (k == 0 && setting_ == TrackSetting::Attack) || (k == 1 && setting_ == TrackSetting::Decay) ||
                            (k == 2 && setting_ == TrackSetting::Sustain) || (k == 3 && setting_ == TrackSetting::Release);
        c.text(x, 6 * Canvas::CHAR_H + 4, item[k], active);
        x += Canvas::text_width(item[k]) + 2;  // deux pixels entre deux valeurs : le pire cas (20 caractères) tient en 126
    }
}

// Vue Samples : les samples disponibles, la roue parcourt la liste, Valider choisit.
void UiController::render_add(Canvas& c) const {
    if (!library_) return;
    const int items = library_->count();

    for (int row = 0; row < LIST_ROWS; ++row) {
        const int i = add_scroll_ + row;
        if (i >= items) break;

        const int  y   = (row + 1) * Canvas::CHAR_H;
        const bool sel = (i == add_cursor_);
        if (sel) c.rect(0, y, Canvas::W, Canvas::CHAR_H, true);

        char name[NAME_LEN + 1];
        char label[NAME_LEN + 3];
        display_name(name, library_->name(i));
        std::snprintf(label, sizeof label, " %s", name);
        c.text(0, y, label, sel);
    }
}

// Menus : piste (changer le sample / supprimer / annuler), loop (charger / sauvegarder / annuler),
// loop de la liste (supprimer / annuler). Les actions destructrices demandent une confirmation.
void UiController::render_menu(Canvas& c) const {
    const char* labels[3];
    int         count = 3;
    switch (menu_kind_) {
        case MenuKind::Track:
            labels[0] = " Changer le sample";
            labels[1] = confirm_ ? " Confirmer ?" : " Supprimer la piste";
            labels[2] = " Annuler";
            break;
        case MenuKind::Loop:
            labels[0] = " Charger une loop";
            labels[1] = confirm_ ? " Ecraser ?" : " Sauvegarder la loop";
            labels[2] = " Annuler";
            break;
        case MenuKind::Keyboard:
            labels[0] = " Annuler la prise";
            labels[1] = " Annuler";
            count     = 2;
            break;
        case MenuKind::LoopItem:
            labels[0] = confirm_ ? " Confirmer ?" : " Supprimer la loop";
            labels[1] = " Annuler";
            count     = 2;
            break;
    }

    for (int i = 0; i < count; ++i) {
        const int  y   = (i + 1) * Canvas::CHAR_H;
        const bool sel = (i == menu_cursor_);
        if (sel) c.rect(0, y, Canvas::W, Canvas::CHAR_H, true);
        c.text(0, y, labels[i], sel);
    }
}

// Vue Loops : une ligne par loop du profil (« > » devant la loop courante), puis « + Nouvelle loop ».
void UiController::render_loops(Canvas& c) const {
    const int items = profile_.loop_count + 1;

    for (int row = 0; row < LIST_ROWS; ++row) {
        const int i = loops_scroll_ + row;
        if (i >= items) break;

        const int  y   = (row + 1) * Canvas::CHAR_H;
        const bool sel = (i == loops_cursor_);
        if (sel) c.rect(0, y, Canvas::W, Canvas::CHAR_H, true);

        char label[24];
        if (sel && confirm_) {
            std::snprintf(label, sizeof label, " Quitter sans sauv. ?");
        } else if (i < profile_.loop_count) {
            const profile::LoopData& loop = profile_.loops[i];
            std::snprintf(label, sizeof label, "%cL%d %dbpm %d piste%s", i == current_loop_ ? '>' : ' ', clamp_int(i + 1, 0, 99),
                          clamp_int(static_cast<int>(std::lround(loop.bpm)), 0, 999), clamp_int(loop.track_count, 0, 99),
                          loop.track_count == 1 ? "" : "s");
        } else {
            std::snprintf(label, sizeof label, " + Nouvelle loop");
        }
        c.text(0, y, label, sel);
    }
}

// Message sur la dernière ligne, par-dessus ce qui s'y trouve.
void UiController::render_notice(Canvas& c) const {
    const int y = LIST_ROWS * Canvas::CHAR_H;
    c.rect(0, y, Canvas::W, Canvas::CHAR_H, true, false);
    c.text(0, y, notice_);
}
