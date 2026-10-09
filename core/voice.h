#pragma once

// Interface commune des voies percussives (synthés et lecteur de samples).
class Voice {
public:
    virtual ~Voice() = default;

    virtual void trigger(float velocity) = 0;

    // Déclenche avec un rapport de pitch propre à cette note (1.0 = inchangé), qui se multiplie
    // avec le pitch de la voix. Par défaut le pitch de la note est ignoré (voix sans pitch).
    virtual void trigger_pitched(float velocity, float pitch_ratio) {
        (void)pitch_ratio;
        trigger(velocity);
    }

    // Ajoute `n` échantillons mono dans `out` (accumulation, pas d'écrasement).
    virtual void render(float* out, int n) = 0;
};
