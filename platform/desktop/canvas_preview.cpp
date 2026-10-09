// Aperçu du Canvas (l'écran OLED 128x64) dans le terminal.
//
// Usage :
//   canvas_preview                      écran d'exemple (vue Pistes)
//   canvas_preview "ligne 1" "ligne 2"  une ligne de texte par argument (8 lignes max)
//   canvas_preview "Kick1" "!Snare1"    un « ! » en début de ligne l'affiche en inversé (sélection)
//   canvas_preview "Titre|120 >"        un « | » aligne la fin de la ligne à droite
//
// Les pixels sont dessinés avec des demi-blocs : deux rangées de pixels par ligne de terminal.
#include <cstdio>
#include <cstring>
#include <string>

#include "../../core/ui/canvas.h"

static void draw_line(Canvas& c, int row, const char* line) {
    bool inverted = false;
    if (line[0] == '!') {
        inverted = true;
        ++line;
    }

    const int y = row * Canvas::CHAR_H;
    if (inverted) c.rect(0, y, Canvas::W, Canvas::CHAR_H, true);  // la barre de sélection pleine largeur

    const char* bar = std::strchr(line, '|');
    if (!bar) {
        c.text(0, y, line, inverted);
        return;
    }
    const std::string left(line, bar - line);
    const char*       right = bar + 1;
    c.text(0, y, left.c_str(), inverted);
    c.text(Canvas::W - Canvas::text_width(right), y, right, inverted);
}

static void print(const Canvas& c) {
    std::printf("+");
    for (int x = 0; x < Canvas::W; ++x) std::printf("-");
    std::printf("+\n");

    for (int y = 0; y < Canvas::H; y += 2) {
        std::printf("|");
        for (int x = 0; x < Canvas::W; ++x) {
            const bool top = c.get_pixel(x, y);
            const bool bot = c.get_pixel(x, y + 1);
            std::printf("%s", top ? (bot ? "█" : "▀") : (bot ? "▄" : " "));
        }
        std::printf("|\n");
    }

    std::printf("+");
    for (int x = 0; x < Canvas::W; ++x) std::printf("-");
    std::printf("+\n");
}

int main(int argc, char** argv) {
    Canvas c;

    if (argc < 2) {
        draw_line(c, 0, "Pistes|120 >");
        draw_line(c, 1, "!> Kick1");
        draw_line(c, 2, "  Snare1");
        draw_line(c, 3, "  HHcl");
        draw_line(c, 4, "  + Ajouter piste");
    } else {
        for (int i = 1; i < argc && i <= 8; ++i) draw_line(c, i - 1, argv[i]);
    }

    print(c);
    return 0;
}
