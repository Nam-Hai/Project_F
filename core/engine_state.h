#pragma once

#include <atomic>
#include <cstdint>

#include "../config.h"

// État du moteur, écrit par le thread audio et lu par l'UI (affichage, LEDs).
struct EngineState {
    std::atomic<int>   step{-1};  // dernier pas déclenché, -1 tant que rien n'a joué
    std::atomic<float> bpm{120.0f};
    std::atomic<bool>  playing{false};

    // Nombre de trigs par piste depuis le démarrage : l'UI compare avec la valeur précédente
    // pour faire clignoter l'indicateur d'une piste.
    std::atomic<uint32_t> triggers[cfg::MAX_TRACKS] = {};
};
