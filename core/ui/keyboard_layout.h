#pragma once

// Clavier de piano : 16 boutons de pas = touches blanches, 10 boutons en plus = touches noires.
// Deux octaves : les 14 premières blanches et les 10 noires font les 24 demi-tons ; les blanches 15 et
// 16 (index 14 et 15) sont inactives dans la vue Clavier. Le Do de la 8e blanche (index 7) est la note
// de base (pitch 0). Tout tient dans la plage de pitch ±12.
//
//   noires :   ▪  ▪     ▪  ▪  ▪     ▪  ▪     ▪  ▪  ▪
//   blanches : C  D  E  F  G  A  B  C  D  E  F  G  A  B  .  .
//   index      0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
namespace keyboard {

constexpr int WHITE_BUTTONS = 16;  // boutons de pas
constexpr int WHITE_KEYS    = 14;  // touches blanches jouables
constexpr int BLACK_KEYS    = 10;
constexpr int ROOT_WHITE    = 7;   // le Do central : pitch 0

// Pitch en demi-tons de la touche blanche `i` (0..13) et de la touche noire `i` (0..9).
constexpr int WHITE_PITCH[WHITE_KEYS] = {-12, -10, -8, -7, -5, -3, -1, 0, 2, 4, 5, 7, 9, 11};
constexpr int BLACK_PITCH[BLACK_KEYS] = {-11, -9, -6, -4, -2, 1, 3, 6, 8, 10};

// La touche noire `i` est placée entre la blanche `BLACK_LEFT_WHITE[i]` et la suivante.
constexpr int BLACK_LEFT_WHITE[BLACK_KEYS] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12};

}  // namespace keyboard
