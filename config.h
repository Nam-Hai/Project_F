#pragma once

// Constantes globales, partagées par le core et les plateformes.
// BLOCK = AUDIO_BLOCK_SAMPLES de la Teensy Audio Library (cf. plan_poc_desktop.md).
namespace cfg {

constexpr int   SAMPLE_RATE = 44100;
constexpr int   BLOCK       = 128;
constexpr int   MAX_TRACKS  = 16;
constexpr int   MAX_STEPS   = 16;

constexpr float MASTER_GAIN = 0.8f;

// Valeurs par pas : vélocité (volume de la note) et pitch (demi-tons).
constexpr int   VELOCITY_MIN     = 1;
constexpr int   VELOCITY_MAX     = 127;  // valeur par défaut : le volume d'avant les vélocités
constexpr int   STEP_PITCH_MIN   = -12;
constexpr int   STEP_PITCH_MAX   = 12;

// Micro-timing : décalage d'une note par rapport à son pas, en 1/128 de pas (-64..63 = -50 % à +50 %),
// jusqu'à 4 notes par pas (la note principale et 3 supplémentaires), force de quantification 0..100 %.
constexpr int   OFFSET_MIN       = -64;
constexpr int   OFFSET_MAX       = 63;
constexpr int   OFFSET_UNITS     = 128;
constexpr int   MAX_EXTRA_HITS   = 3;
constexpr int   QUANTIZE_MAX     = 100;

}  // namespace cfg
