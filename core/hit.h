#pragma once

#include <cstdint>

#include "../config.h"

// Une note d'un pas : décalage par rapport au pas (1/128 de pas, -64..63), vélocité (1..127), pitch (demi-tons,
// -12..12), pan (-50..50). Un pas porte une note principale et jusqu'à cfg::MAX_EXTRA_HITS notes supplémentaires (roulements).
struct Hit {
    int8_t  offset   = 0;
    uint8_t velocity = cfg::VELOCITY_MAX;
    int8_t  pitch    = 0;
    int8_t  pan      = 0;
};
