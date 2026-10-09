#pragma once

// ============================================================================
//  LOOP INITIALE (PC) : c'est ici qu'on règle le tempo, les samples et les patterns.
//  Modifier puis recompiler :  cmake --build build && ./build/drum_machine
// ============================================================================

// Tempo. Un pas = une double croche (1/16), donc 16 pas = 1 mesure.
constexpr float DEFAULT_BPM = 120.0f;

struct TrackSetup {
    // "synth:kick"  -> kick synthétisé par le moteur
    // "mon_son.wav" -> fichier du dossier samples/ (WAV mono, 16 bits, 44,1 kHz)
    const char* sample;
    float       pitch;    // 1.0 = normal, 0.5 = une octave plus bas, 2.0 = une octave plus haut
    float       gain;     // volume de la piste, 1.0 = plein
    const char* pattern;  // 16 pas : 'x' = trig, '.' = silence
};

// Une ligne = une piste (16 pistes max, dans l'ordre).
constexpr TrackSetup DEFAULT_TRACKS[] = {
    //  sample          pitch  gain   pattern (16 pas)
    {"Kick1.wav",      1.0f,  1.0f,  "x...x...x...x..."},
    {"Snare1.wav",    1.0f,  0.8f,  "....x.......x..."},
    // {"hat.wav",      1.0f,  0.5f,  "..x...x...x...x."},
};
