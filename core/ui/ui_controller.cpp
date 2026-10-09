#include "ui_controller.h"

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

// Garde le curseur visible dans une liste de LIST_ROWS lignes.
void reveal(int cursor, int& scroll) {
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + UiController::LIST_ROWS) scroll = cursor - UiController::LIST_ROWS + 1;
}

void move_in_list(int& cursor, int& scroll, int items, int delta) {
    cursor += delta;
    if (cursor < 0) cursor = 0;
    if (cursor > items - 1) cursor = items - 1;
    reveal(cursor, scroll);
}

}  // namespace

int UiController::add_track(const char* name, uint8_t engine_track, uint16_t pattern_bits) {
    if (track_count_ >= cfg::MAX_TRACKS) return -1;

    Track& t = tracks_[track_count_];
    t        = Track{};
    display_name(t.name, name);
    t.engine_track = engine_track;
    t.pattern      = pattern_bits;
    return track_count_++;
}

void UiController::handle(const InputEvent& e) {
    error_until_ = now_ms_;  // toute action efface le message d'erreur

    switch (e.type) {
        case InputEvent::Wheel:  on_wheel(e.delta); break;
        case InputEvent::Ok:     on_ok(); break;
        case InputEvent::Back:   on_back(); break;
        case InputEvent::Menu:   on_menu(); break;
        case InputEvent::Play:   on_play(); break;
        case InputEvent::Step:   on_step(e.index); break;
        case InputEvent::PotBpm: on_pot_bpm(e.value); break;
    }
}

void UiController::on_wheel(int delta) {
    switch (view_) {
        case View::Tracks:
            move_in_list(cursor_, scroll_, track_count_ + 1, delta);  // les pistes, puis « Ajouter piste »
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
            move_in_list(menu_cursor_, unused, MENU_ITEMS, delta);
            confirm_delete_ = false;
            break;
        }
        case View::Pattern:
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
    selected_       = track;
    step_cursor_    = -1;
    confirm_delete_ = false;
    view_           = View::Pattern;
}

// Menu de la piste : celle sous le curseur dans la liste, ou celle du pattern ouvert.
void UiController::open_menu() {
    if (view_ == View::Tracks) {
        if (cursor_ >= track_count_) return;  // « Ajouter piste » n'a pas de menu
        selected_ = cursor_;
    }
    menu_origin_    = view_;
    menu_cursor_    = 0;
    confirm_delete_ = false;
    view_           = View::Menu;
}

void UiController::close_menu() {
    confirm_delete_ = false;
    view_           = menu_origin_;
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
    // Remplacement : retour là d'où on a ouvert le menu. Ajout : retour à la liste (curseur sur « Ajouter »).
    view_ = add_replace_ ? menu_origin_ : View::Tracks;
}

// Charge le sample choisi (hors audio) et le donne à la piste moteur. false = rien n'a changé.
bool UiController::send_sample(int engine_track) {
    stop_preview();

    const int16_t* data   = nullptr;
    uint32_t       length = 0;
    if (!library_->load(add_cursor_, data, length)) {
        fail();
        return false;
    }

    Command c;
    c.type   = Command::SetTrackSample;
    c.track  = static_cast<uint8_t>(engine_track);
    c.data   = data;
    c.length = length;
    if (!sink_.push(c)) {  // file pleine : on ne garde rien, le buffer est rendu
        library_->discard(data);
        fail();
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
    open_pattern(index);
}

// Remplace le sample de la piste sélectionnée ; son pattern est conservé.
void UiController::confirm_replace() {
    Track& t = tracks_[selected_];
    if (!send_sample(t.engine_track)) return;

    display_name(t.name, library_->name(add_cursor_));
    view_ = menu_origin_;
}

void UiController::delete_selected() {
    Command c;
    c.type  = Command::ClearTrack;
    c.track = tracks_[selected_].engine_track;
    if (!sink_.push(c)) {
        confirm_delete_ = false;
        fail();
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

    confirm_delete_ = false;
    view_           = View::Tracks;
}

void UiController::on_ok() {
    switch (view_) {
        case View::Tracks:
            if (cursor_ < track_count_) open_pattern(cursor_);
            else if (can_add()) open_add(false);
            break;
        case View::Pattern:
            break;
        case View::Add:
            if (add_replace_) confirm_replace();
            else confirm_add();
            break;
        case View::Menu:
            if (menu_cursor_ == 0) {
                if (library_ && library_->count() > 0) open_add(true);
            } else if (menu_cursor_ == 1) {
                if (confirm_delete_) delete_selected();
                else confirm_delete_ = true;  // première validation : on demande confirmation
            } else {
                close_menu();  // Annuler
            }
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
        case View::Tracks:
            break;
    }
}

void UiController::on_menu() {
    switch (view_) {
        case View::Tracks:
        case View::Pattern:
            open_menu();
            break;
        case View::Menu:
            close_menu();  // Menu rappuyé : on referme
            break;
        case View::Add:
            break;  // pas de menu dans cette vue pour l'instant
    }
}

void UiController::on_play() {
    Command c;
    c.type = playing_ ? Command::Stop : Command::Play;
    if (sink_.push(c)) playing_ = !playing_;  // confirmé ensuite par update()
}

void UiController::on_step(int index) {
    if (view_ != View::Pattern || index < 0 || index >= cfg::MAX_STEPS) return;

    Track&     t  = tracks_[selected_];
    const bool on = !((t.pattern >> index) & 1u);

    Command c;
    c.type  = Command::SetStep;
    c.track = t.engine_track;
    c.step  = static_cast<uint8_t>(index);
    c.value = on ? 1.0f : 0.0f;
    if (!sink_.push(c)) return;  // file pleine : le modèle reste cohérent avec le moteur

    t.pattern = static_cast<uint16_t>(on ? (t.pattern | (1u << index)) : (t.pattern & ~(1u << index)));
    step_cursor_ = index;
}

void UiController::on_pot_bpm(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    const int bpm = BPM_MIN + static_cast<int>(std::lround(v * (BPM_MAX - BPM_MIN)));
    if (bpm == bpm_) return;  // le potard bouge à peine : pas de commande inutile

    Command c;
    c.type  = Command::SetBpm;
    c.value = static_cast<float>(bpm);
    if (sink_.push(c)) bpm_ = bpm;
}

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

void UiController::update(const EngineState& s, uint32_t now_ms) {
    now_ms_    = now_ms;
    playing_   = s.playing.load();
    bpm_       = static_cast<int>(std::lround(s.bpm.load()));
    play_step_ = playing_ ? s.step.load() : -1;

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
            render_header(c, "Pistes");
            render_tracks(c);
            break;
        case View::Pattern:
            render_header(c, tracks_[selected_].name);
            render_pattern(c);
            break;
        case View::Add:
            render_header(c, add_replace_ ? "Changer sample" : "Ajouter piste");
            render_add(c);
            break;
        case View::Menu:
            render_header(c, tracks_[selected_].name);
            render_menu(c);
            break;
    }
}

// En-tête : titre à gauche, BPM et état lecture / pause en haut à droite.
void UiController::render_header(Canvas& c, const char* title) const {
    char shown[17];
    std::snprintf(shown, sizeof shown, "%s", title);  // tronqué pour ne pas toucher l'indicateur
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
    c.text(icon_x - 4 - Canvas::text_width(bpm), 0, bpm);
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
void UiController::render_pattern(Canvas& c) const {
    constexpr int Y = 24;  // haut des boîtes de pas

    const Track& t = tracks_[selected_];

    for (int i = 0; i < cfg::MAX_STEPS; ++i) {
        const int  x  = i * 8;
        const bool on = (t.pattern >> i) & 1u;

        c.rect(x, Y, 7, 7, on);                              // plein = joué, contour = silence
        if (i % 4 == 0) c.vline(x + 3, Y - 6, 3);            // repère de temps tous les 4 pas
        if (i == step_cursor_) c.hline(x, Y - 2, 7);         // dernier pas touché
        if (i == play_step_) c.rect(x, Y + 9, 7, 2, true);   // pas en cours de lecture
    }

    if (step_cursor_ >= 0) {
        char info[24];
        const bool on = (t.pattern >> step_cursor_) & 1u;
        std::snprintf(info, sizeof info, "Pas %d : %s", step_cursor_ + 1, on ? "joue" : "silence");
        c.text(0, 6 * Canvas::CHAR_H, info);
    }
}

// Vue Ajout : les samples disponibles, la roue parcourt la liste, Valider choisit.
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

    if (error_shown()) render_error(c, " Echec du chargement");
}

// Menu de la piste : Changer le sample / Supprimer la piste (avec confirmation) / Annuler.
void UiController::render_menu(Canvas& c) const {
    const char* labels[MENU_ITEMS] = {
        " Changer le sample",
        confirm_delete_ ? " Confirmer ?" : " Supprimer la piste",
        " Annuler",
    };

    for (int i = 0; i < MENU_ITEMS; ++i) {
        const int  y   = (i + 1) * Canvas::CHAR_H;
        const bool sel = (i == menu_cursor_);
        if (sel) c.rect(0, y, Canvas::W, Canvas::CHAR_H, true);
        c.text(0, y, labels[i], sel);
    }

    if (error_shown()) render_error(c, " Echec de l'action");
}

// Message d'erreur sur la dernière ligne, par-dessus ce qui s'y trouve.
void UiController::render_error(Canvas& c, const char* text) const {
    const int y = LIST_ROWS * Canvas::CHAR_H;
    c.rect(0, y, Canvas::W, Canvas::CHAR_H, true, false);
    c.text(0, y, text);
}
