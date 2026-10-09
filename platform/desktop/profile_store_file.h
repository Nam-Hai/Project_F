#pragma once

#include <memory>
#include <string>

#include "../../core/profile/profile_codec.h"
#include "../../core/profile/profile_store.h"

// Profil dans un fichier JSON du disque.
//
// Écriture sécurisée : le nouveau contenu est écrit dans « <fichier>.tmp », l'ancienne version est
// copiée dans « <fichier>.bak », puis le .tmp remplace le fichier (renommage). Une panne en cours
// d'écriture laisse donc toujours une version complète, et un fichier abîmé n'est jamais écrasé
// sans avoir été mis de côté.
class FileProfileStore : public profile::ProfileStore {
public:
    explicit FileProfileStore(std::string path);

    profile::LoadResult load(profile::ProfileData& out) override;
    bool                save(const profile::ProfileData& profile) override;
    const char*         last_error() const override { return error_.c_str(); }

private:
    bool fail(const std::string& message);

    std::string                            path_;
    std::unique_ptr<profile::ProfileCodec> codec_;  // ~100 Ko : sur le tas
    std::string                            error_;
};
