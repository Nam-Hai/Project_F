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

// Panoramique d'une note : -50 (tout à gauche) à +50 (tout à droite), 0 = centre.
constexpr int   PAN_MIN          = -50;
constexpr int   PAN_MAX          = 50;

// Enveloppe ADSR d'une piste : temps en millisecondes (0 = immédiat), sustain en %. Par défaut, un Release court
// (5 ms) fait finir la note remplacée en douceur au lieu de la couper net (plus de clic).
constexpr int   ENV_MS_MAX               = 2000;
constexpr int   ENV_DEFAULT_RELEASE_MS   = 5;

// Largeur stéréo d'une piste : -100 (mono complet) à +100 (côtés doublés), 0 = sample inchangé.
constexpr int   WIDTH_MIN        = -100;
constexpr int   WIDTH_MAX        = 100;

}  // namespace cfg
