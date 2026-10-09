#pragma once

#include <atomic>

// État du moteur, écrit par le thread audio et lu par l'UI (affichage, LEDs).
struct EngineState {
    std::atomic<int>   step{-1};  // dernier pas déclenché, -1 tant que rien n'a joué
    std::atomic<float> bpm{120.0f};
    std::atomic<bool>  playing{false};
};
