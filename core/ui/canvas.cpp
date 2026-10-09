#include "canvas.h"

#include "font5x7.h"

void Canvas::clear(bool on) {
    const uint8_t v = on ? 0xFF : 0x00;
    for (uint8_t& b : buf_) b = v;
}

void Canvas::set_pixel(int x, int y, bool on) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    uint8_t&      b    = buf_[(y >> 3) * W + x];
    const uint8_t mask = static_cast<uint8_t>(1u << (y & 7));
    b = on ? static_cast<uint8_t>(b | mask) : static_cast<uint8_t>(b & ~mask);
}

bool Canvas::get_pixel(int x, int y) const {
    if (x < 0 || x >= W || y < 0 || y >= H) return false;
    return (buf_[(y >> 3) * W + x] >> (y & 7)) & 1;
}

void Canvas::hline(int x, int y, int w, bool on) {
    for (int i = 0; i < w; ++i) set_pixel(x + i, y, on);
}

void Canvas::vline(int x, int y, int h, bool on) {
    for (int i = 0; i < h; ++i) set_pixel(x, y + i, on);
}

void Canvas::rect(int x, int y, int w, int h, bool fill, bool on) {
    if (w <= 0 || h <= 0) return;
    if (fill) {
        for (int j = 0; j < h; ++j) hline(x, y + j, w, on);
    } else {
        hline(x, y, w, on);
        hline(x, y + h - 1, w, on);
        vline(x, y, h, on);
        vline(x + w - 1, y, h, on);
    }
}

void Canvas::invert_rect(int x, int y, int w, int h) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) set_pixel(x + i, y + j, !get_pixel(x + i, y + j));
}

void Canvas::draw_char(int x, int y, char c, bool inverted) {
    if (c < font5x7::FIRST || c > font5x7::LAST) c = '?';
    const uint8_t* glyph = font5x7::GLYPHS[c - font5x7::FIRST];

    for (int col = 0; col < CHAR_W; ++col) {
        const uint8_t bits = (col < font5x7::WIDTH) ? glyph[col] : 0;  // colonne 5 = espacement
        for (int row = 0; row < CHAR_H; ++row) {
            const bool lit = (row < 7) && ((bits >> row) & 1);
            set_pixel(x + col, y + row, lit != inverted);
        }
    }
}

int Canvas::text(int x, int y, const char* s, bool inverted) {
    for (; *s; ++s) {
        draw_char(x, y, *s, inverted);
        x += CHAR_W;
    }
    return x;
}

int Canvas::text_width(const char* s) {
    int n = 0;
    while (s[n]) ++n;
    return n * CHAR_W;
}
