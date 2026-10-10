#include "ui_controller.h"

#include "keyboard_layout.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static_assert(cfg::MAX_STEPS <= 16, "le pattern d'une piste tient dans un uint16_t");

namespace {

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
        case InputEvent::PotBpm: on_pot_bpm(e.value); break;
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
            if (quantize_edit_) {
                adjust_quantize(delta);
                break;
            }
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
            adjust_step_value(delta);
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
    c.data   = data;
    c.length = length;
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
            if (quantize_edit_) quantize_edit_ = false;  // d'abord sortir du réglage
            else close_menu();
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

// Mode : Edit -> Velocity -> Pitch -> Timing -> Edit, dans la vue Pattern seulement.
void UiController::on_mode() {
    if (view_ != View::Pattern) return;
    switch (edit_mode_) {
        case EditMode::Edit:     edit_mode_ = EditMode::Velocity; break;
        case EditMode::Velocity: edit_mode_ = EditMode::Pitch; break;
        case EditMode::Pitch:    edit_mode_ = EditMode::Timing; break;
        case EditMode::Timing:   edit_mode_ = EditMode::Edit; break;
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
        case EditMode::Edit:     return false;
    }
    auto value = [&](int n) -> int {  // n = 0 : note principale ; n >= 1 : note supplémentaire n
        if (n == 0) {
            return edit_mode_ == EditMode::Velocity ? t.step_velocity[s] : edit_mode_ == EditMode::Pitch ? t.step_pitch[s] : t.step_offset[s];
        }
        const Hit& h = t.extra[s][n - 1];
        return edit_mode_ == EditMode::Velocity ? h.velocity : edit_mode_ == EditMode::Pitch ? h.pitch : h.offset;
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
                                               : Command::SetStepOffset;
    c.value = static_cast<float>(value(0) + d);
    if (!sink_.push(c)) return false;
    if (edit_mode_ == EditMode::Velocity)   t.step_velocity[s] = static_cast<uint8_t>(t.step_velocity[s] + d);
    else if (edit_mode_ == EditMode::Pitch) t.step_pitch[s]    = static_cast<int8_t>(t.step_pitch[s] + d);
    else                                    t.step_offset[s]   = static_cast<int8_t>(t.step_offset[s] + d);

    for (int n = 1; n < notes; ++n) {
        Hit h = t.extra[s][n - 1];
        if (edit_mode_ == EditMode::Velocity)   h.velocity = static_cast<uint8_t>(h.velocity + d);
        else if (edit_mode_ == EditMode::Pitch) h.pitch    = static_cast<int8_t>(h.pitch + d);
        else                                    h.offset   = static_cast<int8_t>(h.offset + d);

        Command e;
        e.type   = Command::SetExtraHit;
        e.track  = t.engine_track;
        e.step   = static_cast<uint8_t>(s);
        e.slot   = static_cast<uint8_t>(n);
        e.offset = h.offset;
        e.value  = static_cast<float>(h.velocity);
        e.pitch  = h.pitch;
        if (!sink_.push(e)) break;  // file pleine : le modèle suit ce que le moteur a reçu
        t.extra[s][n - 1] = h;
    }
    return true;
}

// Menu de la piste, ligne « Quantifier » : la force de quantification change à la roue, par pas de 5 %.
void UiController::adjust_quantize(int delta) {
    Track& t = tracks_[selected_];
    int    q = t.quantize + delta * QUANTIZE_PER_NOTCH;
    if (q < 0) q = 0;
    if (q > cfg::QUANTIZE_MAX) q = cfg::QUANTIZE_MAX;
    if (q == t.quantize) return;

    Command c;
    c.type  = Command::SetTrackQuantize;
    c.track = t.engine_track;
    c.value = static_cast<float>(q);
    if (!sink_.push(c)) return;
    t.quantize = static_cast<uint8_t>(q);
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
        play_key(keyboard::BLACK_PITCH[index], keyboard::WHITE_KEYS + index);
        return;
    }
    if (index != 0) return;

    if (view_ == View::Pattern) {
        enter_keyboard(selected_);
    } else if (view_ == View::Tracks && cursor_ >= 0 && cursor_ < track_count_) {
        enter_keyboard(cursor_);
    } else {
        return;
    }
    play_key(keyboard::BLACK_PITCH[0], keyboard::WHITE_KEYS);
}

// Touche blanche, dans la vue Clavier : les 14 premières jouent, les deux dernières sont inactives.
void UiController::on_white_key(int index) {
    if (index < 0 || index >= keyboard::WHITE_KEYS) return;
    play_key(keyboard::WHITE_PITCH[index], index);
}

void UiController::enter_keyboard(int track) {
    keyboard_origin_  = view_;
    selected_         = track;
    audition_pending_ = false;
    pressed_key_      = -1;
    view_             = View::Keyboard;
}

// Joue la piste du clavier tout de suite, au pitch de la touche ; la touche s'inverse un instant.
void UiController::play_key(int pitch, int key_id) {
    Command c;
    c.type  = Command::Note;
    c.track = tracks_[selected_].engine_track;
    c.value = static_cast<float>(KEYBOARD_VELOCITY);
    c.pitch = static_cast<int8_t>(pitch);
    if (!sink_.push(c)) return;
    pressed_key_   = key_id;
    pressed_until_ = now_ms_ + LIT_MS;
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
        t.extra_count[s]   = snapshot_.extra_count[s];
        for (int e = 0; e < cfg::MAX_EXTRA_HITS; ++e) t.extra[s][e] = snapshot_.extra[s][e];
    }
    snapshot_.engine_track = t.engine_track;

    int extras = 0;
    for (int s = 0; s < cfg::MAX_STEPS; ++s) extras += snapshot_.extra_count[s];
    restore_total_ = 4 * cfg::MAX_STEPS + 2 + extras;
    restore_next_  = 0;
    restoring_     = true;
    pump_restore();
    mark_dirty();
}

// Commande numéro `index` de la remise en état : d'abord les notes supplémentaires effacées, puis chaque
// vélocité, pitch et décalage, le pattern, la quantification, enfin les notes supplémentaires de la copie.
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
    } else if (index == 4 * n) {
        c.type   = Command::SetTrackPattern;
        c.length = snapshot_.pattern;
    } else if (index == 4 * n + 1) {
        c.type  = Command::SetTrackQuantize;
        c.value = static_cast<float>(snapshot_.quantize);
    } else {
        int k = index - (4 * n + 2);
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
    quantize_edit_ = false;
    view_        = View::Menu;
}

void UiController::close_menu() {
    confirm_       = false;
    quantize_edit_ = false;
    view_    = menu_origin_;
}

int UiController::menu_item_count() const {
    return menu_kind_ == MenuKind::LoopItem || menu_kind_ == MenuKind::Keyboard ? 2 : menu_kind_ == MenuKind::Track ? 4 : 3;
}

void UiController::on_menu_ok() {
    switch (menu_kind_) {
        case MenuKind::Track:
            if (menu_cursor_ == 0) {
                if (library_ && library_->count() > 0) open_add(true);
            } else if (menu_cursor_ == 1) {
                quantize_edit_ = !quantize_edit_;  // Valider entre dans le réglage, puis le confirme
            } else if (menu_cursor_ == 2) {
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
        for (int st = 0; st < cfg::MAX_STEPS; ++st) {
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
        std::snprintf(text, sizeof text, " %d samples manquants", missing);
        notify(text);
    }
}

// Nombre de commandes de réglage d'une piste en plus des 4 de base : une par vélocité, pitch ou décalage
// qui s'écarte de la valeur par défaut, une par note supplémentaire, une pour la quantification si elle
// n'est pas nulle.
int UiController::count_extras(const profile::TrackData& td) {
    int n = td.quantize != 0;
    for (int s = 0; s < cfg::MAX_STEPS; ++s) {
        n += td.step_velocity[s] != cfg::VELOCITY_MAX;
        n += td.step_pitch[s] != 0;
        n += td.step_offset[s] != 0;
        n += td.extra_count[s];
    }
    return n;
}

// La `index`-ième commande de réglage d'une piste : les vélocités, puis les pitchs, les décalages, les notes
// supplémentaires (pas par pas) et enfin la quantification.
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
            return true;
        }
    }
    if (td.quantize != 0 && index == 0) {
        c.type  = Command::SetTrackQuantize;
        c.value = static_cast<float>(td.quantize);
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
            c.type   = Command::SetTrackSample;
            c.data   = p.data;
            c.length = p.length;
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
            td.step_offset[s]   = t.step_offset[s];
            td.extra_count[s]   = t.extra_count[s];
            for (int e = 0; e < cfg::MAX_EXTRA_HITS; ++e) td.extra[s][e] = t.extra[s][e];
        }
        td.quantize = t.quantize;
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
    c.type   = Command::PreviewSample;
    c.data   = data;
    c.length = length;
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
            const char letter = edit_mode_ == EditMode::Edit ? 'E' : edit_mode_ == EditMode::Velocity ? 'V' : edit_mode_ == EditMode::Pitch ? 'P' : 'T';
            render_header(c, tracks_[selected_].name, false, letter, 13);
            render_pattern(c);
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
        case View::Keyboard:
            render_header(c, tracks_[selected_].name, false, 0, record_phase_ != 0 ? 8 : 16);
            render_keyboard(c);
            break;
    }
    if (error_shown()) render_notice(c);
}

// En-tête : titre à gauche (16 caractères au plus), à droite l'état lecture / pause et le BPM ;
// dans la vue Pistes, le numéro de la loop et son indicateur de modification s'y ajoutent.
void UiController::render_header(Canvas& c, const char* title, bool show_loop, char mode_letter, int max_title) const {
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

    if (mode_letter) {
        const char letter[2] = {mode_letter, '\0'};
        c.text(bpm_x - 6 - Canvas::CHAR_W, 0, letter);
    }

    if (show_loop) {
        char loop[8];
        if (current_loop_ < 0) std::snprintf(loop, sizeof loop, "L*");
        else std::snprintf(loop, sizeof loop, "L%d%s", current_loop_ + 1, dirty_ ? "*" : "");
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
void UiController::render_pattern(Canvas& c) const {
    constexpr int Y = 24;  // haut des boîtes de pas

    const Track& t = tracks_[selected_];

    for (int i = 0; i < cfg::MAX_STEPS; ++i) {
        const int  x  = i * 8;
        const bool on = (t.pattern >> i) & 1u;


        if (edit_mode_ == EditMode::Edit || edit_mode_ == EditMode::Timing || !on) {
            c.rect(x, Y, 7, 7, on && edit_mode_ != EditMode::Velocity && edit_mode_ != EditMode::Pitch);  // plein = joué, contour = silence
        } else if (edit_mode_ == EditMode::Velocity) {
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
        if (i == step_cursor_) c.hline(x, Y - 2, 7);         // pas sélectionné
        if (i == play_step_) c.rect(x, Y + 9, 7, 2, true);   // pas en cours de lecture
    }

    // Bas de l'écran. Edit : l'état du dernier pas touché. Velocity et Pitch : une jauge horizontale pour
    // le pas sélectionné (s'il est joué), avec sa valeur exacte à droite.
    constexpr int GAUGE_Y = 6 * Canvas::CHAR_H;   // même rangée que le texte d'Edit
    constexpr int GAUGE_W = 100;                  // largeur de la jauge, contour compris
    const bool    selected_on = step_cursor_ >= 0 && ((t.pattern >> step_cursor_) & 1u);

    if (edit_mode_ != EditMode::Edit && selected_on) {
        char value[8];
        c.rect(0, GAUGE_Y, GAUGE_W, 7, false);  // contour de la jauge

        if (edit_mode_ == EditMode::Velocity) {
            // De la gauche vers la droite : 1 pixel pour la vélocité 1, toute la jauge pour 127.
            const int v    = t.step_velocity[step_cursor_];
            const int fill = 1 + (v - cfg::VELOCITY_MIN) * (GAUGE_W - 3) / (cfg::VELOCITY_MAX - cfg::VELOCITY_MIN);
            c.rect(1, GAUGE_Y + 1, fill, 5, true);
            std::snprintf(value, sizeof value, "%d", v);
        } else if (edit_mode_ == EditMode::Timing) {
            // Du centre : vers la droite pour une note en retard, vers la gauche pour une note en avance.
            // 3/4 de pixel par graduation : 48 pixels pour 64 (50 % d'un pas).
            const int o      = t.step_offset[step_cursor_];
            const int center = GAUGE_W / 2;
            const int cols   = ((o < 0 ? -o : o) * 3 + 2) / 4;
            if (o > 0) c.rect(center + 1, GAUGE_Y + 1, cols, 5, true);
            else if (o < 0) c.rect(center - cols, GAUGE_Y + 1, cols, 5, true);
            c.vline(center, GAUGE_Y - 1, 9);
            const int pct = static_cast<int>(std::lround(o * 100.0 / cfg::OFFSET_UNITS));
            if (pct == 0) std::snprintf(value, sizeof value, "0");
            else          std::snprintf(value, sizeof value, "%+d%%", pct);
        } else {
            // Du centre : vers la droite pour un pitch positif, vers la gauche pour un pitch négatif.
            // Un trait dépassant la jauge marque le 0.
            const int p      = t.step_pitch[step_cursor_];
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
    const char* labels[4];
    char        quantize_label[24];
    int         count = 3;
    switch (menu_kind_) {
        case MenuKind::Track:
            std::snprintf(quantize_label, sizeof quantize_label, quantize_edit_ ? " Quantifier <%d%%>" : " Quantifier %d%%",
                          tracks_[selected_].quantize);
            labels[0] = " Changer le sample";
            labels[1] = quantize_label;
            labels[2] = confirm_ ? " Confirmer ?" : " Supprimer la piste";
            labels[3] = " Annuler";
            count     = 4;
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
            std::snprintf(label, sizeof label, "%cL%d %dbpm %d piste%s", i == current_loop_ ? '>' : ' ', i + 1,
                          static_cast<int>(std::lround(loop.bpm)), loop.track_count, loop.track_count == 1 ? "" : "s");
        } else {
            std::snprintf(label, sizeof label, " + Nouvelle loop");
        }
        c.text(0, y, label, sel);
    }
}

// Vue Clavier : un clavier de piano dessiné à l'échelle des boutons (14 blanches de 8 pixels, les deux
// dernières cases vides ; 10 noires au-dessus, décalées entre deux blanches). La touche jouée s'inverse
// un instant ; le Do central (note de base) est marqué d'un point. La rangée du bas est réservée à
// l'enregistrement (rangée du temps).
void UiController::render_keyboard(Canvas& c) const {
    constexpr int TOP         = 16;  // haut du clavier
    constexpr int WHITE_H     = 36;  // hauteur des blanches
    constexpr int BLACK_H     = 20;  // hauteur des noires
    constexpr int BLACK_W     = 5;

    const bool lit        = static_cast<int32_t>(pressed_until_ - now_ms_) > 0;
    const int  pressed    = lit ? pressed_key_ : -1;

    // Blanches : contour, pleine quand elle est jouée.
    for (int i = 0; i < keyboard::WHITE_KEYS; ++i) {
        const bool down = (pressed == i);
        c.rect(i * 8, TOP, 7, WHITE_H, down);
    }
    // Note de base : un point au bas du Do central (inversé si la touche est pleine).
    {
        const int x = keyboard::ROOT_WHITE * 8 + 2;
        c.rect(x, TOP + WHITE_H - 6, 3, 3, true, pressed != keyboard::ROOT_WHITE);
    }
    // Noires : pleines ; creuses quand elles sont jouées.
    for (int j = 0; j < keyboard::BLACK_KEYS; ++j) {
        const int  x0   = keyboard::BLACK_LEFT_WHITE[j] * 8 + 5;
        const bool down = (pressed == keyboard::WHITE_KEYS + j);
        c.rect(x0, TOP, BLACK_W, BLACK_H, true, true);
        if (down) c.rect(x0 + 1, TOP + 1, BLACK_W - 2, BLACK_H - 2, true, false);
    }

    // Enregistrement : l'indicateur à côté du titre, puis la rangée du temps.
    if (record_phase_ == 1) {
        // Décompte des temps restants : 4 pendant le premier temps du pré-compte, puis 3, 2, 1.
        const int beats_left = precount_step_ < 0 ? 4 : 4 - precount_step_ / 4;
        char      text[8];
        std::snprintf(text, sizeof text, "PRE %d", beats_left);
        c.text(54, 0, text, true);
    } else if (record_phase_ == 2) {
        c.text(54, 0, "REC", true);
    }
    if (record_phase_ != 0) {
        // 16 cases : pleine = pas qui porte une note ; un trait dessous marque le pas en cours.
        const Track& t = tracks_[selected_];
        for (int i = 0; i < cfg::MAX_STEPS; ++i) {
            c.rect(i * 8, 54, 7, 5, (t.pattern >> i) & 1u);
            if (i == play_step_) c.rect(i * 8, 60, 7, 2, true);
        }
    }
}

// Message sur la dernière ligne, par-dessus ce qui s'y trouve.
void UiController::render_notice(Canvas& c) const {
    const int y = LIST_ROWS * Canvas::CHAR_H;
    c.rect(0, y, Canvas::W, Canvas::CHAR_H, true, false);
    c.text(0, y, notice_);
}
