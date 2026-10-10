#pragma once

#include <cstddef>
#include <cstdint>

#include "profile_data.h"

// jsmn : en-tête seul ici (types et déclarations) ; l'implémentation est incluse dans profile_codec.cpp.
#ifndef JSMN_H
#define JSMN_HEADER
#endif
#include "../../third_party/jsmn/jsmn.h"

// Lecture et écriture d'un profil au format JSON (voir plan_loops.md).
//
//   {
//     "format": "dmp", "version": 1, "profile": "Default",
//     "samples": ["Kick1.wav", "Snare1.wav"],
//     "loops": [ { "bpm": 120, "tracks": [
//         { "sample": 0, "gain": 1.0, "pitch": 1.0, "pattern": "x...x...x...x...",
//           "velocity": "127,90,...", "step_pitch": "0,3,...", "step_pan": "0,-25,...", "step_offset": "0,-16,...",
//           "extra_hits": "4:-16:100:0:0 4:16:80:0:30", "quantize": 30, "width": -20, "balance": 10,
//           "attack": 5, "decay": 200, "sustain": 70, "release": 20 } ] } ]
//   }
//
// « velocity » (1..127), « step_pitch » (-12..12, demi-tons), « step_pan » (-50..50, gauche à droite) et
// « step_offset » (-64..63, 1/128 de pas) sont des listes de nombres dans une chaîne : un seul jeton pour jsmn,
// au lieu de 17 pour un tableau JSON. Elles ne sont écrites que si une valeur n'est pas celle par défaut
// (127, 0, 0 et 0) ; absentes ou plus courtes que 16, les valeurs manquantes prennent la valeur par défaut.
// « extra_hits » : les notes supplémentaires des pas (roulements), sous la forme « pas:décalage:vélocité:pitch:pan »
// séparées par des espaces, dans l'ordre du temps pour chaque pas (3 au plus par pas). Une entrée à quatre
// champs (anciens fichiers, sans pan) est lue avec un pan de 0 ; l'écriture met toujours les cinq.
// « quantize » : force de quantification de la piste, entier de 0 à 100.
// « width » (largeur stéréo de la piste : -100 mono, 0 inchangé, +100 côtés doublés) : entier de -100 à 100, et
// « balance » (balance de base de la piste) : entier de -50 à 50. Écrits seulement s'ils ne sont pas nuls.
// « attack », « decay », « release » (ms, 0 à 2000) et « sustain » (%, 0 à 100) : l'enveloppe ADSR de la piste,
// écrits seulement s'ils ne valent pas leur défaut (0, 0, 5 et 100) ; absents : le défaut.
//
// Le découpage syntaxique est fait par jsmn (générique, sans allocation) ; la couche de ce fichier
// ne fait que faire correspondre les clés connues à ProfileData. Les clés inconnues sont ignorées :
// ajouter un champ plus tard = lire une clé de plus, avec une valeur par défaut si elle est absente.
namespace profile {

constexpr int FORMAT_VERSION = 1;

enum class ProfileStatus : uint8_t {
    Ok,
    Truncated,    // le JSON s'arrête avant la fin (fichier coupé)
    InvalidJson,  // syntaxe invalide ou structure incohérente
    NotAProfile,  // JSON valide, mais pas un profil de cette application
    TooNew,       // écrit par une version plus récente du programme
    TooBig,       // trop de loops, de pistes, de samples ou de jetons pour les tableaux fixes
    BadField,     // champ de mauvais type, hors limites, trop long ou mal formé
};

// Message en français pour l'afficher ou le journaliser.
const char* describe(ProfileStatus status);

// Contient le tableau de jetons de jsmn (environ 100 Ko) : à déclarer static ou sur le tas,
// jamais sur la pile.
class ProfileCodec {
public:
    // Pire cas : 33 jetons par piste (les 16 clés avec leurs valeurs, plus l'objet) x 16 pistes x 12 loops,
    // plus 5 jetons par loop et 75 pour l'en-tête et la table de samples : 6 471, avec de la marge.
    static constexpr int MAX_TOKENS = 6656;

    // Lit un profil. En cas d'échec `out` est invalide : le lire dans un objet de travail, pas
    // dans celui qu'on utilise. Le texte n'a pas besoin d'être terminé par un zéro.
    ProfileStatus parse(const char* text, std::size_t length, ProfileData& out);

    // Écrit le profil en JSON dans `buffer` (terminé par un zéro). Retourne la longueur écrite,
    // sans le zéro, ou -1 si le buffer est trop petit ou si les données sont invalides (index de
    // sample hors table, caractère de contrôle dans un nom, nombre non fini).
    static int write(const ProfileData& profile, char* buffer, std::size_t capacity);

private:
    jsmntok_t tokens_[MAX_TOKENS];
};

}  // namespace profile
