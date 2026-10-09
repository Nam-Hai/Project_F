#pragma once

#include "profile_data.h"

namespace profile {

enum class LoadResult : uint8_t {
    Loaded,   // profil lu
    Missing,  // pas encore de profil : `out` est un profil neuf et vide
    Failed,   // fichier illisible ou invalide : `out` est inutilisable, voir last_error()
};

// Où vit le profil : un fichier sur PC, la carte SD sur Teensy. L'UI ne connaît que cette interface.
class ProfileStore {
public:
    virtual ~ProfileStore() = default;

    virtual LoadResult load(ProfileData& out) = 0;

    // Sauvegarde sans jamais abîmer l'ancienne version en cas d'échec. false si échec.
    virtual bool save(const ProfileData& profile) = 0;

    // Message de la dernière erreur (en français).
    virtual const char* last_error() const = 0;
};

}  // namespace profile
