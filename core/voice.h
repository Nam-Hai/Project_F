#pragma once

// Interface commune des voies percussives (synthés et lecteur de samples).
class Voice {
public:
    virtual ~Voice() = default;

    virtual void trigger(float velocity) = 0;

    // Ajoute `n` échantillons mono dans `out` (accumulation, pas d'écrasement).
    virtual void render(float* out, int n) = 0;
};
