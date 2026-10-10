#include "profile_status.h"

namespace profile {

const char* describe(ProfileStatus status) {
    switch (status) {
        case ProfileStatus::Ok:          return "ok";
        case ProfileStatus::Truncated:   return "fichier incomplet (coupé)";
        case ProfileStatus::InvalidJson: return "fichier invalide (JSON mal formé)";
        case ProfileStatus::NotAProfile: return "ce n'est pas un profil de drum machine";
        case ProfileStatus::TooNew:      return "profil écrit par une version plus récente";
        case ProfileStatus::TooBig:      return "profil trop grand";
        case ProfileStatus::BadField:    return "champ invalide dans le profil";
        case ProfileStatus::BadChecksum: return "profil abîmé (somme de contrôle fausse)";
    }
    return "erreur";
}

}  // namespace profile
