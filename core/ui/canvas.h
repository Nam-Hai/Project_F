#pragma once

#include <cstdint>

// Écran 128x64 monochrome. Le buffer a la disposition mémoire du SSD1306 : 8 pages de 128 octets,
// chaque octet = 8 pixels verticaux (bit 0 = pixel du haut). Sur Teensy il part tel quel vers
// l'écran ; sur PC il est affiché dans l'émulateur. Aucune allocation, aucun include OS.
class Canvas {
public:
    static constexpr int W      = 128;
    static constexpr int H      = 64;
    static constexpr int BYTES  = W * H / 8;
    static constexpr int CHAR_W = 6;  // 5 pixels de glyphe + 1 d'espacement
    static constexpr int CHAR_H = 8;  // 7 pixels de glyphe + 1 d'espacement

    Canvas() { clear(); }

    void clear(bool on = false);

    // Les coordonnées hors écran sont ignorées (clipping).
    void set_pixel(int x, int y, bool on = true);
    bool get_pixel(int x, int y) const;

    void hline(int x, int y, int w, bool on = true);
    void vline(int x, int y, int h, bool on = true);
    void rect(int x, int y, int w, int h, bool fill = false, bool on = true);
    void invert_rect(int x, int y, int w, int h);

    // Texte opaque en cases de 6x8 pixels ; `inverted` = fond allumé, lettres éteintes
    // (ligne sélectionnée). Retourne l'abscisse suivant le dernier caractère.
    int text(int x, int y, const char* s, bool inverted = false);

    // Largeur en pixels d'un texte, pour l'alignement à droite : text(W - text_width(s), ...).
    static int text_width(const char* s);

    const uint8_t* data() const { return buf_; }

private:
    void draw_char(int x, int y, char c, bool inverted);

    uint8_t buf_[BYTES];
};
