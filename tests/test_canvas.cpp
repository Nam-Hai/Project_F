#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "../core/ui/canvas.h"
#include "../core/ui/font5x7.h"

static int failures = 0;

#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            std::printf("FAIL: %s\n", msg);         \
            ++failures;                             \
        }                                           \
    } while (0)

// Région du canvas en ASCII ('#' = allumé), une ligne par rangée de pixels.
static std::string ascii(const Canvas& c, int x, int y, int w, int h) {
    std::string s;
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) s += c.get_pixel(x + i, y + j) ? '#' : '.';
        s += '\n';
    }
    return s;
}

static int count_on(const Canvas& c) {
    int n = 0;
    for (int y = 0; y < Canvas::H; ++y)
        for (int x = 0; x < Canvas::W; ++x) n += c.get_pixel(x, y);
    return n;
}

int main() {
    // Disposition mémoire SSD1306 : octet = (y / 8) * 128 + x, bit = y % 8.
    {
        Canvas c;
        c.set_pixel(5, 10);
        CHECK(c.data()[1 * 128 + 5] == 0x04, "pixel (5,10) -> octet 133, bit 2");
        c.set_pixel(0, 0);
        CHECK(c.data()[0] == 0x01, "pixel (0,0) -> bit 0 de l'octet 0");
        c.set_pixel(127, 63);
        CHECK(c.data()[Canvas::BYTES - 1] == 0x80, "pixel (127,63) -> bit 7 du dernier octet");
        c.set_pixel(5, 10, false);
        CHECK(!c.get_pixel(5, 10), "extinction d'un pixel");
        CHECK(sizeof(Canvas) >= Canvas::BYTES && Canvas::BYTES == 1024, "buffer de 1024 octets");
    }

    // Clipping : rien ne déborde, rien ne plante.
    {
        Canvas c;
        c.set_pixel(-1, 0);
        c.set_pixel(0, -1);
        c.set_pixel(128, 0);
        c.set_pixel(0, 64);
        c.rect(-10, -10, 200, 200, true);
        CHECK(count_on(c) == Canvas::W * Canvas::H, "rectangle plein plus grand que l'écran : tout allumé");
        c.clear();
        CHECK(count_on(c) == 0, "clear");
        c.clear(true);
        CHECK(count_on(c) == Canvas::W * Canvas::H, "clear(true)");
    }

    // Primitives.
    {
        Canvas c;
        c.hline(10, 5, 20);
        c.vline(40, 0, 64);
        CHECK(count_on(c) == 20 + 64, "hline + vline");

        c.clear();
        c.rect(2, 2, 10, 6);
        CHECK(count_on(c) == 2 * 10 + 2 * (6 - 2), "rectangle vide : le contour seulement");
        c.clear();
        c.rect(2, 2, 10, 6, true);
        CHECK(count_on(c) == 60, "rectangle plein");

        c.invert_rect(2, 2, 10, 6);
        CHECK(count_on(c) == 0, "invert_rect sur une zone pleine");
        c.invert_rect(0, 0, 4, 4);
        CHECK(count_on(c) == 16, "invert_rect sur une zone vide");
    }

    // Référence : la lettre 'A' (colonnes 7E 11 11 11 7E) et l'espacement de la case 6x8.
    {
        Canvas c;
        const int next = c.text(0, 0, "A");
        const std::string expected =
            ".###..\n"
            "#...#.\n"
            "#...#.\n"
            "#...#.\n"
            "#####.\n"
            "#...#.\n"
            "#...#.\n"
            "......\n";
        CHECK(ascii(c, 0, 0, 6, 8) == expected, "glyphe 'A'");
        CHECK(next == 6, "text retourne l'abscisse suivante");

        // Inversé : exactement le négatif, sur toute la case 6x8.
        Canvas inv;
        inv.text(0, 0, "A", true);
        bool negative = true;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 6; ++x) negative = negative && (inv.get_pixel(x, y) != c.get_pixel(x, y));
        CHECK(negative, "texte inversé = négatif de la case");
    }

    // Texte : avance, largeur, position verticale, effacement du fond (opaque).
    {
        Canvas c;
        c.clear(true);
        const int next = c.text(10, 20, "Hi");
        CHECK(next == 22 && Canvas::text_width("Hi") == 12, "avance et largeur de 'Hi'");
        CHECK(!c.get_pixel(15, 27) && !c.get_pixel(10, 20 + 7), "fond de la case effacé (texte opaque)");
        CHECK(c.get_pixel(9, 20) && c.get_pixel(22, 20), "autour de la case : inchangé");

        c.clear();
        c.text(Canvas::W - Canvas::text_width("120"), 0, "120");
        CHECK(c.get_pixel(Canvas::W - 1 - 1, 0) || c.get_pixel(Canvas::W - 1 - 1, 1), "texte aligné à droite");

        // Débordement à droite : clipping, sans plantage.
        c.clear();
        c.text(125, 0, "WWWW");
        CHECK(count_on(c) > 0, "texte partiellement hors écran : la partie visible est dessinée");
        CHECK(Canvas::text_width("") == 0, "largeur du texte vide");
    }

    // Couverture de la police : chaque caractère imprimable (sauf espace) a au moins un pixel,
    // les caractères hors table s'affichent comme '?'.
    {
        bool all_drawn = true;
        for (int ch = 33; ch <= 126; ++ch) {
            Canvas c;
            const char s[2] = {static_cast<char>(ch), 0};
            c.text(0, 0, s);
            all_drawn = all_drawn && count_on(c) > 0;
        }
        CHECK(all_drawn, "tous les caractères imprimables ont un glyphe");

        Canvas a, b;
        a.text(0, 0, "\x01");
        b.text(0, 0, "?");
        CHECK(std::memcmp(a.data(), b.data(), Canvas::BYTES) == 0, "caractère inconnu -> '?'");

        Canvas sp;
        sp.text(0, 0, " ");
        CHECK(count_on(sp) == 0, "espace vide");
    }

    // Segments : Bresenham, extrémités comprises, dans les deux sens, hors écran sans plantage.
    {
        Canvas c;
        c.line(2, 3, 9, 3);
        CHECK(ascii(c, 0, 2, 12, 3) == "............\n..########..\n............\n", "ligne horizontale, extrémités comprises");
        Canvas v;
        v.line(4, 1, 4, 5);
        CHECK(count_on(v) == 5 && v.get_pixel(4, 1) && v.get_pixel(4, 5) && !v.get_pixel(4, 6), "ligne verticale");
        Canvas p;
        p.line(7, 7, 7, 7);
        CHECK(count_on(p) == 1 && p.get_pixel(7, 7), "un seul point");
        Canvas d;
        d.line(0, 0, 4, 4);
        bool diag = count_on(d) == 5;
        for (int i = 0; i < 5; ++i) diag = diag && d.get_pixel(i, i);
        CHECK(diag, "diagonale à 45 degrés");
        Canvas f1, f2;
        f1.line(3, 10, 40, 22);
        f2.line(40, 22, 3, 10);
        CHECK(std::memcmp(f1.data(), f2.data(), Canvas::BYTES) == 0, "mêmes pixels dans les deux sens");
        CHECK(f1.get_pixel(3, 10) && f1.get_pixel(40, 22), "extrémités allumées");
        // Pente douce : un pixel par colonne ; pente raide : un pixel par rangée.
        Canvas g;
        g.line(0, 0, 10, 3);
        bool one_per_col = true;
        for (int x = 0; x <= 10; ++x) {
            int n = 0;
            for (int y = 0; y < 5; ++y) n += g.get_pixel(x, y);
            one_per_col = one_per_col && n == 1;
        }
        CHECK(one_per_col, "pente douce : un pixel par colonne");
        Canvas st;
        st.line(0, 0, 3, 10);
        bool one_per_row = true;
        for (int y = 0; y <= 10; ++y) {
            int n = 0;
            for (int x = 0; x < 5; ++x) n += st.get_pixel(x, y);
            one_per_row = one_per_row && n == 1;
        }
        CHECK(one_per_row, "pente raide : un pixel par rangée");
        Canvas e;
        e.line(5, 20, 8, 20, false);
        e.line(-10, -10, 200, 100);
        e.line(60, 30, 60, 30, false);
        CHECK(count_on(e) > 0, "hors écran : dessiné où ça tient, sans plantage");
        Canvas off;
        off.rect(0, 0, 128, 64, true);
        off.line(10, 10, 20, 10, false);
        CHECK(!off.get_pixel(15, 10) && off.get_pixel(15, 11) && off.get_pixel(9, 10), "ligne en négatif (éteint)");
    }

    // Écran complet de référence (pour la lecture à l'oeil : `test_canvas --show`).
    // Vérifie ici seulement que les 8 lignes de 21 caractères tiennent à l'écran.
    {
        Canvas c;
        for (int line = 0; line < 8; ++line) c.text(0, line * Canvas::CHAR_H, "ABCDEFGHIJKLMNOPQRSTU");
        CHECK(Canvas::text_width("ABCDEFGHIJKLMNOPQRSTU") == 126 && 126 <= Canvas::W, "21 colonnes tiennent en 128 px");
        CHECK(count_on(c) > 0 && !c.get_pixel(127, 0), "8 lignes de 21 caractères");
    }

    if (failures == 0) std::printf("OK\n");
    return failures ? 1 : 0;
}
