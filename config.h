#pragma once

// Constantes globales, partagées par le core et les plateformes.
// BLOCK = AUDIO_BLOCK_SAMPLES de la Teensy Audio Library (cf. plan_poc_desktop.md).
namespace cfg {

constexpr int   SAMPLE_RATE = 44100;
constexpr int   BLOCK       = 128;
constexpr int   MAX_TRACKS  = 16;
constexpr int   MAX_STEPS   = 16;

constexpr float MASTER_GAIN = 0.8f;

}  // namespace cfg
