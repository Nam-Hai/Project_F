#include "profile_store_file.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

FileProfileStore::FileProfileStore(std::string path)
    : path_(std::move(path)), codec_(new profile::ProfileCodec) {}

bool FileProfileStore::fail(const std::string& message) {
    error_ = message;
    return false;
}

profile::LoadResult FileProfileStore::load(profile::ProfileData& out) {
    error_.clear();

    std::error_code ec;
    if (!fs::exists(path_, ec)) {
        out = profile::ProfileData{};
        return profile::LoadResult::Missing;
    }

    std::ifstream file(path_, std::ios::binary);
    if (!file) {
        fail("profil illisible");
        return profile::LoadResult::Failed;
    }
    const std::vector<char> text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    const profile::ProfileStatus status = codec_->parse(text.data(), text.size(), out);
    if (status != profile::ProfileStatus::Ok) {
        fail(profile::describe(status));
        return profile::LoadResult::Failed;
    }
    return profile::LoadResult::Loaded;
}

bool FileProfileStore::save(const profile::ProfileData& profile) {
    error_.clear();

    std::vector<char> text(256 * 1024);  // le pire cas (12 loops de 16 pistes, tout rempli) atteint 215 Ko avec les pans
    const int         len = profile::ProfileCodec::write(profile, text.data(), text.size());
    if (len < 0) return fail("profil invalide ou trop grand");

    std::error_code ec;
    const fs::path  target(path_);
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
        if (ec) return fail("dossier du profil impossible à créer");
    }

    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return fail("écriture impossible");
        out.write(text.data(), len);
        out.flush();
        if (!out) {
            out.close();
            fs::remove(tmp, ec);
            return fail("écriture interrompue");
        }
    }

    // L'ancienne version est gardée à côté, au cas où.
    if (fs::exists(target, ec)) {
        fs::copy_file(target, path_ + ".bak", fs::copy_options::overwrite_existing, ec);
        if (ec) {
            fs::remove(tmp, ec);
            return fail("copie de sauvegarde impossible");
        }
    }

    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return fail("remplacement du fichier impossible");
    }
    return true;
}
