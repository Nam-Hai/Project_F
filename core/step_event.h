#pragma once

#include <cstdint>

#include "../config.h"
#include "hit.h"

// État complet d'un pas que l'enregistrement vient de modifier (moteur -> UI). La note principale est
// `main`, suivie de `extra_count` notes supplémentaires : l'UI écrase son modèle avec ces valeurs, sans
// refaire la logique de la prise. Le pas est joué.
struct StepEvent {
    uint8_t track       = 0;
    uint8_t step        = 0;
    uint8_t extra_count = 0;
    Hit     main;
    Hit     extra[cfg::MAX_EXTRA_HITS];
};

// Source des changements de pas faits par l'enregistrement (le moteur ; un faux dans les tests).
class StepEventSource {
public:
    virtual ~StepEventSource() = default;
    virtual bool pop_step_event(StepEvent& ev) = 0;
};
