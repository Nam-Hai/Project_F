#include "profile_store_file.h"

#include <filesystem>
#include <fstream>

#include "../../core/profile/binary_codec.h"
#include "file_stream.h"

namespace fs = std::filesystem;

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

    FileSource                  source(file);
    const profile::ProfileStatus status = profile::BinaryCodec::read(source, out);
    if (status != profile::ProfileStatus::Ok) {
        fail(profile::describe(status));
        return profile::LoadResult::Failed;
    }
    return profile::LoadResult::Loaded;
}

bool FileProfileStore::save(const profile::ProfileData& profile) {
    error_.clear();

    if (!profile::BinaryCodec::valid(profile)) return fail("profil invalide ou trop grand");

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
        FileSink sink(out);
        const bool written = profile::BinaryCodec::write(profile, sink);
        out.flush();
        if (!written || !out) {
            out.close();
            fs::remove(tmp, ec);
            return fail("écriture interrompue");
        }
    }

    // The old version is kept next to it, just in case.
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
