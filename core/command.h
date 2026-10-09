#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// Commande envoyée par une source (pattern initial, UI simulée, UI matérielle) au moteur.
struct Command {
    enum Type : uint8_t {
        SetBpm,          // value = BPM
        SetStep,         // track, step, value > 0.5 = trig actif
        ToggleStep,      // track, step
        SetTrackGain,    // track, value = gain
        SetTrackPitch,   // track, value = ratio (1.0 = normal)
        SetTrackSample,  // track, data, length : branche un sample sur la piste (la crée si besoin)
        SetTrackPattern, // track, length = les 16 pas d'un coup (bit i = pas i)
        SetStepVelocity, // track, step, value = vélocité 1..127 (bornée)
        SetStepPitch,    // track, step, value = décalage en demi-tons -12..12 (borné)
        AuditionStep,    // track, step : joue la note du pas avec ses valeurs, sans toucher au séquenceur
        ClearTrack,      // track : supprime la piste (voix, sample, pattern, gain)
        ClearAll,        // supprime toutes les pistes (nouvelle loop, chargement d'une loop)
        PreviewSample,   // data, length : joue un sample hors piste (écoute avant ajout)
        StopPreview,     // coupe l'écoute et rend son buffer
        Play,            // repart du pas 0
        Stop,
        Trigger,         // track : déclenchement immédiat (boutons)
    };

    Type           type  = Stop;
    uint8_t        track = 0;
    uint8_t        step  = 0;
    float          value = 0.0f;
    const int16_t* data   = nullptr;  // SetTrackSample : mono 16 bits 44,1 kHz, doit rester valide
    uint32_t       length = 0;        // jusqu'à ce que le moteur le rende (Engine::pop_released)
};

// Destination des commandes : le moteur en vrai, un enregistreur dans les tests.
class CommandSink {
public:
    virtual ~CommandSink() = default;

    // false si la commande n'a pas pu être acceptée (file pleine) : l'appelant peut réessayer.
    virtual bool push(const Command& cmd) = 0;
};

// File lock-free pour un producteur et un consommateur (loop() -> callback audio).
// N doit être une puissance de 2. Ne bloque jamais, pas d'allocation.
template <typename T, std::size_t N>
class SpscQueue {
    static_assert((N & (N - 1)) == 0 && N > 0, "N doit être une puissance de 2");

public:
    bool push(const T& v) {  // côté producteur
        const uint32_t h = head_.load(std::memory_order_relaxed);
        const uint32_t t = tail_.load(std::memory_order_acquire);
        if (h - t >= N) return false;  // pleine
        buf_[h % N] = v;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& v) {  // côté consommateur
        const uint32_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false;  // vide
        v = buf_[t % N];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

private:
    T                     buf_[N];
    std::atomic<uint32_t> head_{0};
    std::atomic<uint32_t> tail_{0};
};
