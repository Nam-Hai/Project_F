#pragma once

#include <cstdint>
#include <cstring>

#include "../../config.h"

// Modèle d'un profil : une collection de loops. Portable, tableaux de taille fixe, aucune allocation.
//
// Une loop = BPM + pistes. Chaque piste référence un sample par son NOM, via la table de samples
// du profil : l'audio n'est jamais sauvegardé, il reste dans samples/ (PC) ou sur la carte SD.
namespace profile {

constexpr int MAX_LOOPS         = 32;
constexpr int MAX_SAMPLES       = 64;
constexpr int SAMPLE_NAME_LEN   = 47;  // caractères d'un nom de fichier (sans le zéro final)
constexpr int PROFILE_NAME_LEN  = 23;

static_assert(cfg::MAX_STEPS == 16 && cfg::VELOCITY_MAX == 127,
              "les valeurs par défaut de TrackData sont écrites pour 16 pas et une vélocité maximale de 127");

struct TrackData {
    uint8_t  sample  = 0;      // index dans la table de samples du profil
    float    gain    = 1.0f;
    float    pitch   = 1.0f;   // pitch de la piste (rapport de vitesse)
    uint16_t pattern = 0;      // bit i = pas i (même convention que l'UI)

    // Valeurs de chaque pas (conservées même si le pas est éteint) : vélocité 1..127, pitch -12..12.
    uint8_t step_velocity[16] = {127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127};
    int8_t  step_pitch[16]    = {};
};

struct LoopData {
    float     bpm         = 120.0f;
    uint8_t   track_count = 0;
    TrackData tracks[cfg::MAX_TRACKS];
};

struct ProfileData {
    char     name[PROFILE_NAME_LEN + 1] = "Default";
    uint8_t  sample_count = 0;
    char     samples[MAX_SAMPLES][SAMPLE_NAME_LEN + 1] = {};
    uint8_t  loop_count = 0;
    LoopData loops[MAX_LOOPS];

    // Index d'un sample dans la table, ou -1.
    int find_sample(const char* file) const {
        for (int i = 0; i < sample_count; ++i)
            if (std::strcmp(samples[i], file) == 0) return i;
        return -1;
    }

    // Index du sample dans la table ; il y est ajouté s'il n'y est pas encore. -1 si la table est
    // pleine ou si le nom est trop long.
    int add_sample(const char* file) {
        const int found = find_sample(file);
        if (found >= 0) return found;
        if (sample_count >= MAX_SAMPLES || std::strlen(file) > static_cast<std::size_t>(SAMPLE_NAME_LEN)) return -1;
        std::strcpy(samples[sample_count], file);
        return sample_count++;
    }

    // Retire de la table les samples qu'aucune loop n'utilise et renumérote les pistes, pour que la
    // table ne se remplisse pas de samples abandonnés au fil des sauvegardes.
    void compact_samples() {
        bool used[MAX_SAMPLES] = {};
        for (int l = 0; l < loop_count; ++l)
            for (int t = 0; t < loops[l].track_count; ++t)
                if (loops[l].tracks[t].sample < sample_count) used[loops[l].tracks[t].sample] = true;

        int remap[MAX_SAMPLES] = {};
        int kept               = 0;
        for (int i = 0; i < sample_count; ++i) {
            if (!used[i]) continue;
            remap[i] = kept;
            if (kept != i) std::strcpy(samples[kept], samples[i]);
            ++kept;
        }
        for (int i = kept; i < sample_count; ++i) samples[i][0] = '\0';
        sample_count = static_cast<uint8_t>(kept);

        for (int l = 0; l < loop_count; ++l)
            for (int t = 0; t < loops[l].track_count; ++t)
                if (loops[l].tracks[t].sample < MAX_SAMPLES) loops[l].tracks[t].sample = static_cast<uint8_t>(remap[loops[l].tracks[t].sample]);
    }

    // Supprime la loop `index` ; les suivantes remontent. Ne touche pas à la table de samples.
    void remove_loop(int index) {
        if (index < 0 || index >= loop_count) return;
        for (int i = index; i < loop_count - 1; ++i) loops[i] = loops[i + 1];
        loops[--loop_count] = LoopData{};
    }
};

}  // namespace profile
