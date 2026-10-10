#pragma once

#include <atomic>
#include <cstdint>

#include "../config.h"

// État du moteur, écrit par le thread audio et lu par l'UI (affichage, LEDs).
struct EngineState {
    std::atomic<int>   step{-1};  // dernier pas déclenché, -1 tant que rien n'a joué
    std::atomic<float> bpm{120.0f};
    std::atomic<bool>  playing{false};

    // Enregistrement : phase (0 repos, 1 pré-compte, 2 prise), pas du pré-compte (0..15, -1 hors pré-compte)
    // et nombre de changements de pas que la file vers l'UI n'a pas pu porter (modèle de l'UI alors en retard).
    std::atomic<int>      record_phase{0};
    std::atomic<int>      precount_step{-1};
    std::atomic<uint32_t> record_lost{0};

    // Nombre de trigs par piste depuis le démarrage : l'UI compare avec la valeur précédente
    // pour faire clignoter l'indicateur d'une piste.
    std::atomic<uint32_t> triggers[cfg::MAX_TRACKS] = {};
};
